#include "../runtime.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
#include "ultra_probe.h"
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;
    char byte;vex_fd_io_req_t fd={0,&byte,1};int stdio=(int)(int64_t)vex_syscall1(VEX_SYS_READFD,(uintptr_t)&fd);
    if(stdio<0)stdio=-(int)vex_syscall0(VEX_SYS_LAST_ERROR);
    vex_open_ex_req_t path={VEX_AT_FDCWD,O_RDONLY,0,0,"/usr/bin/kyil-windows"};
    int ambient=(int)(int64_t)vex_syscall1(VEX_SYS_OPEN_EX,(uintptr_t)&path);
    int legacy=(int)(int64_t)vex_legacy_syscall0(1),gui=(int)(int64_t)vex_syscall1(VEX_SYS_APP_WINDOW_OPEN,0);
    for(;;){ultra_probe p;vex_message_attachment_req_t a={0,VEX_MESSAGE_OBJECT_FD,0};vex_message_auth_req_t r={.io={8,0,&p,sizeof(p),&a,1}};
        int n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV_AUTH,(uintptr_t)&r);if(!n)return 0;
        if(n!=(int)sizeof(p) || r.io.attachment_count!=1)return 1;
        p.authority=r.sender.scope_rights;p.pid=r.sender.pid;p.tid=r.sender.tid;p.uid=r.sender.uid;p.service=(unsigned)wr_pid();p.stdio=stdio;p.ambient=ambient;p.legacy=legacy;p.gui=gui;
        int exported[2]={-1,-1};vex_message_attachment_req_t lease={0,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
        vex_message_io_req_t send={(int)a.handle,0,&p,sizeof(p),0,0};
        if(p.value==ULTRA_EXPORT_PROBE){vex_message_pair_req_t pair={exported,0,0};
            if(vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair))return 2;
            lease.handle=(unsigned)exported[1];send.attachments=&lease;send.attachment_count=1;}
        if(vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&send)!=(uint64_t)sizeof(p))return 3;
        if(exported[0]>=0){vex_syscall1(VEX_SYS_CLOSE,exported[0]);vex_syscall1(VEX_SYS_CLOSE,exported[1]);}
        vex_syscall1(VEX_SYS_CLOSE,a.handle);
    }
}
