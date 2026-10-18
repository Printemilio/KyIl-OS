/* POSIX signals on native Vex events.
 *
 * Vex delivers numbered native event classes (unos/vex_event_abi.h). A POSIX
 * signal that has a native class is registered with the kernel and can be
 * sent to other processes; asynchronous events are delivered when the
 * receiving thread returns from a system call. CPU exceptions (SIGSEGV,
 * SIGBUS, SIGILL, SIGFPE, SIGTRAP) share the synchronous fault class.
 *
 * The other signals exist only inside a process: raise() and pthread_kill()
 * deliver them synchronously, honouring masks and dispositions, and kill()
 * refuses to send them elsewhere (EINVAL).
 *
 * Every handler runs through vex_signal_entry, which builds a full siginfo_t
 * and ucontext_t, applies sa_mask and SA_RESETHAND, and returns to the
 * kernel with VEX_SYS_EVENT_RETURN. */
#define _GNU_SOURCE
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ucontext.h>
#include "pthread_impl.h"
#include "libc.h"
#include "lock.h"

#define SIGSET_WORDS (_NSIG/(8*sizeof(long)))

static const unsigned char native_class[_NSIG] = {
	[SIGINT] = VEX_EVENT_USER_INTERRUPT,
	[SIGTERM] = VEX_EVENT_TERMINATE,
	[SIGHUP] = VEX_EVENT_HANGUP,
	[SIGQUIT] = VEX_EVENT_QUIT,
	[SIGUSR1] = VEX_EVENT_USER_1,
	[SIGUSR2] = VEX_EVENT_USER_2,
	[SIGALRM] = VEX_EVENT_ALARM,
	[SIGPIPE] = VEX_EVENT_BROKEN_PIPE,
	[SIGCHLD] = VEX_EVENT_CHILD,
	[SIGWINCH] = VEX_EVENT_WINDOW_CHANGE,
	[SIGCONT] = VEX_EVENT_CONTINUE,
};

static int is_fault_signal(int sig)
{
	return sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE || sig == SIGTRAP;
}

/* Signals whose default action is to be ignored. Vex has no job control,
 * so the stop signals cannot stop a process either. */
static int default_ignored(int sig)
{
	switch (sig) {
	case SIGCHLD: case SIGCONT: case SIGURG: case SIGWINCH:
	case SIGTSTP: case SIGTTIN: case SIGTTOU:
		return 1;
	}
	return 0;
}

static struct sigaction actions[_NSIG];
static unsigned long handler_set[SIGSET_WORDS];
volatile int __eintr_valid_flag;

/* The POSIX mask of the calling thread; the native event mask mirrors the
 * signals that have a native class. Signals raised in-process while
 * blocked wait in `pending`. */
static __thread unsigned long thread_mask[SIGSET_WORDS];
static __thread unsigned long thread_pending[SIGSET_WORDS];

static int set_has(const unsigned long *set, int sig)
{
	return (set[(sig-1)/(8*sizeof(long))] >> ((sig-1)%(8*sizeof(long)))) & 1;
}

static void set_put(unsigned long *set, int sig, int on)
{
	unsigned long bit = 1UL << ((sig-1)%(8*sizeof(long)));
	if (on) set[(sig-1)/(8*sizeof(long))] |= bit;
	else set[(sig-1)/(8*sizeof(long))] &= ~bit;
}

void __get_handler_set(sigset_t *set)
{
	memcpy(set, handler_set, sizeof handler_set);
}

static uint64_t native_mask_of(const unsigned long *set)
{
	uint64_t native = 0;
	for (int sig = 1; sig < _NSIG; sig++)
		if (set_has(set, sig) && native_class[sig])
			native |= 1ull << native_class[sig];
	/* The fault class is never masked: a fault cannot be deferred. */
	return native;
}

static void apply_native_mask(void)
{
	vex_event_mask_req_t req = { .how = 2, .set = native_mask_of(thread_mask) };
	__vex_call1(VEX_SYS_EVENT_MASK, __vex_ptr(&req));
}

/* ---- delivery ---------------------------------------------------------- */

_Noreturn static void terminate_by(int sig)
{
	for (;;) __vex_call1(VEX_SYS_PROCESS_EXIT_GROUP, 128 + sig);
}

hidden void __vex_event_restore(void);
__asm__(
	".text\n"
	".hidden __vex_event_restore\n"
	".type __vex_event_restore,@function\n"
	"__vex_event_restore:\n"
	"	movabs $0x5645584e000000ad, %rax\n" /* tagged VEX_SYS_EVENT_RETURN */
	"	syscall\n"
	"	ud2\n"
);

