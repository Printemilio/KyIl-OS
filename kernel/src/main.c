#include <limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unos/arp.h>
#include <unos/boot_time.h>
#include <unos/boot_requests.h>
#include <unos/compositor_surface.h>
#include <unos/versioned_store.h>
#include <unos/kernel_allocator.h>
#include <unos/ora.h>
#include <unos/process_memory.h>
#include <unos/disk.h>
#include <unos/dhcp.h>
#include <unos/debug_log.h>
#include <unos/elevation.h>
#include <unos/ethernet.h>
#include <unos/elf.h>
#include <unos/security.h>
#include <unos/framebuffer.h>
#include <unos/random.h>
#include <unos/boot_console.h>
#include <unos/power.h>
#include <unos/gdt.h>
#include <unos/fs.h>
#include <unos/gpu.h>
#include <unos/gui.h>
#include <unos/heap.h>
#include <unos/ipc.h>
#include <unos/inst.h>
#include <unos/iso9660.h>
#include <unos/irq.h>
#include <unos/ipv4.h>
#include <unos/keyboard.h>
#include <unos/mouse.h>
#include <unos/netdev.h>
#include <unos/pe.h>
#include <unos/pmm.h>
#include <unos/rtc.h>
#include <unos/driver_manager.h>
#include <unos/scheduler.h>
#include <unos/profiler.h>
#include <unos/shell.h>
#include <unos/spotlight.h>
#include <unos/syscall.h>
#include <unos/timezone.h>
#include <unos/ui_theme.h>
#include <unos/tty.h>
#include <unos/vex_abi.h>
#include <unos/udp.h>
#include <unos/users.h>
#include <unos/vmm.h>
#include <unos/voice.h>
#include <unos/smp.h>

#define NET_LINK_WAIT_TIMEOUT_MS 150ull
#define NET_LINK_STABLE_POLLS 2u
#define GUI_PROCESS_PRIORITY 1u

static void halt_forever(void) {
    /* Idle with interrupts enabled so the timer can always wake the CPU and
     * resume scheduling. Halting with interrupts masked would hang forever. */
    for (;;) {
        __asm__ volatile ("sti; hlt");
    }
}

static void cpu_enable_sse(void) {
    uint64_t cr0;
    uint64_t cr4;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    boot_console_puts("  CPU initial CR0=");boot_console_hex(cr0);boot_console_puts("\n");
    /* Match AP initialization: FXSAVE/FXRSTOR require both EM and TS clear.
       KyIlOS uses eager state switching, not a lazy #NM ownership mechanism. */
    cr0 &= ~((1ull << 2u) | (1ull << 3u));
    cr0 |= (1ull << 1u);  /* MP: pair WAIT/FWAIT with TS correctly. */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ull << 9u);  /* OSFXSR: enable FXSAVE/FXRSTOR and XMM regs. */
    cr4 |= (1ull << 10u); /* OSXMMEXCPT: enable SSE exceptions. */
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");
}

static uint64_t boot_step_begin(void) {
    return boot_time_ms();
}

static uint64_t boot_step_elapsed(uint64_t start_ms) {
    return boot_time_ms() - start_ms;
}

static void wait_for_net_link(void) {
    const uint64_t start_ms = boot_time_ms();
    uint32_t stable_polls = 0;

    while (boot_time_ms() - start_ms < NET_LINK_WAIT_TIMEOUT_MS &&
           stable_polls < NET_LINK_STABLE_POLLS) {
        if (netdev_link_up()) {
            ++stable_polls;
        } else {
            stable_polls = 0;
        }

        __asm__ volatile ("pause");
    }
}

static void append_string(char *dst, unsigned long capacity, unsigned long *length, const char *src) {
    while (*src != '\0' && *length + 1u < capacity) {
        dst[*length] = *src;
        ++(*length);
        ++src;
    }

    dst[*length] = '\0';
}

static const char *path_basename(const char *path) {
    const char *name = path;

    if (path == NULL || path[0] == '\0') {
        return "module";
    }

    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' && cursor[1] != '\0') {
            name = cursor + 1;
        }
    }

    return name;
}

/* Application keys still exercise the historical ABI's aligned numeric
 * family, but are no longer the process-memory or filesystem inventory. */
static int versioned_aligned_key_smoke(void) {
    const uint32_t owner=0xfffffd01u;const uint64_t base=UINT64_C(0x76ee990000000000);
    int result=-1;
    for(unsigned i=0;i<2048;++i) {
        uint64_t key=base+((uint64_t)i<<12),value=key^UINT64_C(0x1928aabbccddeeff);
        if(versioned_put(owner,key,&value,sizeof(value),NULL))goto done;
    }
    for(unsigned i=0;i<2048;++i) {
        uint64_t key=base+((uint64_t)i<<12),expected=key^UINT64_C(0x1928aabbccddeeff);
        if(!(i%17)){expected^=UINT64_C(0xff00ff00ff00ff00);if(versioned_put(owner,key,&expected,sizeof(expected),NULL))goto done;}
        ora_ref_t content=versioned_acquire(owner,key,0,NULL);
        int match=content && *(const uint64_t *)ora_data(content)==expected;ora_release(content);if(!match)goto done;
        versioned_remove_all(owner,key);
        content=versioned_acquire(owner,key,0,NULL);if(content){ora_release(content);goto done;}
    }
    result=0;
done:
    versioned_reap(owner);return result;
}

static int string_equals(const char *left, const char *right) {
    size_t i = 0;

    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return 0;
        }
        ++i;
    }

    return left[i] == right[i];
}

