/* Included by scheduler.c: uses the existing short FlowSched metadata gate,
 * task leases, CPU claim/unclaim and scheduler waits. No second context engine. */
static void context_notify(uint32_t tid) {
    if(!tid || !context_waiter_count)return;
    for(process_t *p=process_list;p;p=p->next)
        if(p->context_wait_tid==tid)process_wake(p->pid);
}
static void pause_boundary(const vex_event_user_context_t *context) {
    process_t *p=current_process;
    for(;;) {
        {
            FLOW_LOCK;
            if(!p || !p->pause_count || p->terminate_requested) {
                if(p)p->pause_parked=0;
                return;
            }
            p->paused_context=*context;p->pause_parked=1;
            scheduler_set_state(p,PROC_WAITING);
        }
        /* scheduler_running publishes the saved FPU state only AFTER this
           CPU has released its execution claim. A competing resume may race
           the park; count and runnable state share the same metadata lock. */
        schedule();
    }
}
void scheduler_user_return(uint64_t frame,uint64_t result) {
    /* Called after event delivery and after all syscall transactions ended.
       Keep IRQs masked through the final return (the assembly restores IF). */
    __asm__ volatile("cli":::"memory");
    int relocate=0;{FLOW_LOCK;if(current_process && current_process->pid)relocate=!flow_cpu_allowed(&current_process->scheduling,smp_current_index());}if(relocate)schedule();
    if(!current_process || !__atomic_load_n(&current_process->pause_count,__ATOMIC_ACQUIRE)){scheduler_execution_mode(1);return;}
    const uint64_t *f=(const void *)(uintptr_t)frame;
    vex_event_user_context_t c={.r15=f[3],.r14=f[4],.r13=f[5],.r12=f[6],
        .r11=f[15],.r10=f[7],.r9=f[8],.r8=f[9],.rdi=f[10],.rsi=f[11],
        .rbp=f[12],.rdx=f[13],.rcx=f[16],.rbx=f[14],.rax=result,
        .rip=f[2]?f[0]:f[16],.cs=0x2b,.rflags=f[2]?f[1]:f[15],.rsp=f[17],.ss=0x23};
    pause_boundary(&c);
    scheduler_execution_mode(1);
}
void scheduler_user_interrupt(struct interrupt_frame *f) {
    int relocate=0;{FLOW_LOCK;if(current_process && current_process->pid)relocate=!flow_cpu_allowed(&current_process->scheduling,smp_current_index());}if(relocate)schedule();
    if(!current_process || !__atomic_load_n(&current_process->pause_count,__ATOMIC_ACQUIRE))return;
    vex_event_user_context_t c;
    memcpy(&c,f,15u*sizeof(uint64_t));
    c.rip=f->rip;c.cs=f->cs;c.rflags=f->rflags;c.rsp=f->rsp;c.ss=f->ss;
    c.vector=c.error_code=c.fault_address=0;
    pause_boundary(&c);
}
static int thread_cpu_set(process_t *p,vex_thread_control_req_t *request) {
    vex_thread_cpu_set_t r;uintptr_t user=request->value;uint32_t n=smp_cpu_count();
    if(vex_copy_from_user(&r,user,sizeof(r)))return -EFAULT;
    if(r.size!=sizeof(r) || r.previous_count || r.count>n || r.capacity<n || !r.previous || (r.count && !r.cpus))return -EINVAL;
    VEX_SCRATCH(storage);uint32_t *old=vex_scratch_allocate(&storage,(size_t)n*2*sizeof(uint32_t));if(!old)return -ENOMEM;
    uint32_t *input=old+n;
    if(r.count && vex_copy_from_user(input,(uintptr_t)r.cpus,r.count*sizeof(uint32_t)))return -EFAULT;
    for(uint32_t i=0;i<r.count;i++)if(input[i]>=n || !smp_cpu_at(input[i])->online || !smp_cpu_at(input[i])->scheduler_enabled || (i && input[i]<=input[i-1]))return -EINVAL;
    VEX_SCRATCH(fresh_ref);uint32_t *fresh=NULL;
    if(r.count){fresh=vex_scratch_allocate(&fresh_ref,r.count*sizeof(uint32_t));if(!fresh)return -ENOMEM;memcpy(fresh,input,r.count*sizeof(uint32_t));}
    /* Pin the caller's output mappings while publishing the exchange. No wait
       or remote stop is performed under the memory/FlowSched transaction. */
    kernel_guard_t memory __attribute__((cleanup(process_memory_end_update)))=process_memory_begin_update();
    {
        FLOW_LOCK;
        if(p->state==PROC_ZOMBIE || p->terminate_requested)return -ESRCH;
        if(p->placement_explicit)return -EPERM; /* internal kernel binding */
        r.previous_count=0;
        for(uint32_t i=0;i<n;i++)if(flow_cpu_allowed(&p->scheduling,i))old[r.previous_count++]=i;
        if(vex_copy_to_user((uintptr_t)r.previous,old,r.previous_count*sizeof(uint32_t)) || vex_copy_to_user(user,&r,sizeof(r)))return -EFAULT;
        if(r.count){uint32_t *retired=p->scheduling.allowed_cpus;p->scheduling.allowed_cpus=fresh;p->scheduling.allowed_count=r.count;
            (void)vex_scratch_exchange(&fresh_ref,retired);
            if(p->scheduling.running_cpu!=UINT32_MAX)smp_reschedule(p->scheduling.running_cpu);
            for(uint32_t i=0;i<r.count;i++)smp_reschedule(input[i]);
        }
    }
    return 0;
}
int scheduler_thread_control(vex_thread_control_req_t *r) {
    if(r->size!=sizeof(*r) || r->reserved || r->operation>VEX_THREAD_CPU_SET)return -EINVAL;
    if(r->operation==VEX_THREAD_YIELD) {
        if(r->tid)return -EINVAL;
        uint64_t before=current_process->dispatch_count;
        process_yield();r->value=current_process->dispatch_count!=before;return 0;
    }
    SCHEDULER_TASK_REF(ref);
    process_t *p=scheduler_task_acquire(&ref,r->tid,0);
    if(!p || (!p->user_mode && !p->resume_syscall_rsp) || !current_process->group || p->group!=current_process->group)return -EPERM;
    if(r->operation==VEX_THREAD_CPU_SET)return thread_cpu_set(p,r);
    for(;;) {
        {
            FLOW_LOCK;
            if(p->state==PROC_ZOMBIE || p->terminate_requested)return -ESRCH;
            switch(r->operation) {
            case VEX_THREAD_LATENCY_CLASS:
            case VEX_THREAD_AUTOMATIC_FLOWS:
            case VEX_THREAD_EFFICIENCY: {
                flow_thread_t *t=&p->scheduling;int latency=t->latency_class,suppress=t->suppress_automatic,efficiency=t->efficiency;
                if(r->operation==VEX_THREAD_LATENCY_CLASS){if((int64_t)r->value< -4 || (int64_t)r->value>4)return -EINVAL;latency=(int)(int64_t)r->value;}
                else {if(r->value>1)return -EINVAL;if(r->operation==VEX_THREAD_AUTOMATIC_FLOWS)suppress=!r->value;else efficiency=(int)r->value;}
                flow_set_qos(t->cpu,t,latency,suppress,efficiency,sched_clock_ns());
                if(t->running_cpu!=UINT32_MAX)smp_reschedule(t->running_cpu);return 0;
            }
            case VEX_THREAD_QOS_QUERY:
                r->value=(uint8_t)p->scheduling.latency_class|((uint64_t)p->scheduling.suppress_automatic<<8)|((uint64_t)p->scheduling.efficiency<<9);return 0;
            case VEX_THREAD_PAUSE:
                if(r->value>UINT32_MAX)return -EINVAL;
                if(p->pause_count>=(r->value?r->value:UINT32_MAX))return -75; /* EOVERFLOW */
                r->value=p->pause_count++;
                if(p->scheduling.running_cpu!=UINT32_MAX)smp_reschedule(p->scheduling.running_cpu);
                return 0;
            case VEX_THREAD_RESUME:
                r->value=p->pause_count;
                if(p->pause_count && !--p->pause_count) {
                    if(p->pause_parked)process_wake(p->pid);
                    context_notify(p->pid);
                }
                return 0;
            case VEX_THREAD_CYCLES:
                scheduler_account(sched_clock_ns());r->value=p->cpu_cycles;return 0;
            case VEX_THREAD_SNAPSHOT:
                if(p==current_process || !p->pause_count)return -EAGAIN;
                if(p->pause_parked && p->scheduling.running_cpu==UINT32_MAX) {
                    r->value=p->pause_count;r->context=p->paused_context;
                    r->fs_base=p->fs_base;r->gs_base=p->user_gs_base;
                    memcpy(r->fxsave,p->fpu_state,sizeof(r->fxsave));
                    if(scheduler_xsave_mask) {
                        uint64_t present=*(uint64_t *)(p->fpu_state+512u);
                        if(!(present&1u)) {memset(r->fxsave,0,24);memset(r->fxsave+32,0,128);r->fxsave[0]=0x7f;r->fxsave[1]=3;}
                        if(!(present&2u)) {memset(r->fxsave+160,0,256);*(uint32_t *)(r->fxsave+24)=0x1f80u;}
                    }
                    return 0;
                }
                /* We cannot invent the eventual result/registers of a
                   still-blocked syscall. Report this boundary explicitly. */
                if((p->state==PROC_WAITING || p->state==PROC_SLEEPING) && !p->pause_parked)return -ENOSYS;
                current_process->context_wait_tid=p->pid;
                ++context_waiter_count;
                scheduler_set_state(current_process,PROC_WAITING);break;
            }
        }
        schedule();
        {FLOW_LOCK;current_process->context_wait_tid=0;--context_waiter_count;}
    }
}
