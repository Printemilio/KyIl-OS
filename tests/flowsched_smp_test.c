#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unos/flowsched.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FlowSched SMP FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)

/* Independent scheduling simulation: all CPUs select before any is charged.
 * Canonical group service and local indexes must agree even as threads migrate.
 * This verifies the policy core; it does not certify kernel context switching. */
static void run(unsigned count) {
    flow_cpu_t *cpus=calloc(count,sizeof(*cpus));
    flow_cpu_t **index=calloc(count,sizeof(*index));
    flow_group_view_t *views=calloc(count*2,sizeof(*views));
    unsigned total=count*32;
    flow_thread_t *threads=calloc(total,sizeof(*threads));
    flow_thread_t **running=calloc(count,sizeof(*running));
    CHECK(cpus && index && views && threads && running);
    flow_domain_t domain;flow_domain_init(&domain,index,count);
    flow_group_t groups[2]={0};
    for(unsigned c=0;c<count;c++) {flow_cpu_init(&cpus[c]);CHECK(!flow_cpu_join(&cpus[c],&domain,c));}
    for(unsigned g=0;g<2;g++)CHECK(!flow_group_bind_views(&groups[g],views+g*count,count));
    for(unsigned i=0;i<total;i++) {
        unsigned g=i<count*2?0:1;
        /* The entity owner is deliberately NOT the flow_thread pointer. */
        flow_thread_init(&cpus[i%count],&threads[i],&groups[g],(void *)(uintptr_t)(i+1));
        flow_runnable(&cpus[i%count],&threads[i],1);
    }
    for(unsigned c=0;c<count;c++)flow_charge(&cpus[c],NULL,100000,1);
    CHECK(!domain.fair_clock && !groups[0].cpu_ns && !groups[1].cpu_ns);
    uint64_t now=1,last_flow=0;
    for(unsigned step=0;step<10000;step++) {
        CHECK(!flow_validate(&cpus[0]));
        for(unsigned c=0;c<count;c++) {
            running[c]=flow_pick(&cpus[c]);CHECK(running[c]);
            CHECK(!flow_claim(&cpus[c],running[c]));
            for(unsigned previous=0;previous<c;previous++)CHECK(running[c]!=running[previous]);
            CHECK(flow_migrate(&cpus[c],&cpus[(c+1)%count],running[c])==-1);
            flow_context_t f=flow_new(&cpus[c],FLOW_INPUT,c,now,FLOW_SLICE_NS);
            CHECK(f.id>last_flow);last_flow=f.id;
            if(step%17==0)flow_adopt(&cpus[c],running[c],f,now);
        }
        now+=100000;
        for(unsigned c=0;c<count;c++)flow_charge(&cpus[c],running[c],100000,now);
        for(unsigned c=0;c<count;c++)flow_unclaim(running[c]);
        if(step%31==0) {
            flow_thread_t *t=&threads[step%total];
            int64_t service=t->entity.service;flow_context_t effective=t->effective;
            CHECK(!flow_migrate(t->cpu,&cpus[(t->cpu->cpu_id+1)%count],t));
            CHECK(service==t->entity.service && effective.id==t->effective.id);
        }
    }
    uint64_t a=groups[0].cpu_ns,b=groups[1].cpu_ns;
    CHECK((a>b?a-b:b-a)<=count*FLOW_SLICE_NS);
    CHECK(domain.groups.count==2);
    unsigned local_threads=0;
    for(unsigned g=0;g<2;g++)for(unsigned c=0;c<count;c++)local_threads+=views[g*count+c].threads.count;
    CHECK(local_threads==total);
    CHECK(groups[0].threads.count==2*count && groups[1].threads.count==30*count);
    /* One thread cannot consume several CPUs. Idle capacity may be borrowed,
       but adding threads later must not turn old under-use into unlimited credit. */
    for(unsigned i=1;i<2*count;i++)flow_block(threads[i].cpu,&threads[i]);
    uint64_t a_before=groups[0].cpu_ns,b_before=groups[1].cpu_ns;
    for(unsigned step=0;step<1000;step++) {
        for(unsigned c=0;c<count;c++) {running[c]=flow_pick(&cpus[c]);CHECK(running[c]);CHECK(!flow_claim(&cpus[c],running[c]));}
        now+=100000;
        for(unsigned c=0;c<count;c++)flow_charge(&cpus[c],running[c],100000,now);
        for(unsigned c=0;c<count;c++)flow_unclaim(running[c]);
    }
    CHECK(groups[0].cpu_ns-a_before<=UINT64_C(100000000));
    CHECK(groups[0].cpu_ns-a_before+groups[1].cpu_ns-b_before==(uint64_t)count*100000000);
    for(unsigned i=1;i<2*count;i++)flow_runnable(threads[i].cpu,&threads[i],now);
    CHECK(groups[0].entity.service>=groups[1].entity.service-FLOW_LAG_NS*2);
    flow_thread_t *donor=&threads[0],*middle=&threads[2*count],*owner=&threads[total-1];
    now+=250000001; /* previous input flows have expired */
    flow_context_t f=flow_new(donor->cpu,FLOW_EVENT,1,now,0);flow_adopt(donor->cpu,donor,f,now);
    CHECK(!flow_depend(donor->cpu,donor,middle,now));CHECK(!flow_depend(middle->cpu,middle,owner,now));
    CHECK(owner->effective.id==f.id);
    CHECK(flow_depend(owner->cpu,owner,donor,now)==-1);
    CHECK(!flow_migrate(owner->cpu,&cpus[(owner->cpu->cpu_id+1)%count],owner));
    CHECK(owner->effective.id==f.id);
    flow_forget(middle->cpu,middle,now);CHECK(!donor->blocker && !owner->donors);
    flow_thread_t *timer=&threads[1];flow_block(timer->cpu,timer);flow_timer_arm(timer->cpu,timer,now+700);
    flow_cpu_t *destination=&cpus[(timer->cpu->cpu_id+1)%count];
    CHECK(!flow_migrate(timer->cpu,destination,timer));
    CHECK(!flow_timer_pop(destination,now+699));CHECK(flow_timer_pop(destination,now+700)==timer);
    for(unsigned i=0;i<total;i++)flow_forget(threads[i].cpu,&threads[i],now+701);
    CHECK(!domain.groups.count);
    for(unsigned c=0;c<count;c++)CHECK(!cpus[c].groups.count && !cpus[c].timers);
    printf("FlowSched SMP N=%u global group fairness=%llu/%llu, locality/migration/claims/dependencies/timers PASS\n",count,(unsigned long long)a,(unsigned long long)b);
    free(running);free(threads);free(views);free(index);free(cpus);
}
int main(void) {unsigned counts[]={1,2,3,4,8};for(unsigned i=0;i<5;i++)run(counts[i]);return 0;}
