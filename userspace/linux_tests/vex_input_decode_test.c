#include <unos/mouse.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
    mouse_packet_t p;
    const uint8_t positive[]={8,250,250,1},negative[]={0x38,6,6,0xf},middle[]={0x0c,0,0,0};
    assert(mouse_decode_packet(positive,4,&p)&&p.dx==250&&p.dy==-250&&p.wheel==1);
    assert(mouse_decode_packet(negative,4,&p)&&p.dx==-250&&p.dy==250&&p.wheel==-1);
    assert(mouse_decode_packet(middle,3,&p)&&p.buttons==4&&p.wheel==0);
    const uint8_t overflow[]={0xc8,1,1,0},bad_sync[]={0,1,1,0};
    assert(!mouse_decode_packet(overflow,4,&p));assert(!mouse_decode_packet(bad_sync,4,&p));
    assert(!mouse_decode_packet(positive,2,&p));assert(!mouse_decode_packet(NULL,3,&p));
    puts("Vex native mouse decode: 7 checks PASS");return 0;
}