static void boot_log_u64_dec(uint64_t value) {
    char digits[21];
    size_t used = 0u;

    do {
        digits[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && used < sizeof(digits));
    while (used != 0u) {
        char character[2] = { digits[--used], '\0' };
        debug_log_puts(character);
    }
}

/* auto_native_test: the native Vex C library suite. The runner is itself a
   Vex libc program; it spawns every test of /usr/native-tests and reports on
   the serial log. */
static void native_test_task(void) {
    uint32_t child=0u,reaped=0u;int status=-1;
    debug_log_puts("native-tests: starting runner\n");
    if(elf_spawn_pid("/usr/native-tests/native_test_runner",&child) || !child) {
        debug_log_puts("native-tests: runner load FAILED\n");process_exit();
    }
    /* A suite that has not finished after 60 s is profiled once (where every
       CPU spends its time, by thread and user symbol), then waited for. */
    for (unsigned second = 0u;; ++second) {
        int collected = process_wait_pid((int32_t)child,&status,VEX_PROCESS_WAIT_NOHANG,&reaped);
        if (collected == (int)child) break;
        if (collected < 0) {
            debug_log_puts("native-tests: runner wait FAILED\n");
            break;
        }
        if (second == 20u) {
            debug_log_puts("native-tests: still running after 20 s, profiling 5 s\n");
            scheduler_dump_user_threads();
            scheduler_flow_report();
            if (profiler_start(5u) == 0) {
                process_sleep_ticks(600u);
                profiler_report();
            }
        }
        process_sleep_ticks(100u); /* 1 s */
    }
    debug_log_puts("native-tests: runner exit=");
    boot_log_u64_dec((uint64_t)(unsigned int)status);debug_log_puts("\n");
    process_exit();
}

static int graphics_run_gl;
static void flow_test_runner(void) {
    uint32_t child=0;int status=-1;
    if(elf_spawn_pid("/usr/linux-tests/flowsched_runtime_test",&child) || !child) {
        debug_log_puts("flowsched-runtime: load FAILED\n");process_exit();
    }
    for(unsigned i=0;i<36 && scheduler_process_alive(child);i++) {
        process_sleep_ticks(500);scheduler_flow_report();
        uint32_t collected=0;
        if(process_wait_pid((int32_t)child,&status,VEX_PROCESS_WAIT_NOHANG,&collected)==(int)child)break;
    }
    debug_log_puts(status==0?"flowsched-runtime: PASS\n":"flowsched-runtime: FAILED or timeout\n");
    process_exit();
}
static void graphics_test_child(const char *path,const char *label) {
    uint32_t pid=0u,reaped=0u;int status=-1;
    debug_log_puts(label);debug_log_puts(": run queued\n");
    if(elf_spawn_pid(path,&pid)==0&&pid!=0u&&
       process_wait_pid((int32_t)pid,&status,0u,&reaped)==(int)pid&&reaped==pid){
        debug_log_puts(label);debug_log_puts(": exit=");
        boot_log_u64_dec((uint64_t)(unsigned int)status);debug_log_puts("\n");
    }else {debug_log_puts(label);debug_log_puts(": load/wait failed\n");}
}
static void sigreturn_test_runner(void) {
    uint32_t pid=0;
    int status=-1;
    /* wait_pid requires a scheduled parent; boot PID 0 cannot collect a
       detached child. Report its actual exit status, not a failed boot wait. */
    if(!elf_spawn_pid("/usr/linux-tests/linux_sigreturn_concurrency_smoke",&pid) && pid &&
       process_wait_pid((int32_t)pid,&status,0,NULL)==(int)pid && status==0)
        debug_log_puts("smoke: concurrent sigreturn/syscall regression ok\n");
    else debug_log_puts("smoke: concurrent sigreturn/syscall regression FAILED\n");
    process_exit();
}

static void graphics_test_runner(void) {
    /* An ordinary kernel task provides a real parent/wait identity. PID 0
     * denotes an orphan to the process reaper, so bootstrap cannot own these
     * asynchronous children while a compositor service task keeps running. */
    for (unsigned cycle = 0u; cycle < 4u; ++cycle) {
        if (elf_load("/usr/linux-tests/linux_static_smoke") != 0 ||
            process_wait_pid(-1, NULL, VEX_PROCESS_WAIT_NOHANG, NULL) != -1) {
            debug_log_puts("native-load-reap-smoke: FAILED\n");
            process_exit();
        }
    }
    if (elf_load("/usr/linux-tests/no_such_native_load") != -1 ||
        process_wait_pid(-1, NULL, VEX_PROCESS_WAIT_NOHANG, NULL) != -1) {
        debug_log_puts("native-load-reap-smoke: missing image FAILED\n");
        process_exit();
    }
    debug_log_puts("native-load-reap-smoke: four exits collected, missing image rejected\n");
    graphics_test_child("/usr/linux-tests/linux_fault_signal_smoke","fault-signal-smoke");
    graphics_test_child("/usr/linux-tests/linux_sigreturn_concurrency_smoke","sigreturn-smoke");
    if(graphics_run_gl)graphics_test_child("/usr/linux-tests/egl_gles_test","gl-smoke");
    graphics_test_child("/usr/linux-tests/linux_kill_group_smoke","kill-group-smoke");
    graphics_test_child("/home/root/bin/vk_test","vk-smoke");
    debug_log_puts("graphics-smoke: suite completed\n");
    process_exit();
}

static int module_goes_to_bin(const char *name) {
    return string_equals(name, "hello")
        || string_equals(name, "alloc_stress")
        || string_equals(name, "elevate_test")
        || string_equals(name, "attack_test")
        || string_equals(name, "security_boundary_test")
        || string_equals(name, "vex_event_fault_test")
        || string_equals(name, "attack_kill_test")
        || string_equals(name, "attack_kill_payload")
        || string_equals(name, "net_test")
        || string_equals(name, "gpu_test")
        || string_equals(name, "vk_test")
        || string_equals(name, "mouse_test")
        || string_equals(name, "app_window_demo")
        || string_equals(name, "gfm")
        || string_equals(name, "vex_editor")
        || string_equals(name, "vex_paint")
        || string_equals(name, "vex_launcher")
        || string_equals(name, "vex_terminal")
        || string_equals(name, "webkit")
        || string_equals(name, "webkit_linux_main")
        || string_equals(name, "WebKitWebProcess")
        || string_equals(name, "WebKitNetworkProcess")
        || string_equals(name, "WPEWebDriver")
        || string_equals(name, "jsc");
}

static int module_is_linux_interpreter(const char *name) {
    return string_equals(name, "ld-linux-x86-64.so.2");
}

static int path_contains_separator(const char *path) {
    size_t i = 0;

    if (path == NULL) {
        return 0;
    }

    while (path[i] != '\0') {
        if (path[i] == '/') {
            return 1;
        }
        ++i;
    }

    return 0;
}

static const char *module_virtual_path(const char *module_path) {
    static const char prefix[] = "boot():/";
    size_t i = 0;

    if (module_path == NULL) {
        return NULL;
    }

    /* Limine v8+ may strip the "boot():/" scheme and store just the
     * absolute path (e.g. "/lib/foo.so").  Handle both formats. */
    if (module_path[0] == '/') {
        return module_path + 1;   /* skip leading '/', return "lib/foo.so" */
    }

    while (prefix[i] != '\0') {
        if (module_path[i] != prefix[i]) {
            return NULL;
        }
        ++i;
    }

    return module_path + i;
}

static int boot_module_present(const char *name) {
    if (module_request.response == NULL || name == NULL) {
        return 0;
    }

    for (uint64_t i = 0; i < module_request.response->module_count; ++i) {
        const struct limine_file *module = module_request.response->modules[i];
        if (module == NULL || module->path == NULL) {
            continue;
        }

        if (string_equals(path_basename(module->path), name)) {
            return 1;
        }
    }

    return 0;
}

/* Bounded diagnostics for the isolated browser run only. Uses the existing
 * scheduler inventory and saved syscall frame; never probes user memory. */
static void webkit_run_observer(void){
    for(unsigned sample=0;sample<4;++sample){
        process_sleep_ticks(1000);scheduler_dump_cpu_distribution(0u);
        debug_log_puts("webkit-run: thread snapshot\n");
        for(uint64_t index=0;index<scheduler_process_count();++index){
            scheduler_process_info_t info;uint64_t saved[18]={0};uint32_t owner=0;uint64_t fs=0,sampled_rip=0;int framed=0;
            irq_disable();
            if(scheduler_process_info_at(index,&info)!=0){irq_enable();continue;}
            SCHEDULER_TASK_REF(task_ref);
            process_t *thread=scheduler_task_acquire(&task_ref,info.pid,0);
            sampled_rip=irq_sampled_user_rip(info.pid);
            if(thread&&info.user_mode){
                owner=thread->group?thread->group->process_id:thread->pid;fs=thread->fs_base;
                uint64_t base=(uint64_t)(uintptr_t)thread->stack,frame=thread->syscall_frame_rsp;
                if((thread->in_user_syscall||thread->state==PROC_WAITING||thread->state==PROC_SLEEPING)&&frame>=base&&thread->kernel_stack_size>=sizeof(saved)&&frame-base<=thread->kernel_stack_size-sizeof(saved)){
                    for(unsigned word=0;word<18;++word)saved[word]=((const uint64_t*)(uintptr_t)frame)[word];framed=1;
                }
            }
            irq_enable();if(!info.user_mode)continue;
            debug_log_puts("webkit-run: pid=");boot_log_u64_dec(owner);debug_log_puts(" tid=");boot_log_u64_dec(info.pid);
            debug_log_puts(" state=");debug_log_puts(scheduler_state_name_value(info.state));debug_log_puts(" fs=");boot_log_u64_dec(fs);
            if(sampled_rip){debug_log_puts(" sampled_user_rip=");boot_log_u64_dec(sampled_rip);}
            if(framed){debug_log_puts(" syscall=");boot_log_u64_dec(saved[2]);debug_log_puts(" rip=");boot_log_u64_dec(saved[16]);debug_log_puts(" arg1=");boot_log_u64_dec(saved[10]);debug_log_puts(" arg2=");boot_log_u64_dec(saved[11]);}
            debug_log_puts("\n");
        }
    }
    process_exit();
}

static void launch_stress_processes(void) {
    debug_log_puts("stress: autorun queued\n");
    (void)elf_spawn("/home/root/bin/gfm");
    (void)elf_spawn("/home/root/bin/vex_editor");
    (void)elf_spawn("/home/root/bin/vex_paint");
    (void)elf_spawn("/home/root/bin/app_window_demo");
    (void)elf_spawn("/home/root/bin/alloc_stress");
    (void)elf_spawn("/home/root/bin/gpu_test");
    (void)elf_spawn("/home/root/bin/vk_test");
    (void)elf_spawn("/usr/linux-tests/linux_static_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_static_pie_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_clone_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_thread_context_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_futex_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_file_mmap_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_wait4_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_pipe_exec_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_memory_ownership_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_fs_ipc_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_exec_args_smoke");
    (void)elf_spawn("/usr/linux-tests/linux_unsupported_smoke");
    (void)pe_spawn("/usr/pe-tests/pe_probe.exe");
}

static void import_limine_modules(void) {
    if (module_request.response == NULL) {
        return;
    }

    for (uint64_t i = 0; i < module_request.response->module_count; ++i) {
        const struct limine_file *module = module_request.response->modules[i];
        char destination[128];
        unsigned long length = 0;

        if (module == NULL || module->address == NULL || module->size == 0) {
            continue;
        }

        const char *name = path_basename(module->path);
        if (string_equals(name,"system.boot")) {
            if (fs_register_boot_archive(module->address,(size_t)module->size) != 0) {
                boot_console_fail("boot archive validation/import failed");
            }
            continue;
        }
        const char *virtual_path = module_virtual_path(module->path);
        if (module_is_linux_interpreter(name)) {
            append_string(destination, sizeof(destination), &length, "/lib64/ld-linux-x86-64.so.2");
        } else if (virtual_path != NULL && virtual_path[0] != '\0' && path_contains_separator(virtual_path)) {
            append_string(destination, sizeof(destination), &length, "/");
            append_string(destination, sizeof(destination), &length, virtual_path);
        } else {
            append_string(destination, sizeof(destination), &length, module_goes_to_bin(name) ? "/home/root/bin/" : "/home/root/packages/imports/");
            append_string(destination, sizeof(destination), &length, name);
        }
        if (((module_is_linux_interpreter(name) ||
                (virtual_path != NULL && virtual_path[0] != '\0' && path_contains_separator(virtual_path))) &&
                fs_register_boot_module(destination, module->address, (size_t)module->size) != 0) ||
            ((!module_is_linux_interpreter(name) &&
                (virtual_path == NULL || virtual_path[0] == '\0' || !path_contains_separator(virtual_path))) &&
                (((size_t)module->size > (1u << 20u) && fs_register_boot_module(destination, module->address, (size_t)module->size) != 0) ||
                 ((size_t)module->size <= (1u << 20u) && fs_write(destination, module->address, (size_t)module->size) != 0)))) {
            debug_log_puts("boot: module import skipped ");
            debug_log_puts(destination);
            debug_log_puts("\n");
        }
    }
}

void kmain(void) {
    uint64_t step;
    const int auto_vk_test = boot_module_present("auto_vk_test");
    const int auto_gl_test = boot_module_present("auto_gl_test");
    const int auto_webkit = boot_module_present("auto_webkit");
    const int auto_smoke = boot_module_present("auto_smoke");
    const int auto_stress = boot_module_present("auto_stress");

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        halt_forever();
    }

    if (framebuffer_request.response == NULL ||
        framebuffer_request.response->framebuffer_count == 0) {
        halt_forever();
    }

    if (memmap_request.response == NULL) {
        halt_forever();
    }

    boot_console_init();
    boot_console_stage("Limine -> kernel",1);
    boot_console_stage("CPU/TSC bootstrap",0);
    boot_time_init();
    cpu_enable_sse();
    boot_console_stage("CPU/TSC bootstrap",1);
    /* Guards read CPU-local identity through GS, including during boot. */
    boot_console_stage("GDT / GS / TSS",0);
    gdt_init();
    irq_prepare_idt();
    boot_console_stage("GDT / GS / TSS / early IDT",1);
    step = boot_step_begin();
    boot_console_stage("PMM bitmap / memory map",0);
    pmm_init(memmap_request.response);
    if (!pmm_free_page_count()) boot_console_fail("PMM has no allocatable pages");
    boot_console_stage("PMM bitmap / memory map",1);
    (void)boot_step_elapsed(step);

    tty_set_stream_output_enabled(0);
    debug_log_init();
    debug_log_puts("boot: serial log online\n");

    step = boot_step_begin();
    boot_console_stage("VMM bootstrap",0);
    vmm_init();
    boot_console_stage("VMM bootstrap",1);
    (void)boot_step_elapsed(step);

    step = boot_step_begin();
    boot_console_stage("Heap",0);
    heap_init();
    void *boot_heap_probe=kmalloc(1u);
    if(!boot_heap_probe)boot_console_fail("Heap initialization did not produce a usable allocator");
    kfree(boot_heap_probe);
    boot_console_stage("Heap",1);
    (void)boot_step_elapsed(step);

    step = boot_step_begin();
    boot_console_stage("Native allocator / ORA storage foundation",0);
    kernel_allocator_init();
    boot_console_stage("Native allocator / ORA storage foundation",1);
    boot_console_stage("SMP topology / per-CPU resources",0);
    if (smp_prepare()!=0) boot_console_fail("SMP topology/per-CPU allocation failed");
    boot_console_stage("SMP topology / per-CPU resources",1);
    boot_console_stage("FlowSched / FPU",0);
    scheduler_init();
    boot_console_stage("FlowSched / FPU",1);
    boot_console_stage("Users",0);
    users_init();
    boot_console_stage("Users",1);
    boot_console_stage("Capabilities",0);
    elevation_init();
    boot_console_stage("Capabilities",1);
    boot_console_stage("SYSCALL",0);
    syscall_init();
    boot_console_stage("SYSCALL",1);
    boot_console_stage("RTC",0);
    rtc_init();
    boot_console_stage("RTC",1);
    boot_console_stage("PS2 keyboard",0);
    keyboard_init();
    boot_console_stage("PS2 keyboard",1);
    boot_console_stage("PS2 mouse/touchpad",0);
    mouse_init();
    boot_console_stage("PS2 mouse/touchpad",1);
    boot_console_stage("HDA register probe",0);
    voice_init();
    boot_console_stage("HDA register probe",1);
    boot_console_stage("PIC / APIC / AP startup / clock",0);
    irq_init();
    boot_console_stage("PIC / APIC / AP startup / clock",1);
    boot_console_stage("AP timers",0);
    irq_start_ap_timers();
    boot_console_stage("AP timers",1);
    boot_console_stage("RNG provider / DRBG seed",0);
    if (kernel_random_init() != 0) {
        boot_console_fail("no healthy entropy provider: RNG initialization failed");
    }
    (void)boot_step_elapsed(step);

    boot_console_stage("Core memory/CPU/timers/RNG initialized",1);
    boot_console_stage("ACPI tables / AML power backend",0);
    power_start();
    boot_console_stage("ACPI power initialization attempted",1);
    step = boot_step_begin();
    boot_console_stage("Storage",0);
    disk_init();
    boot_console_stage("Storage",1);
    (void)boot_step_elapsed(step);


    step = boot_step_begin();
    boot_console_stage("VexFS (non-destructive)",0);
    fs_init();
    boot_console_stage("VexFS (non-destructive)",1);
    boot_console_stage("Boot modules / archive / ORA ingestion",0);
    import_limine_modules();
    boot_console_stage("Boot modules / archive / ORA ingestion",1);
    boot_console_stage("Optional ISO filesystem",0);
    iso9660_init();
    boot_console_stage("Optional ISO filesystem",1);
    boot_console_stage("Timezone",0);
    timezone_init();
    boot_console_stage("Timezone",1);
    /* The system palette: restored from /etc/kyil/theme.conf, republished
       there for the applications. */
    ui_theme_init();
    boot_console_stage("Spotlight",0);
    spotlight_index_build();
    boot_console_stage("Spotlight",1);
    (void)boot_step_elapsed(step);


    step = boot_step_begin();
    boot_console_stage("Driver Manager",0);
    driver_manager_init();
    boot_console_stage("Driver Manager",1);
    boot_console_stage("Network device",0);
    netdev_init();
    boot_console_stage("Network device",1);
    boot_console_stage("Carrier",0);
    wait_for_net_link();
    boot_console_stage("Carrier",1);
    ethernet_init();
    arp_init();
    ipv4_init();
    udp_init();
    dhcp_init();
    (void)boot_step_elapsed(step);

    step = boot_step_begin();
    dhcp_start();
    ethernet_service_start();
    (void)boot_step_elapsed(step);


    step = boot_step_begin();
    boot_console_stage("IPC",0);
    ipc_init();
    boot_console_stage("IPC",1);
    boot_console_stage("Instruction services",0);
    inst_init();
    boot_console_stage("Instruction services",1);
    (void)boot_step_elapsed(step);

    boot_console_stage("Starting native desktop / service tasks",0);
    boot_console_finish();
    tty_init();
    if (auto_vk_test || auto_gl_test) {
        step = boot_step_begin();
        gpu_init(framebuffer_request.response->framebuffers[0]);
        compositor_surface_system_init();
        (void)gui_compositor_init_headless(
            framebuffer_request.response->framebuffers[0]);
        (void)boot_step_elapsed(step);
        if (compositor_surface_self_test() == 0) {
            debug_log_puts("vk-smoke: Surface capability/backing/IPC lifetime ok\n");
        } else {
            debug_log_puts("vk-smoke: Surface capability/backing/IPC lifetime failed\n");
        }
        /* The asynchronous WSI is consumed by the compositor loop. The
           targeted test boot initializes its headless target here, so start
           the same service loop used by the normal desktop before waiting on
           a process that may queue presentation work. */
        uint32_t gui_pid = process_create_with_stack(gui_loop, GUI_PROCESS_PRIORITY,
                                                     64u * 1024u);
        if (gui_pid != 0u) {
            (void)scheduler_set_process_name(gui_pid, "gui-loop");
            (void)scheduler_allow_migration(gui_pid);
        }
        graphics_run_gl=auto_gl_test;
        (void)process_create_with_stack(graphics_test_runner,1u,64u*1024u);
    } else if (auto_webkit) {
        /* Isolated real browser workload. Regression suites run separately:
         * their concurrent file/process probes must not perturb its trace. */
        /* Input/viewport tests use the same native desktop/window geometry
         * and compositor as a normal launch. A headless target has no window. */
        gui_init();
        uint32_t browser_pid=0;
        debug_log_puts("webkit-run: launching native /webkit\n");
        if(elf_spawn_pid_caps("/webkit",&browser_pid,VEX_CAP_NET_CONNECT)!=0||browser_pid==0)
            debug_log_puts("webkit-run: launcher failed\n");
        (void)process_create_with_stack(webkit_run_observer,1u,64u*1024u);
    } else if (auto_smoke) {
        debug_log_puts(process_memory_inventory_test()==0 ? "smoke: ORA inventory splits/lazy/ownership ok\n" : "smoke: ORA inventory FAILED\n");
        debug_log_puts(process_memory_index_test()==0 ? "smoke: region index matches model ok\n" : "smoke: region index FAILED\n");
        debug_log_puts(ora_self_test() == 0 ? "smoke: ORA storage/views/lifetime/no-rehash ok\n" : "smoke: ORA FAILED\n");
        debug_log_puts(fs_ora_self_test() == 0 ? "smoke: FS ORA exact read leases/overlay/rebind ok\n" : "smoke: FS ORA FAILED\n");
        if(elf_spawn("/usr/linux-tests/ora_native_abi_test"))
            debug_log_puts("smoke: ORA native ABI test load FAILED\n");
        debug_log_puts(versioned_aligned_key_smoke() == 0
            ? "smoke: versioned store 2048 aligned keys/put/get/remove ok\n"
            : "smoke: versioned aligned keys FAILED\n");
        step = boot_step_begin();
        gpu_init(framebuffer_request.response->framebuffers[0]);
        compositor_surface_system_init();
        (void)gui_compositor_init_headless(
            framebuffer_request.response->framebuffers[0]);
        (void)boot_step_elapsed(step);
        if (tty_region_idempotence_self_test() == 0) {
            debug_log_puts("smoke: repeated TTY region preserves cells/cursor ok\n");
        } else {
            debug_log_puts("smoke: repeated TTY region preserves cells/cursor failed\n");
        }
        if (compositor_surface_self_test() == 0) {
            debug_log_puts("smoke: Surface capability lifetime/typed IPC ok\n");
        } else {
            debug_log_puts("smoke: Surface capability lifetime/typed IPC failed\n");
        }
        debug_log_puts("smoke: autorun queued\n");
        /* Run the boundary test before the other smoke workloads consume the
           available physical pages.  It is still an ordinary user process;
           this only makes the negative tests deterministic under low memory. */
        {
            uint32_t security_pid = 0u;
            if (elf_spawn_pid("/home/root/bin/security_boundary_test", &security_pid) == 0 &&
                security_pid != 0u) {
                process_yield_to(security_pid);
            }
        }
        {
            uint32_t event_fault_pid = 0u;
            int event_fault_status = -1;
            uint32_t event_fault_reaped = 0u;
            if (elf_spawn_pid("/home/root/bin/vex_event_fault_test",
                              &event_fault_pid) == 0 && event_fault_pid != 0u &&
                process_wait_pid((int32_t)event_fault_pid, &event_fault_status,
                    0u, &event_fault_reaped) == (int)event_fault_pid &&
                event_fault_status == 0) {
                debug_log_puts("smoke: native synchronous fault-event ok\n");
            } else {
                debug_log_puts("smoke: native sync fault-event failed status=");
                boot_log_u64_dec((uint64_t)(unsigned int)event_fault_status);
                debug_log_puts("\n");
            }
        }
        {
            uint32_t fault_signal_pid = 0u;
            int fault_signal_status = -1;
            uint32_t fault_signal_reaped = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_fault_signal_smoke",
                              &fault_signal_pid) == 0 && fault_signal_pid != 0u &&
                process_wait_pid((int32_t)fault_signal_pid, &fault_signal_status,
                    0u, &fault_signal_reaped) == (int)fault_signal_pid &&
                fault_signal_status == 0) {
                debug_log_puts("smoke: Linux synchronous fault signals ok\n");
            } else {
                debug_log_puts("smoke: Linux synchronous fault signals failed status=");
                boot_log_u64_dec((uint64_t)(unsigned int)fault_signal_status);
                debug_log_puts(" reaped=");
                boot_log_u64_dec(fault_signal_reaped);
                debug_log_puts(" wait=");
                boot_log_u64_dec((uint64_t)(unsigned int)fault_signal_pid);
                debug_log_puts("\n");
            }
        }
        {
            uint32_t jit_alias_pid = 0u;
            int jit_alias_status = -1;
            uint32_t jit_alias_reaped = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_jit_alias_test", &jit_alias_pid) == 0 &&
                jit_alias_pid && process_wait_pid((int32_t)jit_alias_pid, &jit_alias_status,
                    0u, &jit_alias_reaped) == (int)jit_alias_pid && !jit_alias_status)
                debug_log_puts("smoke: Linux native dual-alias JIT lifecycle ok\n");
            else
                debug_log_puts("smoke: Linux native dual-alias JIT lifecycle failed\n");
        }
        {
            uint32_t jit_wx_pid = 0u;
            int jit_wx_status = -1;
            uint32_t jit_wx_reaped = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_jit_wx_smoke",
                              &jit_wx_pid) == 0 && jit_wx_pid != 0u &&
                process_wait_pid((int32_t)jit_wx_pid, &jit_wx_status,
                    0u, &jit_wx_reaped) == (int)jit_wx_pid &&
                jit_wx_status == 0) {
                debug_log_puts("smoke: Linux strict W^X JIT mapping ok\n");
            } else {
                debug_log_puts("smoke: Linux strict W^X JIT mapping failed status=");
                boot_log_u64_dec((uint64_t)(unsigned int)jit_wx_status);
                debug_log_puts(" reaped=");
                boot_log_u64_dec(jit_wx_reaped);
                debug_log_puts(" wait=");
                boot_log_u64_dec((uint64_t)(unsigned int)jit_wx_pid);
                debug_log_puts("\n");
            }
        }
        if (fs_exists("/usr/linux-tests/jsc")) {
            uint32_t pid = 0u, reaped = 0u;
            int status = -1;
            if (!elf_spawn_pid("/usr/linux-tests/linux_jsc_jit_test", &pid) && pid &&
                process_wait_pid((int32_t)pid, &status, 0u, &reaped) == (int)pid && !status)
                debug_log_puts("smoke: real JSC Baseline/DFG/FTL on Vex dual-alias JIT ok\n");
            else debug_log_puts("smoke: real JSC Baseline/DFG/FTL on Vex dual-alias JIT failed\n");
        } else debug_log_puts("smoke: JSC engine test not staged\n");
        {
            uint32_t fault_unhandled_pid = 0u;
            int fault_unhandled_status = -1;
            uint32_t fault_unhandled_reaped = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_fault_unhandled_smoke",
                              &fault_unhandled_pid) == 0 && fault_unhandled_pid != 0u &&
                process_wait_pid((int32_t)fault_unhandled_pid,
                    &fault_unhandled_status, 0u, &fault_unhandled_reaped) ==
                    (int)fault_unhandled_pid && fault_unhandled_status == 139) {
                debug_log_puts("smoke: unhandled synchronous fault fatal cleanup ok\n");
            } else {
                debug_log_puts("smoke: unhandled synchronous fault fatal cleanup failed\n");
            }
        }
        {
            uint32_t abort_group_pid = 0u;
            int abort_group_status = -1;
            uint32_t abort_group_reaped = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_abort_group_smoke",
                              &abort_group_pid) == 0 && abort_group_pid != 0u &&
                process_wait_pid((int32_t)abort_group_pid, &abort_group_status,
                    0u, &abort_group_reaped) == (int)abort_group_pid &&
                abort_group_status == 128 + 6) {
                debug_log_puts("smoke: Linux SIGABRT process-group cleanup ok\n");
            } else {
                debug_log_puts("smoke: Linux SIGABRT process-group cleanup failed status=");
                boot_log_u64_dec((uint64_t)(unsigned int)abort_group_status);
                debug_log_puts(" reaped=");
                boot_log_u64_dec(abort_group_reaped);
                debug_log_puts("\n");
            }
        }
        {
            uint32_t kill_pid = 0u, reaped = 0u;
            int status = -1;
            if (elf_spawn_pid("/usr/linux-tests/linux_kill_group_smoke", &kill_pid) == 0 &&
                kill_pid != 0u && process_wait_pid((int32_t)kill_pid, &status, 0u, &reaped) ==
                    (int)kill_pid && status == 0) {
                debug_log_puts("smoke: Linux SIGKILL all-thread lifetime and EOF ok\n");
            } else debug_log_puts("smoke: Linux SIGKILL all-thread lifetime and EOF failed\n");
        }
        {
            static const char *const paths[] = {
                "/usr/linux-tests/linux_static_smoke",
                "/usr/linux-tests/linux_static_pie_smoke"
            };
            static const char *const markers[] = {
                "smoke: Linux static ELF passed\n",
                "smoke: Linux static-PIE ELF passed\n"
            };
            /* The assembly probes deliberately exit with 7 and 9 on success.
               Run them before long-lived workloads and check those contracts
               directly instead of relying on stdout ordering in the UART log. */
            for (size_t index = 0u; index < sizeof(paths) / sizeof(paths[0]); ++index) {
                const int expected_status = index == 0u ? 7 : 9;
                uint32_t pid = 0u;
                int status = -1;
                uint32_t reaped = 0u;
                if (elf_spawn_pid(paths[index], &pid) == 0 && pid != 0u &&
                    process_wait_pid((int32_t)pid, &status, 0u, &reaped) ==
                        (int)pid && status == expected_status) {
                    debug_log_puts(markers[index]);
                } else {
                    debug_log_puts("smoke: Linux static ELF failed status=");
                    boot_log_u64_dec((uint64_t)(unsigned int)status);
                    debug_log_puts("\n");
                }
            }
        }
        {
            uint32_t pid=0,reaped=0;int status=-1;
            if(!elf_spawn_pid("/usr/linux-tests/linux_browser_system_smoke",&pid) && pid &&
               process_wait_pid((int32_t)pid,&status,0,&reaped)==(int)pid && !status)
                debug_log_puts("smoke: browser UTC/stat/lazy-user-pointer/NET_CONNECT isolation ok\n");
            else debug_log_puts("smoke: browser system ABI regression failed\n");
        }
        {
            uint32_t pid=0,reaped=0;int status=-1;
            if(!elf_spawn_pid("/usr/linux-tests/linux_fd_inventory_smoke",&pid) && pid &&
               process_wait_pid((int32_t)pid,&status,0,&reaped)==(int)pid && !status)
                debug_log_puts("smoke: process FD isolation/four children/glibc spawn ok\n");
            else {debug_log_puts("smoke: process FD inventory regression failed status=");boot_log_u64_dec((uint64_t)(unsigned)status);debug_log_puts("\n");}
        }
        {
            uint32_t unsupported_pid = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_unsupported_smoke", &unsupported_pid) == 0 &&
                unsupported_pid != 0u) {
                process_yield_to(unsupported_pid);
            }
        }
        {
            uint32_t seqpacket_pid = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_seqpacket_smoke", &seqpacket_pid) == 0 &&
                seqpacket_pid != 0u) {
                process_yield_to(seqpacket_pid);
            }
        }
        {
            uint32_t seqpacket_fdpass_pid = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_seqpacket_fdpass_smoke",
                              &seqpacket_fdpass_pid) == 0 &&
                seqpacket_fdpass_pid != 0u) {
                process_yield_to(seqpacket_fdpass_pid);
            }
        }
        {
            uint32_t int3_pid = 0u;
            debug_log_puts("smoke: probing ring-3 int3 gate\n");
            if (elf_spawn_pid("/usr/linux-tests/linux_int3_smoke", &int3_pid) == 0 &&
                int3_pid != 0u) {
                process_yield_to(int3_pid);
            }
        }
        {
            uint32_t exec_args_pid = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_exec_args_smoke", &exec_args_pid) == 0 &&
                exec_args_pid != 0u) {
                process_yield_to(exec_args_pid);
            }
        }
        {
            uint32_t dynamic_pid = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_dynamic_smoke", &dynamic_pid) == 0 &&
                dynamic_pid != 0u) {
                process_yield_to(dynamic_pid);
            }
        }
        {
            uint32_t dynamic_tls_pid = 0u;
            if (elf_spawn_pid("/usr/linux-tests/linux_dynamic_tls_smoke", &dynamic_tls_pid) == 0 &&
                dynamic_tls_pid != 0u) {
                process_yield_to(dynamic_tls_pid);
            }
        }
        {
            uint32_t parent_lifetime_pid = 0u;
            debug_log_puts("smoke: probing process parent lifetime across thread exit\n");
            if (elf_spawn_pid("/usr/linux-tests/linux_parent_lifetime_smoke",
                              &parent_lifetime_pid) == 0 && parent_lifetime_pid != 0u) {
                int parent_lifetime_status = 0;
                uint32_t parent_lifetime_reaped = 0u;
                if (process_wait_pid((int32_t)parent_lifetime_pid,
                                     &parent_lifetime_status, 0u,
                                     &parent_lifetime_reaped) == (int)parent_lifetime_pid &&
                    parent_lifetime_status == 0) {
                    debug_log_puts("smoke: process parent lifetime ok\n");
                } else {
                    debug_log_puts("smoke: process parent lifetime failed\n");
                }
            } else {
                debug_log_puts("smoke: process parent lifetime did not load\n");
            }
        }
        {
            uint32_t pipe_exec_pid = 0u;
            debug_log_puts("smoke: probing pipe/exec spawn contract\n");
            if (elf_spawn_pid("/usr/linux-tests/linux_pipe_exec_smoke", &pipe_exec_pid) == 0 &&
                pipe_exec_pid != 0u) {
                int pipe_exec_status = 0;
                uint32_t pipe_exec_reaped = 0u;
                if (process_wait_pid((int32_t)pipe_exec_pid, &pipe_exec_status, 0u,
                                     &pipe_exec_reaped) == (int)pipe_exec_pid &&
                    pipe_exec_status == 0) {
                    debug_log_puts("smoke: pipe/exec spawn contract ok\n");
                } else {
                    debug_log_puts("smoke: pipe/exec spawn contract failed\n");
                }
            } else {
                debug_log_puts("smoke: pipe/exec probe did not load\n");
            }
        }
        {
            uint32_t glibc_loader_pid = 0u;
            debug_log_puts("smoke: probing ld-linux without libc\n");
            if (elf_spawn_pid("/usr/linux-tests/glibc_loader_probe", &glibc_loader_pid) == 0 && glibc_loader_pid != 0u) {
                process_yield_to(glibc_loader_pid);
            } else {
                debug_log_puts("smoke: ld-linux-only probe did not load\n");
            }
        }
        {
            uint32_t glibc_pid = 0u;
            debug_log_puts("smoke: probing real glibc probe\n");
            if (elf_spawn_pid("/usr/linux-tests/glibc_probe", &glibc_pid) == 0 && glibc_pid != 0u) {
                process_yield_to(glibc_pid);
            } else {
                debug_log_puts("smoke: real glibc probe did not load\n");
            }
        }
        {
            uint32_t glibc_pid = 0u;
            debug_log_puts("smoke: launching native /webkit launcher\n");
            if (elf_spawn_pid_caps("/webkit", &glibc_pid,VEX_CAP_NET_CONNECT) == 0 && glibc_pid != 0u) {
                /* WebKit is a long-lived workload; keep finite smoke probes
                 * schedulable instead of waiting for its main loop here. */
                debug_log_puts("smoke: native /webkit launcher started\n");
            } else {
                debug_log_puts("smoke: native /webkit launcher did not load\n");
            }
        }
        if(!process_create_with_stack(sigreturn_test_runner,1u,64u*1024u))
            debug_log_puts("smoke: concurrent sigreturn runner creation FAILED\n");
        (void)elf_spawn("/home/root/bin/gpu_test");
        (void)elf_spawn("/home/root/bin/vk_test");
        /* vk_test's syscall runner owns/waits these finite probes as an actual
         * process parent. PID 0 children are detached and may be reaped by
         * other scheduler activity before a kernel boot wait collects them. */
        (void)elf_spawn("/usr/linux-tests/linux_clone_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_thread_context_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_futex_smoke");
        /* vk_test owns this probe and waits for its result. Running a second
           copy concurrently truncates its identical file during validation. */
        (void)elf_spawn("/usr/linux-tests/linux_wait4_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_memory_ownership_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_fs_ipc_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_exec_args_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_unsupported_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_seqpacket_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_seqpacket_fdpass_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_int3_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_dynamic_smoke");
        (void)elf_spawn("/usr/linux-tests/linux_dynamic_tls_smoke");
        (void)pe_spawn("/usr/pe-tests/pe_probe.exe");
    } else {

        step = boot_step_begin();
        gui_init();
        (void)boot_step_elapsed(step);

        step = boot_step_begin();
        tty_set_stream_output_enabled(1);
        shell_init();
        gui_set_terminal_pid(shell_pid());
        (void)boot_step_elapsed(step);

        if (auto_stress) {
            launch_stress_processes();
        }
    }

    if (auto_stress || auto_smoke) {
        uint32_t gui_pid = process_create_with_stack(gui_loop, GUI_PROCESS_PRIORITY, 64u * 1024u);
        if(gui_pid){(void)scheduler_set_process_name(gui_pid,"gui-loop");(void)scheduler_allow_migration(gui_pid);}
    } else if (!auto_smoke && !auto_vk_test && !auto_gl_test) {
        uint32_t gui_pid = process_create_with_stack(gui_loop, GUI_PROCESS_PRIORITY, 64u * 1024u);
        if(gui_pid){(void)scheduler_set_process_name(gui_pid,"gui-loop");(void)scheduler_allow_migration(gui_pid);}
    }
    security_watchdogs_start();
    if (smp_start()!=0) {debug_log_puts("smp: AP startup failed\n");halt_forever();}
    if(boot_module_present("auto_smp_test"))
        debug_log_puts(smp_self_test()==0?"smp-platform: PASS\n":"smp-platform: FAILED\n");
    scheduler_enable_smp_dispatch();
    if(boot_module_present("auto_smp_test"))
        if(!process_create_with_stack(smp_wait_race_test,1u,64u*1024u))
            debug_log_puts("smp-wait: FAILED to create waiter\n");
    if(boot_module_present("auto_flow_test"))
        (void)process_create_with_stack(flow_test_runner,1u,64u*1024u);
    if(boot_module_present("auto_native_test"))
        (void)process_create_with_stack(native_test_task,1u,64u*1024u);
    irq_enable();
    for (;;) {
        schedule();
        scheduler_idle_wait();
    }
}
