/* Included after the object inventory gate. Service state is never copied
   into a child; only its native endpoint capability is duplicated/delegated. */
typedef struct remote_wait {struct remote_wait *next;uint32_t *cancel;int fd;} remote_wait;
static remote_wait *remote_waits;
static void event_close_fd(int fd){if(fd>=0)vex_syscall1(VEX_SYS_CLOSE,fd);}
static unsigned event_error(int n){switch(-n){case 12:case 24:return 8;case 13:return 5;case 14:return 998;case 22:return 87;default:return 6;}}
static void event_cancel_fd(int fd){vex_rendezvous_req_t q={.size=sizeof(q),.operation=VEX_RENDEZVOUS_CANCEL_ENDPOINT,.resource_fd=fd};vex_syscall1(VEX_SYS_RENDEZVOUS,(uintptr_t)&q);}
static unsigned event_rpc(int fd,we_packet *p,uint32_t *cancel,int *received){
    if(received)*received=-1;int reply[2];vex_message_pair_req_t pair={reply,0,0};int n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_PAIR,(uintptr_t)&pair);if(n)return event_error(n);
    vex_message_attachment_req_t a={(unsigned)reply[1],VEX_MESSAGE_OBJECT_FD,VEX_MESSAGE_ATTACHMENT_DUP};
    vex_message_io_req_t q={fd,0,p,sizeof(*p),&a,1};n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_SEND,(uintptr_t)&q);event_close_fd(reply[1]);
    if(n!=(int)sizeof(*p)){event_close_fd(reply[0]);return n<0?event_error(n):6;}
    remote_wait registration={.cancel=cancel,.fd=reply[0]};
    if(cancel){wr_lock(&gate);registration.next=remote_waits;remote_waits=&registration;if(__atomic_load_n(cancel,__ATOMIC_ACQUIRE))event_cancel_fd(reply[0]);wr_unlock(&gate);}
    a=(vex_message_attachment_req_t){UINT64_MAX,VEX_MESSAGE_OBJECT_FD,0};q=(vex_message_io_req_t){reply[0],0,p,sizeof(*p),&a,1};
    n=(int)(int64_t)vex_syscall1(VEX_SYS_MESSAGE_RECV,(uintptr_t)&q);
    if(cancel){wr_lock(&gate);remote_wait **link=&remote_waits;while(*link && *link!=&registration)link=&(*link)->next;if(*link)*link=registration.next;wr_unlock(&gate);}
    event_close_fd(reply[0]);if(n!=(int)sizeof(*p)){if(n>=0 && q.attachment_count)event_close_fd((int)a.handle);return n<0?event_error(n):6;}
    if(q.attachment_count){if(received)*received=(int)a.handle;else event_close_fd((int)a.handle);}return p->error;
}
static int shared_event_create(unsigned manual,unsigned initial,unsigned *error){
    vex_fs_root_req_t open={.size=sizeof(open),.operation=VEX_FS_ROOT_OPEN,.fd=wr_system_root(),.path="event-service",.flags=O_RDONLY|O_CLOEXEC|VEX_FS_OPEN_REGULAR|VEX_FS_OPEN_EXECUTABLE};
    int image=(int)(int64_t)vex_syscall1(VEX_SYS_FS_ROOT,(uintptr_t)&open);if(image<0){*error=event_error(image);return -1;}
    vex_process_resource_req_t launch={.size=sizeof(launch),.operation=VEX_PROCESS_ULTRA_OPEN,.fd=image,.status=wr_prefix_root()};
    int service=(int)(int64_t)vex_syscall1(VEX_SYS_PROCESS_RESOURCE,(uintptr_t)&launch);event_close_fd(image);if(service<0){*error=event_error(service);return -1;}
    we_packet p={.operation=WE_CREATE,.manual=manual,.initial=initial};int result=-1;*error=event_rpc(service,&p,NULL,&result);event_close_fd(service);
    if(*error){event_close_fd(result);return -1;}if(result<0)*error=6;return result;
}
