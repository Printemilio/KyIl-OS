#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unos/browser_viewport.h>
#include <unos/vex_legacy_compat.h>
/* Kernel headers deliberately compile assert out. This host test must not. */
extern void *malloc(size_t);
extern int puts(const char *);
extern int printf(const char *,...);
extern int dprintf(int,const char *,...);
#undef assert
#define assert(condition) do{if(!(condition)){dprintf(2,"FAIL line %d: %s\n",__LINE__,#condition);abort();}}while(0)

static uint64_t bx=100,by=80,bw=640,bh=480;
static int visible=1,captured;
static uint32_t owner=42,tab_id=1;
uint32_t gui_browser_active_tab(void){return tab_id;}
void *kmalloc(size_t n){return malloc(n);}
void kfree(void *p){free(p);}
uint32_t scheduler_current_tid(void){return 42;}
uint32_t scheduler_current_pid(void){return 42;}
uint32_t scheduler_current_process_id(void){return owner;}
uint32_t scheduler_current_ppid(void){return owner-1u;}
void process_wake(uint32_t p){(void)p;}
int scheduler_set_process_priority(uint32_t p,uint8_t v){(void)p;(void)v;return 0;}
const char *scheduler_state_name(uint32_t p){(void)p;return "ready";}
uint64_t irq_timer_ticks(void){return 17;}
uint32_t keyboard_modifiers(void){return 2;}
void keyboard_focus_capture_current(void){captured=1;}
void keyboard_focus_release(void){captured=0;}
void debug_log_puts(const char *s){(void)s;}
void gui_browser_begin(void){}
void gui_browser_end(void){}
int gui_browser_viewport_visible(void){return visible;}
uint64_t gui_content_x(void){return bx;}
uint64_t gui_content_y(void){return by;}
uint64_t gui_content_width(void){return bw;}
uint64_t gui_content_height(void){return bh;}
static int drain(unsigned type){vex_viewport_event_t e;int count=0,r;while((r=browser_viewport_poll(&e))>0)if(e.type==type)++count;assert(r==0);return count;}
int main(void){
    vex_viewport_event_t e;
    assert(ladybird_display_open(640,480)==0);
    assert(ladybird_browser_parent_pid()==owner-1u);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_CONFIGURE && e.x==100 && e.width==640);
    browser_viewport_pointer(120,110,0,0,1);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_ENTER && e.x==20 && e.y==30);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_MOTION && e.time==170 && e.hardware==0);
    browser_viewport_pointer(121,111,1,0,1);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_MOTION && e.hardware==0);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_FOCUS && e.pressed);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_BUTTON && e.pressed && e.hardware==1);
    assert(browser_viewport_poll(&e)==0 && captured && browser_viewport_has_focus());
    browser_viewport_key('A',0x10,1,2);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_KEY && e.key=='A' && e.modifiers==2);
    browser_viewport_pointer(121,111,0,3,1);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_MOTION && e.hardware==1);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_BUTTON && !e.pressed && e.hardware==0);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_WHEEL);
    assert(browser_viewport_poll(&e)==0);
    browser_viewport_pointer(99,110,0,0,1);
    assert(drain(VEX_VIEWPORT_LEAVE)==1);
    browser_viewport_pointer(99,110,1,2,1);
    assert(drain(VEX_VIEWPORT_FOCUS)==1 && !captured && !browser_viewport_has_focus());
    browser_viewport_key('b',0x30,1,0);assert(browser_viewport_poll(&e)==0);
    bx=150;bw=320;
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_CONFIGURE && e.x==150 && e.width==320);
    browser_viewport_pointer(170,110,0,0,1);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_ENTER && e.x==20);
    drain(0);visible=0;
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_CONFIGURE && !e.width);
    visible=1;drain(0);
    /* A press owned by chrome must not leak an unpaired release into Web. */
    browser_viewport_pointer(99,110,1,0,1);drain(0);
    browser_viewport_pointer(170,110,1,0,1);drain(0);
    browser_viewport_pointer(170,110,0,0,1);assert(drain(VEX_VIEWPORT_BUTTON)==0);
    /* Conversely a Web-owned drag must receive its actual release, even
     * outside the content rectangle, without forwarding an outside click. */
    browser_viewport_pointer(170,110,1,0,1);assert(drain(VEX_VIEWPORT_BUTTON)==1);
    browser_viewport_pointer(99,110,1,0,1);drain(0);
    browser_viewport_pointer(99,110,0,0,1);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_BUTTON && !e.pressed && e.x==-51);
    assert(browser_viewport_poll(&e)==0);
    /* Queued old-page events keep their identity across selection. */
    tab_id=1;drain(0);browser_viewport_hide();tab_id=2;
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_CONFIGURE && browser_viewport_polled_tab()==2);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_FOCUS && !e.pressed && browser_viewport_polled_tab()==1);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_CONFIGURE && !e.width && browser_viewport_polled_tab()==1);
    assert(browser_viewport_poll(&e)==0);
    char command[1200],out[1200];memset(command,'x',sizeof(command));command[1071]=0;
    assert(ladybird_browser_command_push(command)==0);
    assert(ladybird_browser_command_pop(out,16)==-7); /* no consume on truncation */
    assert(ladybird_browser_command_pop(out,sizeof(out))==1 && !strcmp(out,command));
    for(unsigned i=0;i<31;i++)assert(ladybird_browser_command_push("same")==0);
    assert(ladybird_browser_command_push("same")==-11);
    for(unsigned i=0;i<31;i++)assert(ladybird_browser_command_pop(out,sizeof(out))==1 && !strcmp(out,"same"));
    assert(ladybird_browser_command_pop(out,sizeof(out))==0);
    for(unsigned i=0;i<300;i++)browser_viewport_pointer(170,110,i&1,0,1);
    assert(browser_viewport_poll(&e)==-105);
    ladybird_display_close();
    assert(ladybird_browser_process_pid()==0 && !captured && !browser_viewport_has_focus());
    owner=43;
    assert(ladybird_display_open(640,480)==0);
    assert(ladybird_browser_process_pid()==43);
    assert(ladybird_browser_parent_pid()==42);
    assert(browser_viewport_poll(&e)==1 && e.type==VEX_VIEWPORT_CONFIGURE);
    assert(browser_viewport_poll(&e)==0); /* no stale event/overflow/button */
    ladybird_display_close();
    puts("browser viewport: bounds/local coordinates, focus, chrome exclusion, wheel, resize, hide, explicit overflow PASS");
    return 0;
}
