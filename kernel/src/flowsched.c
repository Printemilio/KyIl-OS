#include <unos/flowsched.h>
#include <stddef.h>
#include <string.h>

/* Deterministic intrusive treaps: key is (received service, insertion order),
 * augmentation is earliest virtual deadline. Prefix queries examine only
 * entities with nonnegative lag relative to the active virtual-time average.
 * Urgency changes the request deadline, NEVER eligibility or CPU weight. */
static uint32_t rank_of(uint64_t n) {
    n^=n>>30;n*=0xbf58476d1ce4e5b9ull;n^=n>>27;n*=0x94d049bb133111ebull;n^=n>>31;
    return (uint32_t)n;
}
static int before(const flow_node_t *a,const flow_node_t *b) {
    return a->service<b->service || (a->service==b->service && a->sequence<b->sequence);
}
static flow_node_t *earlier(flow_node_t *a,flow_node_t *b) {
    if(!a)return b;if(!b)return a;
    return a->deadline<b->deadline || (a->deadline==b->deadline && a->sequence<b->sequence)?a:b;
}
static flow_node_t *update(flow_node_t *n) {
    if(n)n->best=earlier(earlier(n,n->left?n->left->best:NULL),n->right?n->right->best:NULL);
    return n;
}
static flow_node_t *merge(flow_node_t *a,flow_node_t *b) {
    if(!a)return b;if(!b)return a;
    if(a->rank<b->rank){a->right=merge(a->right,b);return update(a);}
    b->left=merge(a,b->left);return update(b);
}
static void split(flow_node_t *root,flow_node_t *key,flow_node_t **a,flow_node_t **b) {
    if(!root){*a=*b=NULL;return;}
    if(before(root,key)){*a=root;split(root->right,key,&root->right,b);update(root);}
    else {*b=root;split(root->left,key,a,&root->left);update(root);}
}
static flow_node_t *insert(flow_node_t *root,flow_node_t *n) {
    if(!root)return update(n);
    if(n->rank<root->rank){split(root,n,&n->left,&n->right);return update(n);}
    if(before(n,root))root->left=insert(root->left,n);else root->right=insert(root->right,n);
    return update(root);
}
static flow_node_t *remove_node(flow_node_t *root,flow_node_t *n) {
    if(!root)return NULL;if(root==n)return merge(root->left,root->right);
    if(before(n,root))root->left=remove_node(root->left,n);else root->right=remove_node(root->right,n);
    return update(root);
}
static int64_t virtual_now(const flow_index_t *q) {
    if(!q->count)return q->floor;
    /* Base-2^32 long division avoids a freestanding __divti3 dependency.
       The average fits int64 because each participating service does. */
    unsigned __int128 magnitude=q->sum<0?(unsigned __int128)-q->sum:(unsigned __int128)q->sum;
    uint64_t remainder=0,result=0;
    for(int shift=96;shift>=0;shift-=32) {
        uint64_t limb=(remainder<<32)|(uint32_t)(magnitude>>shift);
        result=(result<<32)|(limb/q->count);remainder=limb%q->count;
    }
    return q->sum<0?-(int64_t)result:(int64_t)result;
}
static void dequeue(flow_index_t *q,flow_node_t *n) {
    if(!n->linked)return;
    q->floor=virtual_now(q);q->root=remove_node(q->root,n);q->sum-=n->service;--q->count;
    n->linked=0;n->left=n->right=n->best=NULL;
}
static void enqueue(flow_index_t *q,flow_node_t *n) {
    if(n->linked)return;
    q->root=insert(q->root,n);q->sum+=n->service;++q->count;n->linked=1;
}
static flow_node_t *eligible(flow_node_t *root,int64_t v) {
    if(!root)return NULL;
    if(root->service>v)return eligible(root->left,v);
    return earlier(earlier(root,root->left?root->left->best:NULL),eligible(root->right,v));
}
static flow_node_t *pick_index(flow_index_t *q) {return eligible(q->root,virtual_now(q));}
static flow_index_t *group_index(flow_cpu_t *c) {return c->domain?&c->domain->groups:&c->groups;}
static uint64_t *fair_clock(flow_cpu_t *c) {return c->domain?&c->domain->fair_clock:&c->fair_clock;}
static flow_cpu_t *thread_cpu(flow_cpu_t *c,flow_thread_t *t) {return t && t->cpu?t->cpu:c;}
static void local_remove(flow_thread_t *t) {
    if(!t->cpu || !t->cpu->domain || !t->group->views)return;
    flow_group_view_t *v=&t->group->views[t->cpu->cpu_id];
    dequeue(&t->cpu->groups,&v->entity);dequeue(&v->threads,&t->local_entity);
}
static void local_insert(flow_thread_t *t) {
    if(!t->cpu || !t->cpu->domain || !t->group->views)return;
    flow_group_view_t *v=&t->group->views[t->cpu->cpu_id];
    v->group=t->group;v->cpu=t->cpu;
    t->local_entity.service=t->entity.service;t->local_entity.deadline=t->entity.deadline;
    t->local_entity.sequence=t->entity.sequence;t->local_entity.rank=t->entity.rank;t->local_entity.owner=t;
    enqueue(&v->threads,&t->local_entity);
}
static void thread_dequeue(flow_group_t *g,flow_thread_t *t) {local_remove(t);dequeue(&g->threads,&t->entity);}
static void thread_enqueue(flow_group_t *g,flow_thread_t *t) {enqueue(&g->threads,&t->entity);local_insert(t);}
static void refresh_views(flow_group_t *g) {
    if(!g->views)return;
    for(uint32_t i=0;i<g->view_count;++i) {
        flow_group_view_t *v=&g->views[i];if(!v->cpu)continue;
        dequeue(&v->cpu->groups,&v->entity);
        v->entity.service=g->entity.service;v->entity.deadline=g->entity.deadline;
        v->entity.sequence=g->entity.sequence;v->entity.rank=g->entity.rank;v->entity.owner=v;
        if(v->threads.count)enqueue(&v->cpu->groups,&v->entity);
    }
}
static void group_deadline(flow_group_t *g) {
    flow_node_t *t=pick_index(&g->threads);
    int64_t request=t?t->deadline-t->service:(int64_t)FLOW_SLICE_NS;
    if(request<0)request=0;if(request>(int64_t)FLOW_SLICE_NS)request=FLOW_SLICE_NS;
    g->entity.deadline=g->entity.service+request;
    refresh_views(g);
}
static void node_init(flow_cpu_t *c,flow_node_t *n,void *owner) {
    memset(n,0,sizeof(*n));n->sequence=c->domain?++c->domain->sequence:++c->sequence;n->rank=rank_of(n->sequence);n->owner=owner;
}
void flow_cpu_init(flow_cpu_t *c) {memset(c,0,sizeof(*c));}
void flow_domain_init(flow_domain_t *d,flow_cpu_t **cpus,uint32_t count) {
    memset(d,0,sizeof(*d));d->cpus=cpus;d->cpu_count=count;
    if(cpus)memset(cpus,0,(size_t)count*sizeof(*cpus));
}
int flow_cpu_join(flow_cpu_t *c,flow_domain_t *d,uint32_t index) {
    if(!c || !d || !d->cpus || index>=d->cpu_count || d->cpus[index] || c->groups.count || c->timers)return -1;
    c->domain=d;c->cpu_id=index;d->cpus[index]=c;return 0;
}
int flow_group_bind_views(flow_group_t *g,flow_group_view_t *views,uint32_t count) {
    if(!g || !views || !count || g->threads.count || g->views)return -1;
    memset(views,0,(size_t)count*sizeof(*views));g->views=views;g->view_count=count;return 0;
}
void flow_thread_init(flow_cpu_t *c,flow_thread_t *t,flow_group_t *g,void *owner) {
    memset(t,0,sizeof(*t));t->cpu=c;t->running_cpu=UINT32_MAX;t->pinned_cpu=UINT32_MAX;node_init(c,&t->entity,owner);
    if(!g)g=&t->private_group;
    if(!g->entity.owner){node_init(c,&g->entity,g);g->entity.service=virtual_now(group_index(c));}
    t->group=g;t->entity.service=virtual_now(&g->threads);
}
static void refresh(flow_cpu_t *c,flow_thread_t *t,uint64_t now) {
    c=thread_cpu(c,t);
    flow_group_t *g=t->group;int was=t->runnable;
    if(was){dequeue(group_index(c),&g->entity);thread_dequeue(g,t);}
    flow_context_t best=t->suppress_automatic?(flow_context_t){0}:t->local;
    if(best.id && now-best.born_ns>250000000ull)best=(flow_context_t){0};
    for(flow_thread_t *d=t->donors;d;d=d->donor_next) {
        flow_context_t f=d->effective;
        if(f.id && now-f.born_ns<=250000000ull && (!best.id || f.deadline_ns<best.deadline_ns))best=f;
    }
    t->effective=best;uint64_t request=FLOW_SLICE_NS;
    if(t->latency_class>0)request>>=t->latency_class;else request<<=-t->latency_class;
    if(t->efficiency)request*=4;
    if(best.id){uint64_t remaining=best.deadline_ns>now?best.deadline_ns-now:0;if(remaining<request)request=remaining;}
    t->entity.deadline=t->entity.service+(int64_t)request;
    if(was){thread_enqueue(g,t);group_deadline(g);enqueue(group_index(c),&g->entity);}
}
static void propagate(flow_cpu_t *c,flow_thread_t *t,uint64_t now) {
    for(unsigned depth=0;t && depth<64;depth++,t=t->blocker){refresh(c,t,now);++c->propagated;}
}
int flow_set_qos(flow_cpu_t *c,flow_thread_t *t,int latency,int suppress,int efficiency,uint64_t now) {
    if(!t || latency< -4 || latency>4 || (suppress!=0 && suppress!=1) || (efficiency!=0 && efficiency!=1))return -1;
    t->latency_class=(int8_t)latency;t->suppress_automatic=(uint8_t)suppress;t->efficiency=(uint8_t)efficiency;
    propagate(c,t,now);return 0;
}
void flow_runnable(flow_cpu_t *c,flow_thread_t *t,uint64_t now) {
    c=thread_cpu(c,t);
    if(t->runnable)return;
    flow_group_t *g=t->group;int64_t v=virtual_now(group_index(c));
    if(c->domain && g->threads.count && group_index(c)->count>1u) {
        flow_index_t others=*group_index(c);others.sum-=g->entity.service;--others.count;
        v=virtual_now(&others);
    }
    if(c->domain && g->entity.service<v-FLOW_LAG_NS) {
        /* Growing demand cannot cash in credit accumulated while the group
           lacked enough parallel work to consume its fair share. */
        dequeue(group_index(c),&g->entity);g->entity.service=v-FLOW_LAG_NS;
    }
    if(!g->threads.count){
        if(g->entity.service<v-FLOW_LAG_NS)g->entity.service=v-FLOW_LAG_NS;
        /* Sleeping entities retain debt but cannot hoard unbounded credit. */
        g->entitlement_stamp=(*fair_clock(c));
    } else dequeue(group_index(c),&g->entity);
    v=virtual_now(&g->threads);if(t->entity.service<v-FLOW_LAG_NS)t->entity.service=v-FLOW_LAG_NS;
    t->runnable_ns=now;t->entitlement_stamp=g->thread_clock;refresh(c,t,now);t->runnable=1;
    thread_enqueue(g,t);group_deadline(g);enqueue(group_index(c),&g->entity);++c->wakes;
}
void flow_block(flow_cpu_t *c,flow_thread_t *t) {
    c=thread_cpu(c,t);
    if(!t->runnable)return;
    flow_group_t *g=t->group;
    t->entitlement_ns+=g->thread_clock-t->entitlement_stamp;
    dequeue(group_index(c),&g->entity);thread_dequeue(g,t);t->runnable=0;
    if(g->threads.count){group_deadline(g);enqueue(group_index(c),&g->entity);}
    else {refresh_views(g);g->entitlement_ns+=(*fair_clock(c))-g->entitlement_stamp;g->entitlement_stamp=(*fair_clock(c));}
}
void flow_rebind(flow_cpu_t *c,flow_thread_t *t,flow_group_t *g,uint64_t now) {
    c=thread_cpu(c,t);
    int was=t->runnable;flow_block(c,t);t->group=g?g:&t->private_group;
    if(!t->group->entity.owner){node_init(c,&t->group->entity,t->group);t->group->entity.service=virtual_now(group_index(c));}
    t->entity.service=virtual_now(&t->group->threads);if(was)flow_runnable(c,t,now);
}
void flow_charge(flow_cpu_t *c,flow_thread_t *t,uint64_t elapsed,uint64_t now) {
    c=thread_cpu(c,t);
    if(!t || !t->runnable)return;
    flow_group_t *g=t->group;
    if(group_index(c)->count)(*fair_clock(c))+=elapsed/group_index(c)->count;
    if(g->threads.count)g->thread_clock+=elapsed/g->threads.count;
    dequeue(group_index(c),&g->entity);thread_dequeue(g,t);
    t->cpu_ns+=elapsed;g->cpu_ns+=elapsed;t->entity.service+=(int64_t)elapsed;g->entity.service+=(int64_t)elapsed;
    /* Effective donations are maintained on dependency/flow events, not
       rebuilt for every accounting sample. Only expiration requires a local
       donor refresh. Retain an ordinary request until it is consumed. */
    int64_t old=t->entity.deadline;t->runnable=0;
    if(t->effective.id && now-t->effective.born_ns>250000000ull) {
        refresh(c,t,now);
    } else {
        uint64_t request=FLOW_SLICE_NS;
        if(t->latency_class>0)request>>=t->latency_class;else request<<=-t->latency_class;
        if(t->efficiency)request*=4;
        if(t->effective.id) {
            uint64_t remaining=t->effective.deadline_ns>now?t->effective.deadline_ns-now:0;
            if(remaining<request)request=remaining;
        }
        t->entity.deadline=t->entity.service+(int64_t)request;
    }
    if(!t->effective.id && old>t->entity.service)t->entity.deadline=old;
    t->runnable=1;thread_enqueue(g,t);group_deadline(g);enqueue(group_index(c),&g->entity);
}
typedef int (*node_accept_t)(flow_node_t *,void *);
static flow_node_t *eligible_filtered(flow_node_t *n,int64_t v,node_accept_t accept,void *arg) {
    if(!n)return NULL;
    if(n->service>v)return eligible_filtered(n->left,v,accept,arg);
    return earlier(earlier(accept(n,arg)?n:NULL,eligible_filtered(n->left,v,accept,arg)),eligible_filtered(n->right,v,accept,arg));
}
static int thread_available(flow_node_t *node,void *arg) {
    flow_thread_t *t=(flow_thread_t *)node;flow_cpu_t *c=arg;
    return flow_cpu_allowed(t,c->cpu_id) &&
        (t->running_cpu==UINT32_MAX || t->running_cpu==c->cpu_id);
}
static int local_thread_available(flow_node_t *node,void *arg) {
    return thread_available(&((flow_thread_t *)node->owner)->entity,arg);
}
static flow_thread_t *available_thread(flow_group_t *g,flow_cpu_t *c) {
    flow_node_t *n=eligible_filtered(g->threads.root,virtual_now(&g->threads),thread_available,c);
    /* If lower-service peers are already executing elsewhere, borrowing idle
       capacity keeps debt intact; it does not mint another group entitlement. */
    if(!n)n=eligible_filtered(g->threads.root,INT64_MAX,thread_available,c);
    return (flow_thread_t *)n;
}
static int group_available(flow_node_t *node,void *arg) {
    return available_thread(node->owner,arg)!=NULL;
}
flow_thread_t *flow_pick(flow_cpu_t *c) {
    if(!c->domain) {
        flow_node_t *g=pick_index(&c->groups);if(!g)return NULL;
        return (flow_thread_t *)pick_index(&((flow_group_t *)g->owner)->threads);
    }
    /* Global eligibility is based on one Process entity, independent of its
       thread/CPU count. Local view preference never bypasses that eligibility. */
    flow_node_t *n=eligible_filtered(c->domain->groups.root,virtual_now(&c->domain->groups),group_available,c);
    if(!n)n=eligible_filtered(c->domain->groups.root,INT64_MAX,group_available,c);
    if(!n)return NULL;flow_group_t *g=n->owner;
    flow_node_t *t=NULL;
    if(g->views && c->cpu_id<g->view_count)
        t=eligible_filtered(g->views[c->cpu_id].threads.root,virtual_now(&g->threads),local_thread_available,c);
    return t?t->owner:available_thread(g,c);
}
int flow_claim(flow_cpu_t *c,flow_thread_t *t) {
    if(!c || !t || !t->runnable || !flow_cpu_allowed(t,c->cpu_id) ||
       (t->running_cpu!=UINT32_MAX && t->running_cpu!=c->cpu_id))return -1;
    if(t->cpu!=c && flow_migrate(t->cpu,c,t))return -1;
    t->running_cpu=c->cpu_id;return 0;
}
int flow_cpu_allowed(const flow_thread_t *t,uint32_t index) {
    if(t->pinned_cpu!=UINT32_MAX && t->pinned_cpu!=index)return 0;
    if(!t->allowed_cpus)return 1;
    for(uint32_t i=0;i<t->allowed_count;i++){if(t->allowed_cpus[i]==index)return 1;if(t->allowed_cpus[i]>index)break;}
    return 0;
}
void flow_unclaim(flow_thread_t *t) {if(t)t->running_cpu=UINT32_MAX;}
int flow_migrate(flow_cpu_t *source,flow_cpu_t *destination,flow_thread_t *t) {
    if(!source || !destination || !t || t->cpu!=source || !source->domain || source->domain!=destination->domain ||
       t->running_cpu!=UINT32_MAX || !t->group->views || t->group->view_count!=source->domain->cpu_count)return -1;
    if(source==destination)return 0;
    uint8_t sleeping=t->sleeping;uint64_t deadline=t->sleep_deadline;
    flow_timer_cancel(source,t);
    if(t->runnable)local_remove(t);
    t->cpu=destination;
    if(t->runnable)local_insert(t);
    refresh_views(t->group);
    if(sleeping)flow_timer_arm(destination,t,deadline);
    return 0;
}
void flow_selected(flow_cpu_t *c,flow_thread_t *t,uint64_t now) {
    t->selected_ns=now;
    if(t->input_at_ns) {
        uint64_t latency=now-t->input_at_ns,us=latency/1000;unsigned bin=0;
        while(us>1 && bin<31){us>>=1;++bin;}++t->input_hist[bin];
        if(latency>t->input_max_ns)t->input_max_ns=latency;t->input_at_ns=0;
    }
    if(t->runnable_ns){uint64_t latency=now-t->runnable_ns,us=latency/1000;unsigned bin=0;
        while(us>1 && bin<31){us>>=1;++bin;}++c->latency_hist[bin];++t->latency_hist[bin];
        if(latency>t->latency_max_ns)t->latency_max_ns=latency;
        if(latency>c->latency_max_ns)c->latency_max_ns=latency;t->runnable_ns=0;}
    ++c->switches;
}
flow_context_t flow_new(flow_cpu_t *c,uint32_t origin,uint32_t tid,uint64_t now,uint64_t budget) {
    if(budget>250000000ull)budget=250000000ull;
    return (flow_context_t){c->domain?++c->domain->flow_sequence:++c->flow_sequence,now,now>UINT64_MAX-budget?UINT64_MAX:now+budget,origin,tid};
}
void flow_adopt(flow_cpu_t *c,flow_thread_t *t,flow_context_t f,uint64_t now) {
    if(!f.id || now<f.born_ns || now-f.born_ns>250000000ull)return;
    if(!t->local.id || now-t->local.born_ns>250000000ull || f.deadline_ns<t->local.deadline_ns)t->local=f;
    propagate(c,t,now);
}
void flow_undepend(flow_cpu_t *c,flow_thread_t *w,uint64_t now) {
    flow_thread_t *b=w->blocker;if(!b)return;
    flow_thread_t **link=&b->donors;while(*link && *link!=w)link=&(*link)->donor_next;
    if(*link)*link=w->donor_next;w->donor_next=NULL;w->blocker=NULL;propagate(c,b,now);
}
int flow_depend(flow_cpu_t *c,flow_thread_t *w,flow_thread_t *b,uint64_t now) {
    unsigned depth=0;for(flow_thread_t *p=b;p;p=p->blocker) {
        if(p==w || ++depth>=64){++c->dependency_cycles;return -1;}
    }
    flow_undepend(c,w,now);if(!b)return 0;
    w->blocker=b;w->donor_next=b->donors;b->donors=w;++c->inversions;propagate(c,b,now);return 0;
}
void flow_forget(flow_cpu_t *c,flow_thread_t *t,uint64_t now) {
    flow_timer_cancel(c,t);flow_block(c,t);flow_undepend(c,t,now);
    while(t->donors)flow_undepend(c,t->donors,now);
}
/* Intrusive deadline timer treap: O(log sleepers), no process-list timer scan. */
static int timer_before(flow_thread_t *a,flow_thread_t *b) {
    return a->sleep_deadline<b->sleep_deadline || (a->sleep_deadline==b->sleep_deadline && a->entity.sequence<b->entity.sequence);
}
static flow_thread_t *timer_merge(flow_thread_t *a,flow_thread_t *b) {
    if(!a)return b;if(!b)return a;
    if(a->timer_rank<b->timer_rank){a->timer_right=timer_merge(a->timer_right,b);return a;}
    b->timer_left=timer_merge(a,b->timer_left);return b;
}
static void timer_split(flow_thread_t *r,flow_thread_t *t,flow_thread_t **a,flow_thread_t **b) {
    if(!r){*a=*b=NULL;return;}
    if(timer_before(r,t)){*a=r;timer_split(r->timer_right,t,&r->timer_right,b);}
    else {*b=r;timer_split(r->timer_left,t,a,&r->timer_left);}
}
static flow_thread_t *timer_insert(flow_thread_t *r,flow_thread_t *t) {
    if(!r)return t;if(t->timer_rank<r->timer_rank){timer_split(r,t,&t->timer_left,&t->timer_right);return t;}
    if(timer_before(t,r))r->timer_left=timer_insert(r->timer_left,t);else r->timer_right=timer_insert(r->timer_right,t);return r;
}
static flow_thread_t *timer_remove(flow_thread_t *r,flow_thread_t *t) {
    if(!r)return NULL;if(r==t)return timer_merge(r->timer_left,r->timer_right);
    if(timer_before(t,r))r->timer_left=timer_remove(r->timer_left,t);else r->timer_right=timer_remove(r->timer_right,t);return r;
}
void flow_timer_cancel(flow_cpu_t *c,flow_thread_t *t) {
    c=thread_cpu(c,t);
    if(!t->sleeping)return;c->timers=timer_remove(c->timers,t);t->sleeping=0;t->timer_left=t->timer_right=NULL;
}
void flow_timer_arm(flow_cpu_t *c,flow_thread_t *t,uint64_t deadline) {
    c=thread_cpu(c,t);
    flow_timer_cancel(c,t);t->sleep_deadline=deadline;t->timer_rank=rank_of(t->entity.sequence);
    t->sleeping=1;c->timers=timer_insert(c->timers,t);
}
flow_thread_t *flow_timer_pop(flow_cpu_t *c,uint64_t now) {
    flow_thread_t *t=c->timers;if(!t)return NULL;while(t->timer_left)t=t->timer_left;
    if(t->sleep_deadline>now)return NULL;flow_timer_cancel(c,t);return t;
}
int flow_wait_precedes(const flow_thread_t *a,const flow_thread_t *b,uint64_t now) {
    /* Aging becomes decisive at 8ms, bounding repeated urgency overtaking. */
    int aged_a=now-a->wait_order>=8000000ull,aged_b=now-b->wait_order>=8000000ull;
    if(aged_a || aged_b)return a->wait_order<b->wait_order || (a->wait_order==b->wait_order && a->entity.sequence<b->entity.sequence);
    if(a->effective.id && (!b->effective.id || a->effective.deadline_ns<b->effective.deadline_ns))return 1;
    if(b->effective.id)return 0;
    return a->wait_order<b->wait_order || (a->wait_order==b->wait_order && a->entity.sequence<b->entity.sequence);
}
uint64_t flow_latency_percentile(const flow_cpu_t *c,unsigned p) {
    uint64_t count=0,seen=0;for(unsigned i=0;i<32;i++)count+=c->latency_hist[i];
    if(!count)return 0;uint64_t target=(count*p+99)/100;
    for(unsigned i=0;i<32;i++){seen+=c->latency_hist[i];if(seen>=target)return (2ull<<i)*1000;}return c->latency_max_ns;
}