static void register_native(int sig);

/* Handlers of native events currently running on this thread. A handler
 * left with siglongjmp never returns to the kernel: __vex_signal_leave tells
 * it so (see __sigsetjmp_tail). */
static __thread int native_handler_depth;

hidden void __vex_signal_leave(void)
{
	if (native_handler_depth > 0) {
		native_handler_depth = 0;
		__vex_call0(VEX_SYS_EVENT_LEAVE);
	}
}

static void run_handler(int sig, siginfo_t *si, ucontext_t *uc)
{
	struct sigaction sa = actions[sig];
	unsigned long saved[SIGSET_WORDS];

	if (sa.sa_flags & SA_RESETHAND) {
		actions[sig].sa_handler = SIG_DFL;
		actions[sig].sa_flags &= ~SA_SIGINFO;
		register_native(sig);
	}
	memcpy(saved, thread_mask, sizeof saved);
	memcpy(&uc->uc_sigmask, saved, sizeof saved);
	for (size_t i = 0; i < SIGSET_WORDS; i++)
		thread_mask[i] |= ((unsigned long *)&sa.sa_mask)[i];
	if (!(sa.sa_flags & SA_NODEFER)) set_put(thread_mask, sig, 1);
	apply_native_mask();

	if (sa.sa_flags & SA_SIGINFO) sa.sa_sigaction(sig, si, uc);
	else sa.sa_handler(sig);

	memcpy(thread_mask, saved, sizeof saved);
	apply_native_mask();
}

/* Asynchronous native event: rdi = signal number (delivery value),
 * rsi = kernel siginfo prefix (si_signo, si_code, si_pid at Linux offsets). */
static void async_entry(int sig, const siginfo_t *kinfo, void *kctx)
{
	siginfo_t si;
	ucontext_t uc;
	(void)kctx;
	memset(&si, 0, sizeof si);
	memset(&uc, 0, sizeof uc);
	si.si_signo = sig;
	if (kinfo) {
		si.si_code = SI_USER;
		si.si_pid = kinfo->si_pid;
	}
	if ((uintptr_t)actions[sig].sa_handler > 1) {
		native_handler_depth++;
		run_handler(sig, &si, &uc);
		native_handler_depth--;
	}
}

static int fault_signal(const vex_event_user_context_t *ctx, int *code)
{
	switch (ctx->vector) {
	case 0: *code = FPE_INTDIV; return SIGFPE;
	case 3: *code = TRAP_BRKPT; return SIGTRAP;
	case 6: *code = ILL_ILLOPN; return SIGILL;
	case 16: case 19: *code = FPE_FLTINV; return SIGFPE;
	case 17: *code = BUS_ADRALN; return SIGBUS;
	case 14: *code = (ctx->error_code & 1) ? SEGV_ACCERR : SEGV_MAPERR; return SIGSEGV;
	default: *code = SI_KERNEL; return SIGSEGV;
	}
}

static void context_to_ucontext(const vex_event_user_context_t *c, ucontext_t *uc)
{
	greg_t *g = uc->uc_mcontext.gregs;
	g[REG_R8] = c->r8; g[REG_R9] = c->r9; g[REG_R10] = c->r10; g[REG_R11] = c->r11;
	g[REG_R12] = c->r12; g[REG_R13] = c->r13; g[REG_R14] = c->r14; g[REG_R15] = c->r15;
	g[REG_RDI] = c->rdi; g[REG_RSI] = c->rsi; g[REG_RBP] = c->rbp; g[REG_RBX] = c->rbx;
	g[REG_RDX] = c->rdx; g[REG_RAX] = c->rax; g[REG_RCX] = c->rcx; g[REG_RSP] = c->rsp;
	g[REG_RIP] = c->rip; g[REG_EFL] = c->rflags; g[REG_TRAPNO] = c->vector;
	g[REG_ERR] = c->error_code; g[REG_CR2] = c->fault_address;
}

static void ucontext_to_context(const ucontext_t *uc, vex_event_user_context_t *c)
{
	const greg_t *g = uc->uc_mcontext.gregs;
	c->r8 = g[REG_R8]; c->r9 = g[REG_R9]; c->r10 = g[REG_R10]; c->r11 = g[REG_R11];
	c->r12 = g[REG_R12]; c->r13 = g[REG_R13]; c->r14 = g[REG_R14]; c->r15 = g[REG_R15];
	c->rdi = g[REG_RDI]; c->rsi = g[REG_RSI]; c->rbp = g[REG_RBP]; c->rbx = g[REG_RBX];
	c->rdx = g[REG_RDX]; c->rax = g[REG_RAX]; c->rcx = g[REG_RCX]; c->rsp = g[REG_RSP];
	c->rip = g[REG_RIP]; c->rflags = g[REG_EFL];
}

