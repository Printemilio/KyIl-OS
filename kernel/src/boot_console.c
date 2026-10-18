#include <unos/boot_console.h>
#include <unos/boot_requests.h>
#include <unos/boot_font.h>
#include <stddef.h>
static struct limine_framebuffer *fb;
static unsigned column,row,columns,rows;
static uint8_t active;
static uint32_t ink;
static uint32_t color(unsigned r,unsigned g,unsigned b) {
    return ((r*((1u<<fb->red_mask_size)-1u)/255u)<<fb->red_mask_shift)|
           ((g*((1u<<fb->green_mask_size)-1u)/255u)<<fb->green_mask_shift)|
           ((b*((1u<<fb->blue_mask_size)-1u)/255u)<<fb->blue_mask_shift);
}
static void pixel(unsigned x,unsigned y,uint32_t value) {
    if(x>=fb->width||y>=fb->height)return;
    volatile uint8_t *p=(volatile uint8_t *)fb->address+(uint64_t)y*fb->pitch+(uint64_t)x*(fb->bpp/8u);
    if(fb->bpp==32)*(volatile uint32_t *)p=value;
    else if(fb->bpp==24){p[0]=value;p[1]=value>>8u;p[2]=value>>16u;}
    else if(fb->bpp==16)*(volatile uint16_t *)p=(uint16_t)value;
}
static void newline(void) {
    column=0;
    if(++row<rows)return;
    /* Scroll the small text area, using the real pitch. No presentation loop. */
    unsigned last=(rows-1u)*16u;
    for(unsigned y=0;y<last;++y) {
        volatile uint8_t *dst=(volatile uint8_t *)fb->address+(uint64_t)(y+8u)*fb->pitch+8u*(fb->bpp/8u);
        volatile uint8_t *src=dst+16u*fb->pitch;
        for(unsigned x=0;x<columns*8u*(fb->bpp/8u);++x)dst[x]=src[x];
    }
    row=rows-1u;
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<columns*8u;++x)pixel(8u+x,8u+last+y,0);
}
void boot_console_init(void) {
    if(!framebuffer_request.response||!framebuffer_request.response->framebuffer_count)return;
    fb=framebuffer_request.response->framebuffers[0];
    if(!fb || fb->width<32 || fb->height<32 || (fb->bpp!=16 && fb->bpp!=24 && fb->bpp!=32))return;
    columns=(fb->width-16u)/8u;rows=(fb->height-16u)/16u;column=row=0;
    ink=color(210,220,235);active=1;
    for(unsigned y=0;y<fb->height;++y)for(unsigned x=0;x<fb->width;++x)pixel(x,y,0);
    boot_console_puts("KyIlOS - early boot trace (no GUI / no serial required)\n");
}
void boot_console_puts(const char *text) {
    if(!active||!text)return;
    for(;*text;++text) {
        unsigned char c=(unsigned char)*text;
        if(c=='\n'){newline();continue;}
        if(c=='\r'){column=0;continue;}
        if(c>=128)c='?';
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<8;++x)
            pixel(8u+column*8u+x,8u+row*16u+y,(boot_font[c][y/2u]&(1u<<(7u-x)))?ink:0);
        if(++column>=columns)newline();
    }
}
void boot_console_hex(uint64_t value) {
    char digits[19]="0x0000000000000000";
    const char *hex="0123456789ABCDEF";
    for(unsigned i=0;i<16;++i)digits[17u-i]=hex[(value>>(i*4u))&15u];
    boot_console_puts(digits);
}
void boot_console_stage(const char *name,int completed) {
    boot_console_puts(completed?"[DONE] ":"[....] ");boot_console_puts(name);boot_console_puts("\n");
}
void boot_console_finish(void) {active=0;}
int boot_console_active(void) {return active!=0;}
void boot_console_fail(const char *reason) {
    if(fb){active=1;ink=color(255,120,120);}
    boot_console_puts("\n[FAIL] ");boot_console_puts(reason);boot_console_puts("\n");
    for(;;)__asm__ volatile("cli; hlt");
}