typedef struct {unsigned count;__int128 sum;flow_node_t *best,*first,*last;} flow_tree_check_t;
static int check_tree(flow_node_t *n,unsigned depth,flow_tree_check_t *out) {
    *out=(flow_tree_check_t){0};if(!n)return 0;
    if(depth>256u || !n->linked)return -1;
    flow_tree_check_t left,right;
    if(check_tree(n->left,depth+1u,&left) || check_tree(n->right,depth+1u,&right))return -2;
    if((left.last && !before(left.last,n)) || (right.first && !before(n,right.first)))return -3;
    if((n->left && n->left->rank<n->rank) || (n->right && n->right->rank<n->rank))return -4;
    out->count=left.count+right.count+1u;out->sum=left.sum+right.sum+n->service;
    out->best=earlier(earlier(n,left.best),right.best);
    out->first=left.first?left.first:n;out->last=right.last?right.last:n;
    return out->best==n->best?0:-5;
}
static int check_index(const flow_index_t *index) {
    flow_tree_check_t tree;int result=check_tree(index->root,0u,&tree);
    if(result)return result;return tree.count==index->count && tree.sum==index->sum?0:-6;
}
static int check_groups(flow_node_t *n,unsigned depth) {
    if(!n)return 0;if(depth>256u)return -1;
    flow_group_t *g=n->owner;
    if(!g || &g->entity!=n || !g->threads.count || check_index(&g->threads))return -7;
    if(g->views)for(unsigned i=0;i<g->view_count;++i)if(check_index(&g->views[i].threads))return -8;
    return check_groups(n->left,depth+1u)?-9:check_groups(n->right,depth+1u);
}
int flow_validate(const flow_cpu_t *c) {
    if(!c)return -10;
    const flow_index_t *groups=c->domain?&c->domain->groups:&c->groups;
    int result=check_index(groups);if(result)return result;
    result=check_groups(groups->root,0u);if(result)return result;
    if(c->domain)for(unsigned i=0;i<c->domain->cpu_count;++i) {
        if(!c->domain->cpus[i] || check_index(&c->domain->cpus[i]->groups))return -11;
    }
    return 0;
}