/* Synchronous fault: rsi = native fault payload, rdx = editable context. A
 * handler may change the context (for instance its RIP) before returning. */
static void fault_entry(uint64_t value, const void *payload, vex_event_user_context_t *ctx)
{
	siginfo_t si;
	ucontext_t uc;
	int code;
	int sig = fault_signal(ctx, &code);
	(void)value; (void)payload;

	if ((uintptr_t)actions[sig].sa_handler <= 1) {
		/* Default action, or an ignored fault that would only repeat. */
		terminate_by(sig);
	}
	memset(&si, 0, sizeof si);
	memset(&uc, 0, sizeof uc);
	si.si_signo = sig;
	si.si_code = code;
	si.si_addr = (void *)(uintptr_t)(ctx->fault_address ? ctx->fault_address : ctx->rip);
	context_to_ucontext(ctx, &uc);
	native_handler_depth++;
	run_handler(sig, &si, &uc);
	native_handler_depth--;
	ucontext_to_context(&uc, ctx);
}

static void register_class(unsigned cls, uint64_t handler, uint64_t value, int on_stack)
{
	vex_event_register_req_t req = {
		.event_id = (uint16_t)cls,
		.handler = handler,
		.restorer = handler > 1 ? __vex_ptr(__vex_event_restore) : 0,
		.flags = handler > 1 ? VEX_EVENT_SIGINFO | (on_stack ? VEX_EVENT_ON_ALT_STACK : 0) : 0,
		.delivery_value = value,
	};
	__vex_call1(VEX_SYS_EVENT_REGISTER, __vex_ptr(&req));
}

static void register_native(int sig)
{
	void (*h)(int) = actions[sig].sa_handler;
	if (is_fault_signal(sig)) {
		/* One native class carries every CPU fault: it uses the alternate
		 * stack when any fault handler asked for it. */
		int any = 0, on_stack = 0;
		for (int s = 1; s < _NSIG; s++)
			if (is_fault_signal(s) && (uintptr_t)actions[s].sa_handler > 1) {
				any = 1;
				on_stack |= (actions[s].sa_flags & SA_ONSTACK) != 0;
			}
		register_class(VEX_EVENT_SYNCHRONOUS_FAULT, any ? __vex_ptr(fault_entry) : 0, 0, on_stack);
		return;
	}
	if (!native_class[sig]) return;
	if (h == SIG_IGN || (h == SIG_DFL && default_ignored(sig)))
		register_class(native_class[sig], VEX_EVENT_IGNORE_HANDLER, sig, 0);
	else if (h == SIG_DFL)
		register_class(native_class[sig], 0, sig, 0);
	else
		register_class(native_class[sig], __vex_ptr(async_entry), sig,
			(actions[sig].sa_flags & SA_ONSTACK) != 0);
}

/* Called once at startup: the kernel's default for an unregistered class
 * would report the class number, not the POSIX signal, and would terminate
 * on signals POSIX ignores by default. */
hidden void __vex_signal_init(void)
{
	for (int sig = 1; sig < _NSIG; sig++)
		if (native_class[sig]) register_native(sig);
}

/* ---- dispositions -------------------------------------------------------- */

int __libc_sigaction(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old)
{
	if (old) *old = actions[sig];
	if (!sa) return 0;
	if ((uintptr_t)sa->sa_handler > 1UL) {
		a_or_l(handler_set+(sig-1)/(8*sizeof(long)), 1UL<<(sig-1)%(8*sizeof(long)));
		if (!(sa->sa_flags & SA_RESTART)) a_store(&__eintr_valid_flag, 1);
	}
	actions[sig] = *sa;
	register_native(sig);
	return 0;
}

int __sigaction(int sig, const struct sigaction *restrict sa, struct sigaction *restrict old)
{
	unsigned long set[SIGSET_WORDS];

	if (sig-32U < 3 || sig-1U >= _NSIG-1) {
		errno = EINVAL;
		return -1;
	}
	if (sa && (sig == SIGKILL || sig == SIGSTOP)) {
		errno = EINVAL;
		return -1;
	}
	if (sig == SIGABRT) {
		__block_all_sigs(&set);
		LOCK(__abort_lock);
	}
	int r = __libc_sigaction(sig, sa, old);
	if (sig == SIGABRT) {
		UNLOCK(__abort_lock);
		__restore_sigs(&set);
	}
	return r;
}
weak_alias(__sigaction, sigaction);

