/* Raw legacy instructions are intentionally kept here: this is the exact old
   wire contract, independent of the migrated native convenience wrappers. */
static uint64_t vk_old_call(uint64_t n,uint64_t a,uint64_t b,uint64_t c) {
    uint64_t result;
    __asm__ volatile("syscall" : "=a"(result) : "a"(n),"D"(a),"S"(b),"d"(c)
                     : "rcx","r11","memory");
    return result;
}
static int vk_syscall_domain_smoke(void) {
    vex_bulk_result_t result={0};
    vex_bulk_req_t bulk={VEX_BULK_MEMORY|VEX_BULK_PID|VEX_BULK_ABI,&result};
    char read_byte='x';
    const char *text="vk_test: legacy SYS_WRITE=1 executes its string contract\n";
    if(VEX_SYS_BULK!=1u||vk_old_call(1u,(uint64_t)(uintptr_t)text,0u,0u)!=0u||
       vex_syscall1(1u,(uint64_t)(uintptr_t)&bulk)!=0u||
       result.valid_mask!=bulk.mask||result.pid!=(uint32_t)getpid()||
       result.abi!=0u||result.total_pages==0u||
       result.used_pages+result.free_pages!=result.total_pages)return 0;
    if(vk_old_call(3u,(uint64_t)(uintptr_t)&read_byte,1u,0u)!=0u||read_byte!='\0'||
       vk_old_call(VEX_SYS_GETPID,0u,0u,0u)!=(uint64_t)getpid())return 0;
    /* Same address, different domains: neither guesses from its contents. */
    vex_bulk_result_t saved=result;
    if(vk_old_call(1u,(uint64_t)(uintptr_t)&bulk,0u,0u)!=0u||
       memcmp(&saved,&result,sizeof(result))!=0||
       vex_syscall1(1u,(uint64_t)(uintptr_t)text)!=(uint64_t)-1)return 0;
    uint64_t bad[]={0u,0x800000000000ull,0xffff800000000000ull,UINT64_MAX};
    for(uint32_t i=0u;i<4u;++i) {
        if(vex_syscall1(1u,bad[i])!=(uint64_t)-1||
           vk_old_call(1u,bad[i],0u,0u)!=(uint64_t)-1||
           vk_old_call(3u,bad[i],1u,0u)!=(uint64_t)-1)return 0;
    }
    bulk.result=(vex_bulk_result_t *)(uintptr_t)bad[2];
    if(vex_syscall1(1u,(uint64_t)(uintptr_t)&bulk)!=(uint64_t)-1||
       vex_syscall0(2u)!=(uint64_t)-1||
       vex_syscall0(0xffffffffu)!=(uint64_t)-1||
       vk_old_call(0x12345678u,0u,0u,0u)!=(uint64_t)-1||
       vk_old_call(0x5645584f00000001ull,0u,0u,0u)!=(uint64_t)-1)return 0;
    /* Legacy exit is tested in an independently owned child, not disabled. */
    int child=fork();if(child<0)return 0;
    if(child==0){(void)vk_old_call(2u,0u,0u,0u);exit(99);}
    int status=-1;if(waitpid(child,&status,0)!=child||status!=0)return 0;
    vex_process_spawn_req_t spawn={0};spawn.path="/usr/linux-tests/linux_native_domain_smoke";
    if(vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uint64_t)(uintptr_t)&spawn)!=0u||spawn.pid==0u||
       waitpid((int)spawn.pid,&status,0)!=(int)spawn.pid||status!=0)return 0;
    write_text("\nvk_test: native BULK=1 / legacy WRITE=1, READ/EXIT, old native numbers, invalid domains/pointers and unknown calls ok\n");
    return 1;
}

/* More cumulative payload than MRAC's 768 MiB kernel-VA window, with only one
   4 MiB live allocation. A rising virtual cursor must not cause false OOM. */
static int vk_native_heap_extent_smoke(void){
    const uint64_t bytes=4u*1024u*1024u;
    for(uint32_t i=0u;i<200u;++i){
        uint8_t *p=(uint8_t *)(uintptr_t)vex_syscall1(VEX_SYS_HEAP_ALLOC,bytes);
        if(p==NULL)return 0;
        if(p[0]!=0u||p[bytes-1u]!=0u)return 0;
        p[0]=(uint8_t)i;p[bytes-1u]=(uint8_t)~i;
        vex_heap_req_t release={p,bytes};
        if(vex_syscall1(VEX_SYS_HEAP_FREE,(uint64_t)(uintptr_t)&release)!=0u)return 0;
    }
    write_text("vk_test: 800 MiB cumulative native heap churn exceeds MRAC VA window with 4 MiB live; recycled extents/zeroing/free ok\n");
    return 1;
}

/* Finite regressions without the global harness's unrelated /webkit launch. */
static int vk_syscall_regression_smoke(void) {
    static const struct {const char *path;int status;} tests[]={
        {"/home/root/bin/security_boundary_test",0},
        {"/home/root/bin/vex_event_fault_test",0},
        /* These legacy fixtures deliberately return 7 and 9 on success. */
        {"/usr/linux-tests/linux_static_smoke",7},
        {"/usr/linux-tests/linux_static_pie_smoke",9},
        {"/usr/linux-tests/linux_thread_context_smoke",0},
        {"/usr/linux-tests/linux_futex_smoke",0},
        {"/usr/linux-tests/linux_memory_ownership_smoke",0},
        {"/usr/linux-tests/linux_elf_protect_edges",0},
        {"/usr/linux-tests/linux_poll_fd_smoke",0},
        {"/usr/linux-tests/wpe_renderer_fd_test",0},
        {"/usr/linux-tests/linux_file_mmap_smoke",0},
        {"/usr/linux-tests/linux_unsupported_smoke",0},
        {"/usr/linux-tests/linux_pipe_exec_smoke",0},
        {"/usr/linux-tests/linux_seqpacket_smoke",0},
        {"/usr/linux-tests/linux_seqpacket_fdpass_smoke",0},
        {"/usr/linux-tests/linux_fault_signal_smoke",0},
        {"/usr/linux-tests/linux_fault_unhandled_smoke",139},
        {"/usr/linux-tests/linux_jit_wx_smoke",0},
        {"/usr/linux-tests/linux_dynamic_tls_smoke",0},
        {"/usr/linux-tests/glibc_probe",0}
    };
    for(uint32_t i=0u;i<sizeof(tests)/sizeof(tests[0]);++i){
        vex_process_spawn_req_t req={0};req.path=tests[i].path;int status=-1;
        if(vex_syscall1(VEX_SYS_PROCESS_SPAWN,(uint64_t)(uintptr_t)&req)!=0u||req.pid==0u||
           waitpid((int)req.pid,&status,0)!=(int)req.pid||status!=tests[i].status){
            write_text("vk_test: syscall regression FAILED ");write_text(tests[i].path);
            write_hex32(" status=",(uint32_t)status);return 0;
        }
        write_text("vk_test: syscall regression verified ");write_text(tests[i].path);write_text("\n");
    }
    return 1;
}
