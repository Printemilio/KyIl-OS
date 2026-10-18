/* Shared native WebKit startup. No browser UI or navigation policy here. */
#ifndef KYIL_WEBKIT_SESSION_H
#define KYIL_WEBKIT_SESSION_H
static int ensure_runtime_directory(const char *path){
    if((int64_t)vex_syscall2(VEX_SYS_ACCESS,(uintptr_t)path,0)==0)return 0;
    vex_mkdir_req_t request={path,0700u};
    if((int64_t)vex_syscall1(VEX_SYS_MKDIR,(uintptr_t)&request)==0)return 0;
    return (int64_t)vex_syscall2(VEX_SYS_ACCESS,(uintptr_t)path,0)==0?0:-1;
}
static int prepare_webkit_runtime(void){
    static const char *directories[]={
        "/tmp/d",
        "/tmp/d/wpe",
        "/tmp/d/wpe/storage",
        "/tmp/s",
        "/home/root/.config",
        "/tmp/c",
        "/tmp/c/fontconfig",
        "/tmp/c/wpe"
    };
    for(unsigned i=0;i<sizeof(directories)/sizeof(directories[0]);i++){
        if(ensure_runtime_directory(directories[i])!=0){
            log_text("browser supervisor: cannot create WebKit runtime directory: ");
            log_text(directories[i]);log_text("\n");return -1;
        }
    }
    log_text("browser supervisor: WebKit runtime directories ready\n");
    return 0;
}
static int webkit_session_spawn(int control, int *child_control, int *diagnostics){int logs[2];vex_pipe_req_t p={logs};int64_t result=(int64_t)vex_syscall1(VEX_SYS_PIPE,(uintptr_t)&p);if(result){log_text("browser supervisor: child log pipe failed\n");return -1;}result=(int64_t)vex_syscall3(VEX_SYS_FCNTL,logs[0],4,0x800);if(result){vex_syscall1(VEX_SYS_CLOSE,logs[0]);vex_syscall1(VEX_SYS_CLOSE,logs[1]);log_text("browser supervisor: child log pipe CLOEXEC setup failed\n");return -1;}
    int64_t pid=vex_syscall0(VEX_SYS_PROCESS_FORK);
    if(!pid){vex_syscall1(VEX_SYS_CLOSE,logs[0]);vex_syscall1(VEX_SYS_CLOSE,control);
        if((int64_t)vex_syscall2(VEX_SYS_DUP2,logs[1],1)!=1 || (int64_t)vex_syscall2(VEX_SYS_DUP2,logs[1],2)!=2){vex_syscall1(VEX_SYS_PROCESS_EXIT,127);return -1;}
        vex_syscall1(VEX_SYS_CLOSE,logs[1]);
        char *env[]={
            "HOME=/home/root",
            /* The Linux WebKit storage path appends two opaque origin hashes.
             * Keep its real persistent VexFS location short enough for the
             * current 128-byte VexFS path contract. */
            "XDG_DATA_HOME=/tmp/d",
            "XDG_CONFIG_HOME=/home/root/.config",
            "XDG_STATE_HOME=/tmp/s",
            "XDG_CACHE_HOME=/tmp/c",
            "XDG_DATA_DIRS=/usr/share:/usr/local/share",
            "XDG_RUNTIME_DIR=/tmp",
            "FONTCONFIG_PATH=/etc/fonts",
            "SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt",
            "SSL_CERT_DIR=/etc/ssl/certs",
            "CURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt",
            "LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib",
            "WEBKIT_EXEC_PATH=/usr/local/libexec/wpe-webkit-2.0",
            "WPE_BACKEND_LIBRARY=/lib/libvex_wpe_backend.so",
            "WEBKIT_DISABLE_COMPOSITING_MODE=1",
            "LIBGL_ALWAYS_SOFTWARE=true",
            "GALLIUM_DRIVER=softpipe",
#ifdef VEX_BROWSER_FONT_DIAGNOSTIC
            "VEX_DIAG_FONT_TRACE=1",
#endif
#ifdef VEX_JSC_DIAGNOSTIC
            "VEX_JSC_DIAGNOSTIC=1",
#endif
            NULL};
        vex_process_exec_req_t e={"/home/root/bin/webkit_linux_main",0,env};
        result=(int64_t)vex_syscall1(VEX_SYS_PROCESS_EXEC,(uintptr_t)&e);vex_syscall1(VEX_SYS_PROCESS_EXIT,127);return (int)result;}
    if(pid<0){vex_syscall1(VEX_SYS_CLOSE,logs[0]);vex_syscall1(VEX_SYS_CLOSE,logs[1]);return -1;}
    vex_syscall1(VEX_SYS_CLOSE,logs[1]);vex_syscall1(VEX_SYS_CLOSE,(*child_control));(*child_control)=-1;
    *diagnostics=logs[0];return (int)pid;
}

#endif