/* ---- masks --------------------------------------------------------------- */

static void deliver_local(int sig);

static void flush_pending(void)
{
	for (int sig = 1; sig < _NSIG; sig++)
		if (set_has(thread_pending, sig) && !set_has(thread_mask, sig)) {
			set_put(thread_pending, sig, 0);
			deliver_local(sig);
		}
}

int pthread_sigmask(int how, const sigset_t *restrict set, sigset_t *restrict old)
{
	if (old) {
		memset(old, 0, sizeof *old);
		memcpy(old, thread_mask, sizeof thread_mask);
	}
	if (!set) return 0;
	const unsigned long *s = (const unsigned long *)set;
	switch (how) {
	case SIG_BLOCK:
		for (size_t i = 0; i < SIGSET_WORDS; i++) thread_mask[i] |= s[i];
		break;
	case SIG_UNBLOCK:
		for (size_t i = 0; i < SIGSET_WORDS; i++) thread_mask[i] &= ~s[i];
		break;
	case SIG_SETMASK:
		memcpy(thread_mask, s, sizeof thread_mask);
		break;
	default:
		return EINVAL;
	}
	set_put(thread_mask, SIGKILL, 0);
	set_put(thread_mask, SIGSTOP, 0);
	apply_native_mask();
	flush_pending();
	return 0;
}

void __block_all_sigs(void *set)
{
	sigset_t all;
	sigfillset(&all);
	pthread_sigmask(SIG_BLOCK, &all, set);
}

void __block_app_sigs(void *set)
{
	sigset_t app;
	sigfillset(&app);
	pthread_sigmask(SIG_BLOCK, &app, set);
}

void __restore_sigs(void *set)
{
	pthread_sigmask(SIG_SETMASK, set, 0);
}

int sigpending(sigset_t *set)
{
	/* Only in-process pending signals are visible: the kernel does not
	 * report pending native events. */
	memset(set, 0, sizeof *set);
	memcpy(set, thread_pending, sizeof thread_pending);
	return 0;
}

/* ---- sending ------------------------------------------------------------- */

static void deliver_local(int sig)
{
	void (*h)(int) = actions[sig].sa_handler;
	if (h == SIG_IGN || (h == SIG_DFL && default_ignored(sig))) return;
	if (h == SIG_DFL) terminate_by(sig);
	siginfo_t si;
	ucontext_t uc;
	memset(&si, 0, sizeof si);
	memset(&uc, 0, sizeof uc);
	si.si_signo = sig;
	si.si_code = SI_TKILL;
	si.si_pid = getpid();
	run_handler(sig, &si, &uc);
}

static int post_event(pid_t pid, int tid, unsigned cls)
{
	vex_event_post_req_t req = {
		.process_id = (uint32_t)pid,
		.thread_id = (uint32_t)tid,
		.event_id = (uint16_t)cls,
	};
	if (__vex_call1(VEX_SYS_EVENT_POST, __vex_ptr(&req)) == (uint64_t)-1) {
		errno = ESRCH;
		return -1;
	}
	return 0;
}

/* Send to one thread of this process. */
static int signal_thread(int tid, int sig)
{
	if (!sig) return 0;
	if (sig == SIGKILL) terminate_by(SIGKILL);
	if (native_class[sig] && !is_fault_signal(sig))
		return post_event(getpid(), tid, native_class[sig]);
	if (tid != __pthread_self()->tid) {
		/* An in-process signal cannot interrupt another thread. */
		errno = EINVAL;
		return -1;
	}
	if (set_has(thread_mask, sig)) set_put(thread_pending, sig, 1);
	else deliver_local(sig);
	return 0;
}

int raise(int sig)
{
	if (sig-1U >= _NSIG-1) {
		errno = EINVAL;
		return -1;
	}
	return signal_thread(__pthread_self()->tid, sig);
}

int pthread_kill(pthread_t t, int sig)
{
	if (sig-1U >= _NSIG-1 && sig) return EINVAL;
	if (!t->tid) return ESRCH;
	return signal_thread(t->tid, sig) ? errno : 0;
}

