/* Private FD inventory. A number names a slot in ONE resource-owning Process;
 * unrelated processes cannot consume its descriptor capacity. Object references
 * remain shared by dup/fork/IPC. Included by the provider and its host test. */
typedef struct ladybird_process_fd_table {
    struct ladybird_process_fd_table *next;
    uint32_t owner_pid;
    ladybird_fd_slot_t slots[LADYBIRD_MAX_FDS];
} ladybird_process_fd_table_t;
static ladybird_process_fd_table_t *ladybird_fd_tables;
static uint8_t fd_inventory_gate;

static uint64_t fd_inventory_lock(void) {
    uint64_t flags;unsigned short cs;
    __asm__ volatile("pushfq; pop %0; mov %%cs,%1":"=r"(flags),"=r"(cs));
    if(!(cs&3))__asm__ volatile("cli" ::: "memory");
#ifdef FD_INVENTORY_TESTING
    while(__atomic_test_and_set(&fd_inventory_gate,__ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause" ::: "memory");
#else
    while(smp_cpu_count()>1u && __atomic_test_and_set(&fd_inventory_gate,__ATOMIC_ACQUIRE)) {
        smp_relax();
#endif
    }
    return flags;
}
static void fd_inventory_unlock(uint64_t flags) {
#ifdef FD_INVENTORY_TESTING
    __atomic_clear(&fd_inventory_gate,__ATOMIC_RELEASE);
#else
    if(smp_cpu_count()>1u)__atomic_clear(&fd_inventory_gate,__ATOMIC_RELEASE);
#endif
    unsigned short cs;__asm__ volatile("mov %%cs,%0":"=r"(cs));
    if(!(cs&3))__asm__ volatile("push %0; popfq"::"r"(flags):"memory","cc");
}
static ladybird_process_fd_table_t *ladybird_fd_find_table(uint32_t owner) {
    for(ladybird_process_fd_table_t *t=ladybird_fd_tables;t;t=t->next)
        if(t->owner_pid==owner)return t;
    return NULL;
}
static ladybird_process_fd_table_t *ladybird_fd_get_table(uint32_t owner) {
    uint64_t flags=fd_inventory_lock();
    ladybird_process_fd_table_t *t=ladybird_fd_find_table(owner);
    fd_inventory_unlock(flags);
    if(t)return t;
    ladybird_process_fd_table_t *fresh=kmalloc(sizeof(*fresh));
    if(!fresh)return NULL;
    memset(fresh,0,sizeof(*fresh));fresh->owner_pid=owner;
    flags=fd_inventory_lock();
    t=ladybird_fd_find_table(owner);
    if(!t){fresh->next=ladybird_fd_tables;ladybird_fd_tables=fresh;t=fresh;fresh=NULL;}
    fd_inventory_unlock(flags);kfree(fresh);return t;
}
static ladybird_fd_slot_t *ladybird_fd_find_slot(uint32_t owner,int fd) {
    if(fd<0 || fd>=LADYBIRD_MAX_FDS)return NULL;
    ladybird_process_fd_table_t *t=ladybird_fd_find_table(owner);
    return t && t->slots[fd].object?&t->slots[fd]:NULL;
}
static ladybird_process_fd_table_t *ladybird_fd_detach_table(uint32_t owner) {
    uint64_t flags=fd_inventory_lock();
    ladybird_process_fd_table_t **link=&ladybird_fd_tables,*t;
    while((t=*link) && t->owner_pid!=owner)link=&t->next;
    if(t)*link=t->next;
    fd_inventory_unlock(flags);return t;
}
/* Allocate first, publish only after the entire inventory has been retained.
 * OOM/refcount overflow never publishes a partially inherited child. */
static int ladybird_fd_clone_inventory(uint32_t parent,uint32_t child) {
    ladybird_process_fd_table_t *fresh=kmalloc(sizeof(*fresh));
    if(!fresh)return -ENOMEM;
    memset(fresh,0,sizeof(*fresh));fresh->owner_pid=child;
    uint64_t flags=fd_inventory_lock();
    ladybird_process_fd_table_t *source=ladybird_fd_find_table(parent);
    if(ladybird_fd_find_table(child)){fd_inventory_unlock(flags);kfree(fresh);return -EINVAL;}
    if(source)for(unsigned i=0;i<LADYBIRD_MAX_FDS;i++) {
        ladybird_fd_object_t *o=source->slots[i].object;
        if(o && o->refcount==UINT32_MAX){
            for(unsigned j=0;j<i;j++)if(fresh->slots[j].object)--fresh->slots[j].object->refcount;
            fd_inventory_unlock(flags);kfree(fresh);return -EMFILE;
        }
        fresh->slots[i]=source->slots[i];fresh->slots[i].owner_pid=child;
        if(o)++o->refcount;
    }
    fresh->next=ladybird_fd_tables;ladybird_fd_tables=fresh;
    fd_inventory_unlock(flags);return 0;
}
