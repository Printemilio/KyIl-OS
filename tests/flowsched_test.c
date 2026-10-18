#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unos/flowsched.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FlowSched FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static flow_cpu_t cpu;
static flow_group_t groups[4];
static flow_thread_t tasks[34];
static void init(void){flow_cpu_init(&cpu);memset(groups,0,sizeof(groups));memset(tasks,0,sizeof(tasks));}
static void add(unsigned t,unsigned g,uint64_t now){flow_thread_init(&cpu,&tasks[t],&groups[g],&tasks[t]);flow_runnable(&cpu,&tasks[t],now);}
int main(void) {
    init();add(0,0,1);add(1,0,1);add(2,1,1);
    CHECK(!flow_set_qos(&cpu,&tasks[0],2,0,0,1));
    CHECK(!flow_set_qos(&cpu,&tasks[1],-2,0,1,1));
    CHECK(tasks[0].entity.deadline-tasks[0].entity.service==FLOW_SLICE_NS/4);
    CHECK(tasks[1].entity.deadline-tasks[1].entity.service==FLOW_SLICE_NS*16);
    for(unsigned n=0;n<120000;n++){flow_thread_t *t=flow_pick(&cpu);CHECK(t);flow_charge(&cpu,t,100000,(n+1)*100000ull);}
    CHECK(llabs((long long)groups[0].cpu_ns-(long long)groups[1].cpu_ns)<=FLOW_SLICE_NS);
    CHECK(llabs((long long)tasks[0].cpu_ns-(long long)tasks[1].cpu_ns)<=FLOW_SLICE_NS*16);
    uint64_t now=12000000001ull;flow_adopt(&cpu,&tasks[0],flow_new(&cpu,FLOW_INPUT,1,now,1000),now);
    CHECK(tasks[0].effective.id);CHECK(!flow_set_qos(&cpu,&tasks[0],0,1,0,now));CHECK(!tasks[0].effective.id);
    flow_adopt(&cpu,&tasks[2],flow_new(&cpu,FLOW_INPUT,3,now,1000),now);
    CHECK(!flow_depend(&cpu,&tasks[2],&tasks[0],now) && tasks[0].effective.id==tasks[2].effective.id);
    CHECK(flow_validate(&cpu)==0);puts("FlowSched QoS: real deadlines / unchanged group fairness / automatic suppression / dependency donation PASS");
    init();flow_charge(&cpu,NULL,100000,1);CHECK(!cpu.fair_clock);
    init();add(0,0,1);for(unsigned t=1;t<=30;t++)add(t,1,1);
    for(unsigned n=0;n<120000;n++){flow_thread_t *t=flow_pick(&cpu);CHECK(t);flow_charge(&cpu,t,100000,(n+1)*100000ull);}
    CHECK((uint64_t)llabs((long long)groups[0].cpu_ns-(long long)groups[1].cpu_ns)<=FLOW_SLICE_NS);
    for(unsigned t=1;t<=30;t++)CHECK(tasks[t].cpu_ns>190000000ull && tasks[t].cpu_ns<210000000ull);
    printf("FlowSched hierarchy 1 vs 30: %llu / %llu ns PASS\n",(unsigned long long)groups[0].cpu_ns,(unsigned long long)groups[1].cpu_ns);
    init();add(0,0,1);add(1,1,1);add(2,2,1);
    for(unsigned n=0;n<60000;n++) {
        uint64_t now=(n+1)*100000ull;
        flow_adopt(&cpu,&tasks[0],flow_new(&cpu,FLOW_INPUT,1,now,0),now);
        flow_thread_t *t=flow_pick(&cpu);CHECK(t);flow_charge(&cpu,t,100000,now);
    }
    for(unsigned g=0;g<3;g++)CHECK(groups[g].cpu_ns>1990000000ull && groups[g].cpu_ns<2010000000ull);
    puts("FlowSched malicious urgency cannot bypass eligibility: PASS");
    flow_adopt(&cpu,&tasks[0],flow_new(&cpu,FLOW_INPUT,1,6000000001ull,1000),6000000001ull);
    CHECK(flow_depend(&cpu,&tasks[0],&tasks[1],6000000001ull)==0);
    CHECK(flow_depend(&cpu,&tasks[1],&tasks[2],6000000001ull)==0);
    CHECK(tasks[2].effective.id==tasks[0].effective.id);
    CHECK(flow_depend(&cpu,&tasks[2],&tasks[0],6000000001ull)==-1);
    flow_forget(&cpu,&tasks[1],6000000002ull);
    CHECK(!tasks[0].blocker && !tasks[2].donors && !tasks[1].runnable);
    puts("FlowSched transitive blocker donation/cycle rejection/teardown: PASS");
    init();for(unsigned t=0;t<34;t++)add(t,t%4,1);
    for(unsigned t=0;t<34;t++){flow_block(&cpu,&tasks[t]);flow_timer_arm(&cpu,&tasks[t],1000+(33-t)*100);}
    for(unsigned t=0;t<34;t++){CHECK(!flow_timer_pop(&cpu,999+t*100));CHECK(flow_timer_pop(&cpu,1000+t*100)==&tasks[33-t]);}
    CHECK(!cpu.timers && !cpu.groups.count);
    puts("FlowSched ordered timers and empty idle runqueue: PASS");
    init();add(0,0,1);add(1,1,1);flow_block(&cpu,&tasks[1]);
    for(unsigned n=0;n<1000;n++)flow_charge(&cpu,flow_pick(&cpu),100000,(n+1)*100000ull);
    flow_runnable(&cpu,&tasks[1],100000001);CHECK(tasks[1].group->entity.service>=groups[0].entity.service-FLOW_LAG_NS);
    flow_adopt(&cpu,&tasks[1],flow_new(&cpu,FLOW_INPUT,2,100000001,0),100000001);
    CHECK(flow_pick(&cpu)==&tasks[1]);
    flow_selected(&cpu,&tasks[1],100000701);CHECK(cpu.latency_max_ns==700);
    tasks[0].wait_order=1;tasks[1].wait_order=9000000;
    CHECK(flow_wait_precedes(&tasks[0],&tasks[1],10000000));
    flow_rebind(&cpu,&tasks[1],&groups[0],100000702);CHECK(cpu.groups.count==1 && groups[0].threads.count==2);
    flow_forget(&cpu,&tasks[0],100000703);flow_forget(&cpu,&tasks[1],100000704);CHECK(!cpu.groups.count);
    puts("FlowSched bounded wake credit, urgent preemption, wait aging, rebind: PASS");
    init();add(0,0,1);add(1,1,1);
    flow_charge(&cpu,&tasks[0],UINT64_C(5000000000000000000),2);
    flow_charge(&cpu,&tasks[1],UINT64_C(5000000000000000000),2);
    CHECK(cpu.groups.sum>(__int128)INT64_MAX && flow_pick(&cpu));
    flow_block(&cpu,&tasks[0]);flow_block(&cpu,&tasks[1]);CHECK(!cpu.groups.count);
    puts("FlowSched aggregate virtual time beyond int64 without overflow: PASS");
    return 0;
}
