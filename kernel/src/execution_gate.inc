/* Included in scheduler.c: one metadata transaction with scheduler state,
 * including remote wake/stop. No second scheduler or global execution lock.
 * The FLOW_LOCK protects only metadata. Alloc/free occur outside it. */
typedef struct execution_gate {
    struct execution_gate *next;
    process_t *waiting;
    uint64_t token;
    uint32_t owner,capacity,active,refs;
    uint8_t closed;
} execution_gate_t;
static execution_gate_t *execution_gates,*execution_retired;
static int execution_runnable(process_t *p) {return p->state==PROC_READY || p->state==PROC_RUNNING;}
static execution_gate_t *execution_find(uint64_t token,uint32_t owner) {
    for(execution_gate_t *g=execution_gates;g;g=g->next)
        if(g->token==token && g->owner==owner && !g->closed)return g;
    return NULL;
}
static void execution_release(execution_gate_t *g) {
    if(--g->refs)return;
    execution_gate_t **p=&execution_gates;while(*p!=g)p=&(*p)->next;
    *p=g->next;g->next=execution_retired;execution_retired=g;
}
static void execution_unwait(process_t *p) {
    if(!p->execution_pending)return;
    process_t **link=&p->execution_gate->waiting;
    while(*link && *link!=p)link=&(*link)->execution_wait_next;
    if(*link)*link=p->execution_wait_next;
    p->execution_wait_next=NULL;p->execution_pending=0;
}
static void execution_admit(execution_gate_t *g) {
    while(!g->closed && g->active<g->capacity && g->waiting) {
        process_t *p=g->waiting;execution_unwait(p);
        /* Reserve before waking: concurrent admissions cannot consume the same
         * slot while this context is still SLEEPING/WAITING on a remote CPU. */
        p->execution_admitted=1;p->execution_counted=1;g->active++;
        __atomic_store_n(&p->execution_woken,1,__ATOMIC_RELEASE);
        process_wake(p->pid);
    }
}
static void execution_detach(process_t *p) {
    execution_gate_t *g=p->execution_gate;if(!g)return;
    execution_unwait(p);
    if(p->execution_counted)g->active--;
    p->execution_gate=NULL;p->execution_admitted=0;p->execution_counted=0;
    execution_admit(g);execution_release(g);
}
static void execution_transition(process_t *p) {
    execution_gate_t *g=p->execution_gate;if(!g)return;
    if(p->state==PROC_ZOMBIE){execution_detach(p);return;}
    if(!p->execution_admitted)return;
    unsigned active=execution_runnable(p);
    if(active==p->execution_counted)return;
    p->execution_counted=(uint8_t)active;
    if(active)g->active++; /* Reactivation can temporarily exceed capacity. */
    else {g->active--;execution_admit(g);}
}
static void execution_collect(void) {
    execution_gate_t *retired;
    {FLOW_LOCK;retired=execution_retired;execution_retired=NULL;}
    while(retired){execution_gate_t *next=retired->next;kfree(retired);retired=next;}
}
static void execution_close(execution_gate_t *g) {
    g->closed=1;
    while(g->waiting) {
        process_t *p=g->waiting;execution_unwait(p);
        __atomic_store_n(&p->execution_woken,1,__ATOMIC_RELEASE);process_wake(p->pid);
    }
    execution_release(g); /* Drop process-owned handle, not member references. */
}
static void execution_reap_owner(uint32_t owner) {
    {FLOW_LOCK;execution_gate_t *g=execution_gates;
     while(g){execution_gate_t *next=g->next;if(g->owner==owner && !g->closed)execution_close(g);g=next;}}
    execution_collect();
}
int64_t scheduler_execution_gate(unsigned op,unsigned capacity,uint64_t token,uint64_t timeout) {
    process_t *p=current_process;
    if(!p || !p->pid || !p->group)return -EINVAL;
    uint32_t owner=p->group->process_id;int64_t result=0;
    execution_collect();
    if(op==VEX_EXECUTION_CREATE) {
        if(token || timeout)return -EINVAL;
        execution_gate_t *g=kmalloc(sizeof(*g));if(!g)return -ENOMEM;
        memset(g,0,sizeof(*g));
        do {
            if(kernel_random_try_fill(&g->token,sizeof(g->token))){kfree(g);return -EIO;}
            g->token&=INT64_MAX;
        }while(!g->token);
        g->owner=owner;g->capacity=capacity?capacity:smp_cpu_count();g->refs=1;
        {FLOW_LOCK;
         /* Resolve the (extremely unlikely) opaque-token collision safely. */
         for(execution_gate_t *a=execution_gates;a;a=a->next)if(a->token==g->token){result=-EAGAIN;break;}
         if(!result){g->next=execution_gates;execution_gates=g;result=(int64_t)g->token;}}
        if(result<0)kfree(g);return result;
    }
    if(capacity || (op!=VEX_EXECUTION_ENTER && timeout))return -EINVAL;
    if(op==VEX_EXECUTION_LEAVE) {
        if(token)return -EINVAL;
        {FLOW_LOCK;execution_detach(p);}execution_collect();return 0;
    }
    uint64_t now=sched_clock_ns(),deadline=timeout>UINT64_MAX-now?UINT64_MAX:now+timeout;
    {FLOW_LOCK;
     execution_gate_t *g=execution_find(token,owner);if(!g)return -EBADF;
     if(op==VEX_EXECUTION_CLOSE)execution_close(g);
     else if(op==VEX_EXECUTION_QUERY)return g->active;
     else if(op==VEX_EXECUTION_CAPACITY)return g->capacity;
     else if(op==VEX_EXECUTION_WAITERS) {
        unsigned count=0;for(process_t *a=g->waiting;a;a=a->execution_wait_next)count++;return count;
     }
     else if(op==VEX_EXECUTION_ENTER || op==VEX_EXECUTION_RESUME) {
        if(p->execution_gate!=g) {
            if(g->refs==UINT32_MAX)return -ENOMEM;
            execution_detach(p);g->refs++;p->execution_gate=g;
        }
        if(op==VEX_EXECUTION_RESUME) {
            execution_unwait(p);p->execution_admitted=1;
            if(!p->execution_counted && execution_runnable(p)){p->execution_counted=1;g->active++;}
        }else if(!p->execution_admitted) {
            p->execution_woken=0;p->execution_pending=1;
            p->execution_wait_next=g->waiting;g->waiting=p;execution_admit(g);
        }
     }else return -EINVAL;}
    if(op!=VEX_EXECUTION_ENTER){execution_collect();return 0;}
    for(;;) {
        {FLOW_LOCK;execution_gate_t *g=p->execution_gate;
         if(g->closed){execution_detach(p);result=-ECANCELED;break;}
         if(p->execution_admitted)break;
         now=sched_clock_ns();
         if(timeout && now>=deadline){execution_unwait(p);result=-ETIMEDOUT;break;}
         if(p->event_pending&~p->event_mask){execution_unwait(p);result=-EINTR;break;}}
        futex_block(&p->execution_woken,timeout?deadline:0);
    }
    execution_collect();return result;
}
