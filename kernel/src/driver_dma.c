#include <unos/driver_dma.h>
#include <string.h>

typedef struct dma_binding {ora_ref_t target;size_t offset,length;unsigned owned;} dma_binding_t;
typedef struct dma_storage {
    struct dma_storage *next;
    ora_ref_t object;
    drv_dma_context_t *owner;
    drv_dma_provider_t provider;
    void *cpu,*cookie;
    uint64_t bus;
    uint64_t generation;
    size_t size,charge,metadata_charge;
    unsigned grant,device,busy,endpoint,post_cursor;
    dma_binding_t *bindings;
    unsigned binding_count;
} dma_storage_t;
static uint32_t epoch_source;
static int width_valid(unsigned w){return w==1||w==2||w==4||w==8;}
static uint64_t width_mask(unsigned w){return w==8?UINT64_MAX:(1ull<<(w*8))-1;}
static int range(size_t size,uint64_t off,uint64_t n){return off<=size && n<=size-off;}
static uint64_t load(const void *p,unsigned w){uint64_t v=0;memcpy(&v,p,w);return v;}
static uint64_t device_load(const void *p,unsigned w) {
    switch(w){case 1:return *(volatile const uint8_t *)p;case 2:return *(volatile const uint16_t *)p;
    case 4:return *(volatile const uint32_t *)p;default:return *(volatile const uint64_t *)p;}
}
static void store(void *p,unsigned w,uint64_t v){memcpy(p,&v,w);}
static uint64_t reg_end(const drv_dma_register_t *r) {
    uint64_t count=r->count?r->count:1,stride=r->stride?r->stride:r->width;
    return (uint64_t)r->offset+(count-1)*stride+r->width;
}
static dma_storage_t *lookup(drv_dma_context_t *c,uint64_t id) {
    if(!c || !c->epoch || !id)return NULL;
    for(unsigned i=0;i<DRV_DMA_OBJECTS;i++)if(c->slots[i].token==id)return ora_storage_state(c->slots[i].object);
    return NULL;
}
static ora_ref_t object_for(drv_dma_context_t *c,uint64_t id) {
    for(unsigned i=0;i<DRV_DMA_OBJECTS;i++)if(c->slots[i].token==id)return c->slots[i].object;
    return NULL;
}
static void destroy_storage(void *p) {
    dma_storage_t *s=p;
    dma_storage_t **link=(dma_storage_t **)&s->owner->storages;
    while(*link && *link!=s)link=&(*link)->next;
    if(*link)*link=s->next;
    for(unsigned i=0;i<s->binding_count;i++)ora_release(s->bindings[i].target);
    if(s->bindings)s->provider.metadata_free(s->bindings);
    s->provider.release(s->provider.context,s->cookie,s->charge);
    s->owner->bytes-=s->charge;
    s->owner->metadata_bytes-=s->metadata_charge;
    s->provider.metadata_free(s);
}
static int validate_policy(const drv_dma_policy_t *p) {
    if(!p || !p->grant_count || p->grant_count>DRV_RES_MAX || p->register_count>DRV_DMA_REGS ||
       p->endpoint_count>DRV_RES_MAX || !p->max_bytes || p->max_bytes>DRV_DMA_MAX_BYTES || !p->address_limit)return DRV_FORMAT;
    for(unsigned i=0;i<p->grant_count;i++) {
        const drv_dma_grant_t *g=&p->grants[i];
        if(!g->max_bytes || g->max_bytes>p->max_bytes || !g->rights || (g->rights&~3u) ||
           !g->widths || (g->widths&~15u))return DRV_FORMAT;
        if(g->kind==DRV_DMA_BUFFER){if(g->stride || g->field_count)return DRV_FORMAT;continue;}
        if(g->kind!=DRV_DMA_RING || !g->stride || g->stride>4096 || !g->field_count || g->field_count>DRV_DMA_FIELDS)return DRV_FORMAT;
        unsigned addresses=0;
        for(unsigned f=0;f<g->field_count;f++) {
            const drv_dma_field_t *a=&g->fields[f];
            if(!width_valid(a->width) || !range(g->stride,a->offset,a->width) || a->offset%a->width || g->stride%a->width ||
               a->kind<DRV_DMA_ADDRESS || a->kind>DRV_DMA_STATUS || (a->write_mask&~width_mask(a->width)))return DRV_FORMAT;
            if(a->kind!=DRV_DMA_SCALAR && a->write_mask)return DRV_DENIED;
            if(a->kind==DRV_DMA_ADDRESS) {
                addresses++;
                if(a->width!=8 || a->length_field>=g->field_count || g->fields[a->length_field].kind!=DRV_DMA_LENGTH)return DRV_FORMAT;
                for(unsigned x=0;x<f;x++)if(g->fields[x].kind==DRV_DMA_ADDRESS && g->fields[x].length_field==a->length_field)return DRV_FORMAT;
            } else if(a->length_field!=DRV_ABSENT)return DRV_FORMAT;
            for(unsigned x=0;x<f;x++)if(a->offset<g->fields[x].offset+g->fields[x].width && g->fields[x].offset<a->offset+a->width)return DRV_FORMAT;
        }
        if(!addresses)return DRV_FORMAT;
        for(unsigned f=0;f<g->field_count;f++)if(g->fields[f].kind==DRV_DMA_LENGTH) {
            unsigned uses=0;for(unsigned x=0;x<g->field_count;x++)if(g->fields[x].kind==DRV_DMA_ADDRESS && g->fields[x].length_field==f)uses++;
            if(uses!=1)return DRV_FORMAT;
        }
    }
    for(unsigned i=0;i<p->register_count;i++) {
        const drv_dma_register_t *r=&p->registers[i];
        if(!width_valid(r->width) || r->offset%r->width || reg_end(r)>UINT32_MAX ||
           (r->stride && (r->stride<r->width || r->stride%r->width)) ||
           r->kind<DRV_REG_SCALAR || r->kind>DRV_REG_DOORBELL || !r->rights || (r->rights&~3u) ||
           (r->write_mask&~width_mask(r->width)))return DRV_FORMAT;
        if(r->kind!=DRV_REG_SCALAR && (r->width!=4 || !(r->rights&DRV_WRITE) || r->write_mask || r->count>1 ||
           ((r->kind==DRV_REG_ADDRESS_LOW || r->kind==DRV_REG_ADDRESS_HIGH) && r->rights!=DRV_WRITE)))return DRV_DENIED;
        if(r->guard_mask && (r->kind!=DRV_REG_SCALAR || r->require_endpoint>=p->endpoint_count ||
           (r->guard_mask&~r->write_mask)))return DRV_FORMAT;
        if(r->invalidate_mask && (r->kind!=DRV_REG_SCALAR || (r->invalidate_mask&~r->write_mask)))return DRV_FORMAT;
        if(r->block_endpoint && (r->kind!=DRV_REG_SCALAR || r->block_endpoint>p->endpoint_count))return DRV_FORMAT;
        for(unsigned j=0;j<i;j++)if(r->offset<reg_end(&p->registers[j]) && p->registers[j].offset<reg_end(r))return DRV_FORMAT;
    }
    for(unsigned i=0;i<p->endpoint_count;i++) {
        const drv_dma_endpoint_t *e=&p->endpoints[i];
        unsigned ids[]={e->low,e->high,e->length,e->doorbell};
        unsigned kinds[]={DRV_REG_ADDRESS_LOW,DRV_REG_ADDRESS_HIGH,DRV_REG_LENGTH,DRV_REG_DOORBELL};
        if(e->grant>=p->grant_count || p->grants[e->grant].kind!=DRV_DMA_RING || e->doorbell_value>UINT32_MAX)return DRV_FORMAT;
        if(e->streaming) {
            const drv_dma_grant_t *g=&p->grants[e->grant];
            if(e->streaming!=1 || e->initial_owned>1 || e->post_next>1 || e->length_output>1 ||
               e->completion_field>=g->field_count || g->fields[e->completion_field].kind!=DRV_DMA_STATUS ||
               !e->completion_mask || (e->completion_value&~e->completion_mask) ||
               e->completion_mask>width_mask(g->fields[e->completion_field].width) || !e->min_span)return DRV_FORMAT;
        }
        for(unsigned j=0;j<4;j++)if(ids[j]>=p->register_count || p->registers[ids[j]].kind!=kinds[j])return DRV_DENIED;
        for(unsigned j=0;j<i;j++) {
            const drv_dma_endpoint_t *b=&p->endpoints[j];unsigned other[]={b->low,b->high,b->length,b->doorbell};
            for(unsigned x=0;x<4;x++)for(unsigned y=0;y<4;y++)if(ids[x]==other[y])return DRV_CONFLICT;
        }
    }
    return 0;
}
int drv_dma_init(drv_dma_context_t *c,const drv_dma_policy_t *p,const drv_dma_provider_t *provider) {
    if(!c || c->epoch || !provider || !provider->allocate || !provider->release ||
       !provider->metadata_alloc || !provider->metadata_free)return DRV_FORMAT;
    int e=validate_policy(p);if(e)return e;
    if(p->endpoint_count && (!provider->reg_write || !provider->quiesced))return DRV_UNSUPPORTED;
    uint32_t epoch=__atomic_load_n(&epoch_source,__ATOMIC_RELAXED);
    do {if(epoch==UINT32_MAX)return DRV_NOMEM;}
    while(!__atomic_compare_exchange_n(&epoch_source,&epoch,epoch+1,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    memset(c,0,sizeof(*c));c->epoch=epoch+1;c->next=1;c->policy=*p;c->provider=*provider;return 0;
}
int drv_dma_destroy(drv_dma_context_t *c) {
    if(!c || !c->epoch)return DRV_NOT_BOUND;
    for(dma_storage_t *s=c->storages;s;s=s->next) {
        if(s->device || s->busy)return DRV_BUSY;
        uint64_t owned=0;
        for(unsigned i=0;i<DRV_DMA_OBJECTS;i++)if(c->slots[i].object==s->object)owned++;
        for(dma_storage_t *r=c->storages;r;r=r->next)
            for(unsigned i=0;i<r->binding_count;i++)if(r->bindings[i].target==s->object)owned++;
        /* A kernel client may own an explicit ORA reference beyond its local
         * token. Keep the namespace/provider/accounting alive until it retires. */
        if(ora_reference_count(s->object)!=owned)return DRV_BUSY;
    }
    for(unsigned i=0;i<DRV_DMA_OBJECTS;i++)if(c->slots[i].object)ora_release(c->slots[i].object);
    memset(c,0,sizeof(*c));return 0;
}
static int allocate(drv_dma_context_t *c,unsigned grant,uint64_t length,uint64_t *out) {
    if(grant>=c->policy.grant_count || !length || length>SIZE_MAX-4095 || !c->next)return DRV_BOUNDS;
    const drv_dma_grant_t *g=&c->policy.grants[grant];
    if(length>g->max_bytes || (g->kind==DRV_DMA_RING && (length%g->stride || length/g->stride>DRV_DMA_MAX_DESCRIPTORS)))return DRV_BOUNDS;
    size_t charge=(length+4095)&~(size_t)4095;
    if(c->bytes>c->policy.max_bytes || charge>c->policy.max_bytes-c->bytes)return DRV_NOMEM;
    unsigned index;for(index=0;index<DRV_DMA_OBJECTS && c->slots[index].object;index++);
    if(index==DRV_DMA_OBJECTS)return DRV_NOMEM;
    uint64_t count=g->kind==DRV_DMA_RING?(length/g->stride)*g->field_count:0;
    if(count>UINT32_MAX || count>SIZE_MAX/sizeof(dma_binding_t))return DRV_BOUNDS;
    size_t meta=sizeof(dma_storage_t)+count*sizeof(dma_binding_t);
    if(c->metadata_bytes>DRV_DMA_MAX_METADATA || meta>DRV_DMA_MAX_METADATA-c->metadata_bytes)return DRV_NOMEM;
    dma_storage_t *s=c->provider.metadata_alloc(sizeof(*s));if(!s)return DRV_NOMEM;
    memset(s,0,sizeof(*s));s->owner=c;s->provider=c->provider;s->size=length;s->charge=charge;s->grant=grant;s->metadata_charge=meta;
    if(g->kind==DRV_DMA_RING) {
        s->binding_count=count;s->bindings=c->provider.metadata_alloc(count*sizeof(dma_binding_t));
        if(!s->bindings){c->provider.metadata_free(s);return DRV_NOMEM;}memset(s->bindings,0,count*sizeof(dma_binding_t));
    }
    int e=c->provider.allocate(c->provider.context,charge,&s->cpu,&s->bus,&s->cookie);
    if(e)goto fail;
    if(!s->cpu || !s->bus || s->bus%4096 || s->bus>c->policy.address_limit || charge-1>c->policy.address_limit-s->bus){e=DRV_BOUNDS;goto free_pages;}
    ora_ref_t object=ora_storage_create(length,s,destroy_storage);
    if(!object){e=DRV_NOMEM;goto free_pages;}
    uint64_t token=((uint64_t)c->epoch<<32)|c->next++;
    s->object=object;s->next=c->storages;c->storages=s;
    c->bytes+=charge;c->metadata_bytes+=meta;c->slots[index]=(drv_dma_slot_t){token,object};*out=token;return 0;
free_pages:c->provider.release(c->provider.context,s->cookie,charge);
fail:if(s->bindings)c->provider.metadata_free(s->bindings);c->provider.metadata_free(s);return e;
}
static int busy_range(drv_dma_context_t *c,dma_storage_t *s,uint64_t off,uint64_t length) {
    if(s->device)return 1;
    if(!s->busy)return 0;
    for(dma_storage_t *r=c->storages;r;r=r->next)for(unsigned i=0;i<r->binding_count;i++) {
        dma_binding_t *b=&r->bindings[i];
        if(b->owned && b->target==s->object &&
           (!c->policy.endpoints[r->endpoint].streaming || (off<b->offset+b->length && b->offset<off+length)))return 1;
    }
    return 0;
}
static int slot_owned(dma_storage_t *s,const drv_dma_grant_t *g,unsigned index) {
    for(unsigned f=0;f<g->field_count;f++)if(s->bindings[index*g->field_count+f].owned)return 1;
    return 0;
}
static int access(drv_dma_context_t *c,dma_storage_t *s,uint64_t off,unsigned width,int write,uint64_t *value) {
    const drv_dma_grant_t *g=&c->policy.grants[s->grant];
    if(!width_valid(width) || !range(s->size,off,width))return DRV_BOUNDS;
    if(!(g->rights&(write?DRV_WRITE:DRV_READ)))return DRV_DENIED;
    if(!(g->widths&width))return DRV_DENIED;
    if(g->kind==DRV_DMA_RING) {
        if(s->device && (!c->policy.endpoints[s->endpoint].streaming || slot_owned(s,g,off/g->stride)))return DRV_BUSY;
        unsigned f;for(f=0;f<g->field_count;f++)if(g->fields[f].offset==off%g->stride && g->fields[f].width==width)break;
        if(f==g->field_count || g->fields[f].kind==DRV_DMA_ADDRESS)return DRV_DENIED;
        if(write && (g->fields[f].kind!=DRV_DMA_SCALAR || (*value&~g->fields[f].write_mask)))return DRV_DENIED;
    } else if(busy_range(c,s,off,width))return DRV_BUSY;
    if(write){if(*value&~width_mask(width))return DRV_BOUNDS;store((uint8_t *)s->cpu+off,width,*value);}
    else *value=load((uint8_t *)s->cpu+off,width);
    return 0;
}
static int bind(drv_dma_context_t *c,dma_storage_t *s,uint64_t target,uint64_t descriptor,unsigned field,uint64_t off,uint64_t length) {
    const drv_dma_grant_t *g=&c->policy.grants[s->grant];dma_storage_t *t=lookup(c,target);
    if(g->kind!=DRV_DMA_RING || field>=g->field_count || descriptor>=s->size/g->stride || g->fields[field].kind!=DRV_DMA_ADDRESS)return DRV_DENIED;
    if(!t || c->policy.grants[t->grant].kind!=DRV_DMA_BUFFER)return DRV_DENIED;
    if(s->device || s->busy || t->device || t->busy)return DRV_BUSY;
    if(!(g->rights&DRV_WRITE))return DRV_DENIED;
    if(!length || !range(t->size,off,length))return DRV_BOUNDS;
    const drv_dma_field_t *a=&g->fields[field],*l=&g->fields[a->length_field];
    if(length>width_mask(l->width))return DRV_BOUNDS;
    dma_binding_t *b=&s->bindings[descriptor*g->field_count+field];
    ora_ref_t object=ora_retain(object_for(c,target));if(!object)return DRV_NOMEM;
    ora_ref_t old=b->target;*b=(dma_binding_t){.target=object,.offset=off,.length=length};
    store((uint8_t *)s->cpu+descriptor*g->stride+a->offset,8,t->bus+off);
    store((uint8_t *)s->cpu+descriptor*g->stride+l->offset,l->width,length);
    ora_release(old);return 0;
}
static int publish(drv_dma_context_t *c,dma_storage_t *s,unsigned endpoint) {
    if(endpoint>=c->policy.endpoint_count || c->policy.endpoints[endpoint].grant!=s->grant)return DRV_DENIED;
    if(!(c->policy.grants[s->grant].rights&DRV_WRITE))return DRV_DENIED;
    if(s->device || s->busy)return DRV_BUSY;
    /* One programmed ring per endpoint, including failed partial submissions. */
    for(unsigned i=0;i<DRV_DMA_OBJECTS;i++)if(c->slots[i].object) {
        dma_storage_t *other=ora_storage_state(c->slots[i].object);
        if(other->device && other->endpoint==endpoint)return DRV_BUSY;
    }
    const drv_dma_grant_t *g=&c->policy.grants[s->grant];
    const drv_dma_endpoint_t *e=&c->policy.endpoints[endpoint];
    uint64_t slots=s->size/g->stride;
    if(e->doorbell_value>=slots || (e->min_slots && slots<e->min_slots) || (e->max_slots && slots>e->max_slots))return DRV_BOUNDS;
    for(unsigned i=0;i<s->binding_count;i++)if(g->fields[i%g->field_count].kind==DRV_DMA_ADDRESS) {
        dma_binding_t *b=&s->bindings[i];dma_storage_t *t=b->target?ora_storage_state(b->target):NULL;
        if(!t || t->device || t->busy)return DRV_BUSY;
        if(e->streaming && b->length<e->min_span)return DRV_BOUNDS;
    }
    /* Set ownership BEFORE any register write. Failure may already have made
     * hardware observe a pointer: keep every ORA reference until proven stop. */
    s->device=1;s->endpoint=endpoint;s->generation=c->hardware_generation;
    for(unsigned i=0;i<s->binding_count;i++)if(s->bindings[i].target && (!e->streaming || e->initial_owned)) {
        s->bindings[i].owned=1;((dma_storage_t *)ora_storage_state(s->bindings[i].target))->busy++;
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    unsigned ids[]={e->low,e->high,e->length,e->doorbell};
    uint64_t values[]={s->bus&UINT32_MAX,s->bus>>32,s->size,e->doorbell_value};
    for(unsigned i=0;i<4;i++) {const drv_dma_register_t *r=&c->policy.registers[ids[i]];
        int error=c->provider.reg_write(c->provider.context,r->offset,r->width,values[i]);if(error)return error;}
    return 0;
}
static int reclaim(drv_dma_context_t *c,dma_storage_t *s) {
    if(!s->device)return DRV_DENIED;
    int e=c->provider.quiesced(c->provider.context,s->endpoint);if(e)return e;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const drv_dma_grant_t *g=&c->policy.grants[s->grant];
    /* Device-written length/status is untrusted even in an allowed buffer.
     * Never expose an address scribbled by hardware back to package code. */
    int corrupt=0;
    for(unsigned i=0;i<s->binding_count;i++)if(s->bindings[i].target) {
        dma_binding_t *b=&s->bindings[i];dma_storage_t *t=ora_storage_state(b->target);
        const drv_dma_field_t *a=&g->fields[i%g->field_count],*l=&g->fields[a->length_field];
        uint8_t *desc=(uint8_t *)s->cpu+(i/g->field_count)*g->stride;
        if(load(desc+a->offset,8)!=t->bus+b->offset || load(desc+l->offset,l->width)>b->length)corrupt=1;
        if(b->owned){t->busy--;b->owned=0;}
        store(desc+a->offset,8,t->bus+b->offset);
        if(corrupt)store(desc+l->offset,l->width,0);
    }
    s->device=0;return corrupt?DRV_IO:0;
}
static int slot_acquire(drv_dma_context_t *c,dma_storage_t *s,uint64_t index,uint64_t *out) {
    const drv_dma_grant_t *g=&c->policy.grants[s->grant];
    if(g->kind!=DRV_DMA_RING || !s->device || index>=s->size/g->stride)return DRV_DENIED;
    const drv_dma_endpoint_t *e=&c->policy.endpoints[s->endpoint];if(!e->streaming)return DRV_DENIED;
    if(s->generation!=c->hardware_generation)return DRV_DENIED;
    uint8_t *desc=(uint8_t *)s->cpu+index*g->stride;
    const drv_dma_field_t *status=&g->fields[e->completion_field];
    if(slot_owned(s,g,index)) {
        if((device_load(desc+status->offset,status->width)&e->completion_mask)!=e->completion_value)return DRV_AGAIN;
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    }
    int corrupt=0;uint64_t length=0;
    for(unsigned f=0;f<g->field_count;f++)if(g->fields[f].kind==DRV_DMA_ADDRESS) {
        dma_binding_t *b=&s->bindings[index*g->field_count+f];dma_storage_t *t=ora_storage_state(b->target);
        const drv_dma_field_t *a=&g->fields[f],*l=&g->fields[a->length_field];
        uint64_t n=device_load(desc+l->offset,l->width);
        if(device_load(desc+a->offset,8)!=t->bus+b->offset || n>b->length)corrupt=1;
        if(b->owned){t->busy--;b->owned=0;}length=n;
    }
    if(corrupt)return DRV_IO;*out=length;return 0;
}
int drv_dma_quiesce_all(drv_dma_context_t *c) {
    if(!c || !c->epoch)return DRV_NOT_BOUND;
    int result=0;
    for(dma_storage_t *s=c->storages;s;s=s->next)if(s->device){int e=reclaim(c,s);if(e && !result)result=e;}
    return result;
}
static int slot_post(drv_dma_context_t *c,dma_storage_t *s,uint64_t index,uint64_t length) {
    const drv_dma_grant_t *g=&c->policy.grants[s->grant];
    if(g->kind!=DRV_DMA_RING || !s->device || index>=s->size/g->stride)return DRV_DENIED;
    const drv_dma_endpoint_t *e=&c->policy.endpoints[s->endpoint];
    if(!e->streaming || index!=s->post_cursor || slot_owned(s,g,index))return DRV_BUSY;
    if(s->generation!=c->hardware_generation)return DRV_DENIED;
    uint8_t *desc=(uint8_t *)s->cpu+index*g->stride;
    for(unsigned f=0;f<g->field_count;f++)if(g->fields[f].kind==DRV_DMA_ADDRESS) {
        dma_binding_t *b=&s->bindings[index*g->field_count+f];dma_storage_t *t=ora_storage_state(b->target);
        if(!t || !length || length>b->length)return DRV_BOUNDS;
        if(busy_range(c,t,b->offset,b->length))return DRV_BUSY;
        if(device_load(desc+g->fields[f].offset,8)!=t->bus+b->offset)return DRV_IO;
    }
    for(unsigned f=0;f<g->field_count;f++) {
        const drv_dma_field_t *a=&g->fields[f];
        if(a->kind==DRV_DMA_STATUS)store(desc+a->offset,a->width,0);
        if(a->kind==DRV_DMA_LENGTH)store(desc+a->offset,a->width,e->length_output?0:length);
        if(a->kind==DRV_DMA_ADDRESS) {
            dma_binding_t *b=&s->bindings[index*g->field_count+f];b->owned=1;((dma_storage_t *)ora_storage_state(b->target))->busy++;
        }
    }
    unsigned next=(index+1)%(s->size/g->stride);s->post_cursor=next;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    const drv_dma_register_t *r=&c->policy.registers[e->doorbell];
    return c->provider.reg_write(c->provider.context,r->offset,r->width,e->post_next?next:index);
}
int drv_dma_operation(drv_dma_context_t *c,unsigned op,const uint64_t a[8],uint64_t *out,uint8_t *scratch,size_t scratch_size) {
    if(!c || !c->epoch || !a || !out)return DRV_FORMAT;
    if(op==DRV_DMA_ALLOC) {
        if(a[0]>UINT32_MAX)return DRV_BOUNDS;
        return allocate(c,(unsigned)a[0],a[1],out);
    }
    dma_storage_t *s=lookup(c,a[0]);if(!s)return DRV_NOT_BOUND;
    uint64_t result=0;int e;
    switch(op) {
    case DRV_DMA_RELEASE:
        if(s->device || s->busy)return DRV_BUSY;
        for(unsigned i=0;i<DRV_DMA_OBJECTS;i++)if(c->slots[i].token==a[0]){ora_ref_t object=c->slots[i].object;c->slots[i]=(drv_dma_slot_t){0};ora_release(object);break;}
        break;
    case DRV_DMA_READ:case DRV_DMA_WRITE:
        if(a[2]>UINT32_MAX)return DRV_BOUNDS;
        result=a[3];
        e=access(c,s,a[1],a[2],op==DRV_DMA_WRITE,&result);if(e)return e;break;
    case DRV_DMA_BIND:
        if(a[3]>UINT32_MAX)return DRV_BOUNDS;
        e=bind(c,s,a[1],a[2],a[3],a[4],a[5]);if(e)return e;break;
    case DRV_DMA_PUBLISH:
        if(a[1]>UINT32_MAX)return DRV_BOUNDS;
        e=publish(c,s,a[1]);if(e)return e;break;
    case DRV_DMA_RECLAIM:e=reclaim(c,s);if(e)return e;break;
    case DRV_DMA_SLOT_ACQUIRE:e=slot_acquire(c,s,a[1],&result);if(e)return e;break;
    case DRV_DMA_SLOT_POST:e=slot_post(c,s,a[1],a[2]);if(e)return e;break;
    case DRV_DMA_SLOT_STATUS: {
        const drv_dma_grant_t *g=&c->policy.grants[s->grant];
        if(g->kind!=DRV_DMA_RING || !s->device || a[1]>=s->size/g->stride)return DRV_DENIED;
        const drv_dma_endpoint_t *endpoint=&c->policy.endpoints[s->endpoint];
        if(!endpoint->streaming || s->generation!=c->hardware_generation)return DRV_DENIED;
        const drv_dma_field_t *field=&g->fields[endpoint->completion_field];
        result=device_load((uint8_t *)s->cpu+a[1]*g->stride+field->offset,field->width);break;
    }
    case DRV_DMA_COPY_IN:case DRV_DMA_COPY_OUT: {
        int write=op==DRV_DMA_COPY_IN;const drv_dma_grant_t *g=&c->policy.grants[s->grant];
        if(g->kind!=DRV_DMA_BUFFER || !(g->rights&(write?DRV_WRITE:DRV_READ)))return DRV_DENIED;
        if(!scratch || !range(s->size,a[1],a[3]) || !range(scratch_size,a[2],a[3]))return DRV_BOUNDS;
        if(busy_range(c,s,a[1],a[3]))return DRV_BUSY;
        if(write)memcpy((uint8_t *)s->cpu+a[1],scratch+a[2],a[3]);
        else memcpy(scratch+a[2],(uint8_t *)s->cpu+a[1],a[3]);
        break;
    }
    default:return DRV_UNSUPPORTED;
    }
    *out=result;return 0;
}
static const drv_dma_register_t *reg(drv_dma_context_t *c,unsigned resource,unsigned width,uint64_t off) {
    if(!c || !c->epoch || resource)return NULL;
    for(unsigned i=0;i<c->policy.register_count;i++) {
        const drv_dma_register_t *r=&c->policy.registers[i];unsigned stride=r->stride?r->stride:r->width;
        if(off>=r->offset && off<reg_end(r) && (off-r->offset)%stride==0 && r->width==width)return r;
    }
    return NULL;
}
int drv_dma_register_read(drv_dma_context_t *c,unsigned resource,unsigned width,uint64_t off,uint64_t *out) {
    const drv_dma_register_t *r=reg(c,resource,width,off);
    if(!out || !r || r->kind==DRV_REG_ADDRESS_LOW || r->kind==DRV_REG_ADDRESS_HIGH || !(r->rights&DRV_READ))return DRV_DENIED;
    return c->provider.reg_read?c->provider.reg_read(c->provider.context,off,width,out):DRV_UNSUPPORTED;
}
int drv_dma_register_write(drv_dma_context_t *c,unsigned resource,unsigned width,uint64_t off,uint64_t value) {
    const drv_dma_register_t *r=reg(c,resource,width,off);
    if(!r || r->kind!=DRV_REG_SCALAR || !(r->rights&DRV_WRITE) || (value&~r->write_mask))return DRV_DENIED;
    if(r->block_endpoint)for(dma_storage_t *s=c->storages;s;s=s->next)
        if(s->device && s->endpoint==r->block_endpoint-1)return DRV_DENIED;
    if(value&r->guard_mask) {
        unsigned ready=0;
        for(dma_storage_t *s=c->storages;s;s=s->next)if(s->device && s->endpoint==r->require_endpoint && s->generation==c->hardware_generation)ready=1;
        if(!ready)return DRV_DENIED;
    }
    if(value&r->invalidate_mask){if(c->hardware_generation==UINT64_MAX)return DRV_NOMEM;c->hardware_generation++;}
    return c->provider.reg_write?c->provider.reg_write(c->provider.context,off,width,value):DRV_UNSUPPORTED;
}
static int vm_register(drv_dma_context_t *c,unsigned resource,unsigned width,uint64_t off,int write,uint64_t *v) {
    if(resource>=c->request_count)return DRV_DENIED;
    const drv_requirement_t *r=&c->requests[resource];
    if(r->kind!=DRV_MMIO || !(r->rights&(write?DRV_WRITE:DRV_READ)) || !(r->widths&width) ||
       !range(r->length,off,width))return DRV_DENIED;
    return write?drv_dma_register_write(c,0,width,r->offset+off,*v):drv_dma_register_read(c,0,width,r->offset+off,v);
}
static int vm_read(void *c,unsigned r,unsigned w,uint64_t off,uint64_t *v){return vm_register(c,r,w,off,0,v);}
static int vm_write(void *c,unsigned r,unsigned w,uint64_t off,uint64_t v){return vm_register(c,r,w,off,1,&v);}
static int vm_dma(void *context,unsigned op,const uint64_t in[8],uint64_t *out,uint8_t *scratch,size_t n) {
    drv_dma_context_t *c=context;
    if(op==DRV_DMA_ALLOC) {
        if(in[0]>=c->policy.grant_count || !(c->allowed_grants&(1u<<in[0])))return DRV_DENIED;
    } else {
        dma_storage_t *s=lookup(c,in[0]);
        if(!s)return DRV_NOT_BOUND;
        if(!(c->allowed_grants&(1u<<s->grant)))return DRV_DENIED;
        if(op==DRV_DMA_BIND) {
            dma_storage_t *t=lookup(c,in[1]);
            if(!t || !(c->allowed_grants&(1u<<t->grant)))return DRV_DENIED;
        }
        if(op==DRV_DMA_PUBLISH || op==DRV_DMA_SLOT_POST) {
            unsigned writes=0;
            for(unsigned i=0;i<c->request_count;i++)if(c->requests[i].kind==DRV_MMIO && (c->requests[i].rights&DRV_WRITE))writes++;
            if(!writes)return DRV_DENIED;
            unsigned endpoint=op==DRV_DMA_PUBLISH?in[1]:s->endpoint;
            if(endpoint>=c->policy.endpoint_count)return DRV_DENIED;
            const drv_dma_endpoint_t *e=&c->policy.endpoints[endpoint];
            unsigned ids[]={e->low,e->high,e->length,e->doorbell};
            for(unsigned j=0;j<4;j++) {
                const drv_dma_register_t *reg=&c->policy.registers[ids[j]];unsigned permitted=0;
                for(unsigned i=0;i<c->request_count;i++) {
                    const drv_requirement_t *r=&c->requests[i];
                    if(r->kind==DRV_MMIO && (r->rights&DRV_WRITE) && (r->widths&reg->width) && reg->offset>=r->offset &&
                       range(r->length,reg->offset-r->offset,reg->width))permitted=1;
                }
                if(!permitted)return DRV_DENIED;
            }
        }
    }
    return drv_dma_operation(c,op,in,out,scratch,n);
}
int drv_dma_attach_io(drv_dma_context_t *c,const drv_image_t *image,uint64_t (*clock)(void *),drv_io_t *io) {
    if(!c || !c->epoch || !image || !clock || !io)return DRV_FORMAT;
    if(c->attached || c->storages)return DRV_BUSY;
    const drv_manifest_t *m=&image->manifest;
    if(m->engine_min!=2 || !(m->features&DRV_F_DMA) || !m->resource_count || m->resource_count>DRV_RES_MAX)return DRV_DENIED;
    unsigned allowed=0,mmio=0;drv_dma_policy_t narrowed=c->policy;
    uint64_t reg_extent=0;
    for(unsigned i=0;i<c->policy.register_count;i++) {
        uint64_t end=reg_end(&c->policy.registers[i]);
        if(end>reg_extent)reg_extent=end;
    }
    for(unsigned i=0;i<m->resource_count;i++) {
        const drv_requirement_t *r=&m->resources[i];
        if(!r->rights || (r->rights&~3u) || !r->widths || (r->widths&~15u) || !r->length || r->reserved)return DRV_DENIED;
        if(r->kind==DRV_DMA) {
            if(r->selector>=c->policy.grant_count || r->offset || (allowed&(1u<<r->selector)))return DRV_DENIED;
            drv_dma_grant_t *g=&narrowed.grants[r->selector];
            if(r->length>g->max_bytes || (r->rights&~g->rights) || (r->widths&~g->widths))return DRV_DENIED;
            g->max_bytes=r->length;g->rights=r->rights;g->widths=r->widths;allowed|=1u<<r->selector;
        } else if(r->kind==DRV_MMIO) {
            if(r->selector || mmio++ || !(m->features&DRV_F_MMIO) || r->offset>reg_extent || r->length>reg_extent-r->offset)return DRV_DENIED;
        } else return DRV_DENIED; /* Never mix raw PIO callbacks into this path. */
    }
    if(!allowed)return DRV_DENIED;
    c->policy=narrowed;c->allowed_grants=allowed;c->request_count=m->resource_count;c->attached=1;
    memcpy(c->requests,m->resources,m->resource_count*sizeof(*c->requests));
    *io=(drv_io_t){.context=c,.read=vm_read,.write=vm_write,.clock_ns=clock,.dma=vm_dma};
    return 0;
}
