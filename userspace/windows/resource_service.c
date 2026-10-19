#include "runtime.h"
#include "resource_protocol.h"
#include <unos/vex_abi.h>
#include "vex_syscall.h"
static int call(unsigned n,void *p){return (int)(int64_t)vex_syscall1(n,(uintptr_t)p);}
static void close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,fd);}
int wr_main(uint64_t argc,char **argv){(void)argc;(void)argv;
    static uint8_t bytes[65536];
    for(;;){vex_message_attachment_req_t a[32];
        vex_message_auth_req_t r={.io={8,0,bytes,sizeof(bytes),a,32}};
        int n=call(VEX_SYS_MESSAGE_RECV_AUTH,&r);if(n<=0)return n?125:0;
        if(n!=sizeof(kr_open) || r.io.attachment_count!=2){for(unsigned i=0;i<r.io.attachment_count;i++)close_fd((int)a[i].handle);continue;}
        kr_open p;memcpy(&p,bytes,sizeof(p));
        int root=(int)a[0].handle,reply=(int)a[1].handle;
        /* Attenuate the reply: a forged resource can never be used to receive
           or revoke. The root capability itself is the native authority. */
        vex_rendezvous_req_t limit={.size=sizeof(limit),.operation=VEX_RENDEZVOUS_ATTENUATE,.resource_fd=reply,.rights=1};
        int sender=call(VEX_SYS_RENDEZVOUS,&limit);close_fd(reply);
        int fd=-1,valid=!p.result;p.result=-22;
        vex_fs_root_info_t info;vex_fs_root_req_t query={.size=sizeof(query),.operation=VEX_FS_ROOT_QUERY,.fd=root,.out=&info};
        unsigned terminated=0;for(unsigned i=0;i<sizeof(p.path);i++)if(!p.path[i]){terminated=1;break;}
        if(sender>=0 && valid && p.magic==KR_MAGIC && !p.reserved && terminated &&
           p.flags==(O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_PROTECTED)) {
            int error=call(VEX_SYS_FS_ROOT,&query);
            if(error)p.result=error;
            else if(info.rights!=(VEX_FS_READ|VEX_FS_TRANSFER))p.result=-13;
            else{vex_fs_root_req_t open={.size=sizeof(open),.operation=VEX_FS_ROOT_OPEN,.fd=root,.path=p.path,.flags=p.flags};
                fd=call(VEX_SYS_FS_ROOT,&open);p.result=fd<0?fd:0;}
        }
        close_fd(root);
        if(sender>=0){vex_message_attachment_req_t out={(unsigned)fd,VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
            vex_message_io_req_t q={sender,MSG_DONTWAIT,&p,sizeof(p),fd>=0?&out:0,fd>=0?1:0};call(VEX_SYS_MESSAGE_SEND,&q);close_fd(sender);}
        close_fd(fd);
    }
}