int kill(pid_t pid, int sig)
{
	pid_t self = getpid();
	if (sig < 0 || sig >= _NSIG) {
		errno = EINVAL;
		return -1;
	}
	if (pid == 0 || pid == self) {
		if (!sig) return 0;
		if (native_class[sig] && !is_fault_signal(sig)) return post_event(self, 0, native_class[sig]);
		return raise(sig);
	}
	if (pid < 0) {
		/* There are no process groups to address. */
		errno = pid == -1 ? EPERM : ESRCH;
		return -1;
	}
	if (sig == SIGKILL) {
		if (__vex_call1(VEX_SYS_PROCESS_KILL, (uint64_t)pid) == (uint64_t)-1) {
			errno = ESRCH;
			return -1;
		}
		return 0;
	}
	if (!sig) {
		/* Existence checks need a native probe that does not exist yet. */
		errno = ENOSYS;
		return -1;
	}
	if (!native_class[sig] || is_fault_signal(sig)) {
		errno = EINVAL;
		return -1;
	}
	return post_event(pid, 0, native_class[sig]);
}

int sigqueue(pid_t pid, int sig, const union sigval value)
{
	(void)value; /* Native events carry no payload. */
	return kill(pid, sig);
}

/* ---- waiting ------------------------------------------------------------- */

int sigsuspend(const sigset_t *mask)
{
	sigset_t old;
	pthread_sigmask(SIG_SETMASK, mask, &old);
	pause();
	pthread_sigmask(SIG_SETMASK, &old, 0);
	errno = EINTR;
	return -1;
}

static volatile int waited_signal;

static void record_signal(int sig)
{
	a_store(&waited_signal, sig);
}

int sigtimedwait(const sigset_t *restrict mask, siginfo_t *restrict si, const struct timespec *restrict timeout)
{
	struct sigaction saved[_NSIG], catcher = { .sa_handler = record_signal };
	sigset_t old;
	int sig = 0;

	/* A signal raised in-process may already be pending. */
	for (int s = 1; s < _NSIG; s++)
		if (sigismember(mask, s) == 1 && set_has(thread_pending, s)) {
			set_put(thread_pending, s, 0);
			sig = s;
			break;
		}
	if (!sig) {
		waited_signal = 0;
		for (int s = 1; s < _NSIG; s++)
			if (sigismember(mask, s) == 1 && native_class[s] && !is_fault_signal(s))
				__libc_sigaction(s, &catcher, &saved[s]);
		pthread_sigmask(SIG_UNBLOCK, mask, &old);
		uint64_t ns = 0;
		if (timeout) {
			ns = (uint64_t)timeout->tv_sec * 1000000000u + (uint64_t)timeout->tv_nsec;
			if (!ns) ns = 1;
		}
		while (!waited_signal) {
			int r = __vex_futex_wait(&waited_signal, 0, ns);
			if (r == -ETIMEDOUT) break;
			if (r == -EINTR && !waited_signal) break;
		}
		pthread_sigmask(SIG_SETMASK, &old, 0);
		for (int s = 1; s < _NSIG; s++)
			if (sigismember(mask, s) == 1 && native_class[s] && !is_fault_signal(s))
				__libc_sigaction(s, &saved[s], 0);
		sig = waited_signal;
		if (!sig) {
			errno = timeout ? EAGAIN : EINTR;
			return -1;
		}
	}
	if (si) {
		memset(si, 0, sizeof *si);
		si->si_signo = sig;
		si->si_code = SI_USER;
	}
	return sig;
}

/* The per-thread alternate stack lives in the kernel, which builds the
 * frame of an SA_ONSTACK handler on it. */
int sigaltstack(const stack_t *restrict ss, stack_t *restrict old)
{
	vex_event_alt_stack_req_t req = { 0 };
	if (ss) {
		if (ss->ss_flags & ~SS_DISABLE) {
			errno = EINVAL;
			return -1;
		}
		req.set = 1;
		if (!(ss->ss_flags & SS_DISABLE)) {
			if (ss->ss_size < MINSIGSTKSZ) {
				errno = ENOMEM;
				return -1;
			}
			req.base = __vex_ptr(ss->ss_sp);
			req.size = ss->ss_size;
		}
	}
	if (__vex_ret(__vex_call1(VEX_SYS_EVENT_ALT_STACK, __vex_ptr(&req))) < 0) return -1;
	if (old) {
		old->ss_sp = (void *)(uintptr_t)req.old_base;
		old->ss_size = req.old_size;
		old->ss_flags = req.old_size ? (req.old_active ? SS_ONSTACK : 0) : SS_DISABLE;
	}
	return 0;
}

/* ---- abort --------------------------------------------------------------- */

_Noreturn void abort(void)
{
	raise(SIGABRT);

	/* The handler returned, or SIGABRT was blocked or ignored: the process
	 * must still end, with the default action. */
	__block_all_sigs(0);
	LOCK(__abort_lock);
	actions[SIGABRT].sa_handler = SIG_DFL;
	terminate_by(SIGABRT);
}
