#include <unos/console.h>
#include <unos/gui.h>
#include <unos/app_surface.h>
#include <unos/scheduler.h>
#include <unos/random.h>
#include <unos/security.h>
#include <unos/shell.h>
#include <unos/gui_app_ipc.h>
#include <string.h>
#define CAPACITY 65536u
static char bytes[CAPACITY];
static uint32_t owner,window,head,count;
static uint64_t lease;
static uint8_t notified,overrun,busy;
static void notify(void) {
    if(owner && !notified && !app_surface_notify(owner,window,1u))notified=1;
}
static int permitted(uint32_t id,uint64_t token) {
    app_surface_t *surface=app_surface_find(id);
    return owner==scheduler_current_process_id() && window==id && token==lease && token &&
        surface && surface->owner_pid==owner && scheduler_current_uid()==0 && scheduler_current_has_capability(VEX_CAP_ALL);
}
int console_attach(uint32_t id,uint64_t *token) {
    GUI_GUARD;
    app_surface_t *surface=app_surface_find(id);
    if(!token || !surface || surface->owner_pid!=scheduler_current_process_id() ||
       surface->close_pending || scheduler_current_uid()!=0 || !scheduler_current_has_capability(VEX_CAP_ALL))return -13;
    if(owner){if(!app_surface_raise_owner(owner))return 1;return -16;}
    window=id;lease=kernel_random_u64();if(!lease)lease=1;
    notified=0;*token=lease;
    __atomic_store_n(&owner,surface->owner_pid,__ATOMIC_RELEASE);
    notify();return 0;
}
int console_read(uint32_t id,uint64_t token,char *out,size_t size,uint32_t *flags) {
    GUI_GUARD;
    if(!permitted(id,token))return -13;
    if(!out || !size || size>4096 || !flags)return -22;
    size_t n=count<size?count:size;
    size_t first=n<CAPACITY-head?n:CAPACITY-head;
    memcpy(out,bytes+head,first);if(n>first)memcpy(out+first,bytes,n-first);
    head=(head+n)%CAPACITY;count-=n;
    *flags=(busy?VEX_CONSOLE_BUSY:0)|(overrun?VEX_CONSOLE_OVERRUN:0);
    overrun=notified=0;if(count)notify();return (int)n;
}
int console_submit(uint32_t id,uint64_t token,const char *input,size_t size) {
    GUI_GUARD;
    if(!permitted(id,token))return -13;
    if(busy)return -16;
    if(!input || !size || size>254)return -22;
    for(size_t i=0;i<size;i++)if((unsigned char)input[i]<32)return -22;
    char line[255];memcpy(line,input,size);line[size]=0;
    int result=shell_submit_line(line);if(result)return result;
    busy=1;notify();return 0;
}
void console_detach(uint32_t pid,uint32_t id) {
    GUI_GUARD;
    if(owner!=pid || window!=id)return;
    __atomic_store_n(&owner,0,__ATOMIC_RELEASE);window=0;lease=0;notified=0;
}
void console_emit(char byte) {
    GUI_GUARD;
    if(count==CAPACITY){head=(head+1)%CAPACITY;--count;overrun=1;}
    bytes[(head+count)%CAPACITY]=byte;++count;notify();
}
void console_clear(void){console_emit('\033');console_emit('[');console_emit('2');console_emit('J');console_emit('\033');console_emit('[');console_emit('H');}
void console_command_begin(void){GUI_GUARD;busy=1;notify();}
void console_command_ready(void){GUI_GUARD;busy=0;notify();}
