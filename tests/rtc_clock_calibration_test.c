#include <unos/rtc_clock_calibration.h>
#include <assert.h>
#include <stdio.h>
enum { NORMAL, BINARY, UIP_GARBAGE, TORN, FROZEN, BAD_BCD, BACKWARDS, SKIP, RATE_CHANGE };
typedef struct { uint64_t ns,tsc; unsigned fault,second_reads,cycle_reads; } fixture_t;
static void advance(fixture_t *f,uint64_t ns) {
    uint64_t hz=f->fault==RATE_CHANGE && f->ns>=2000000000ull?4000000000ull:2000000000ull;
    f->ns+=ns;f->tsc+=ns*(hz/1000000000ull);
}
static uint64_t cycles(void *context) {
    fixture_t *f=context;advance(f,50000);
    if(f->fault==BACKWARDS && ++f->cycle_reads==4)return 0;
    return f->tsc;
}
static uint8_t read_cmos(void *context,uint8_t reg) {
    fixture_t *f=context;advance(f,5000);
    int uip=f->ns%1000000000ull>=998000000ull;
    if(reg==0x0a)return uip?0x80:0x20;
    if(reg==0x0b)return f->fault==FROZEN?0x80:f->fault==BINARY?4:0;
    assert(reg==0);
    ++f->second_reads;
    if(f->fault==BAD_BCD)return 0x7a;
    if(f->fault==TORN)return f->second_reads&1?0x10:0x11;
    if(f->fault==UIP_GARBAGE && uip)return f->second_reads&1?0:0x59;
    unsigned second=(57+f->ns/1000000000ull+(f->fault==SKIP && f->ns>=1500000000ull?3:0))%60;
    return f->fault==BINARY?second:(second/10)*16+second%10;
}
static rtc_clock_result_t run(unsigned fault,int expected) {
    fixture_t f={.fault=fault};
    if(fault==UIP_GARBAGE) { f.ns=998100000;f.tsc=f.ns*2; }
    rtc_clock_io_t io={&f,read_cmos,cycles,100000};rtc_clock_result_t r;
    int rc=rtc_clock_measure(&io,&r);
    if(rc!=expected)printf("fault %u: rc=%d expected=%d\n",fault,rc,expected);
    assert(rc==expected);
    if(!rc) {
        uint64_t hz=fault==RATE_CHANGE?4000000000ull:2000000000ull;
        assert(r.lower_hz<=hz && r.upper_hz>=hz);
        assert(r.frequency>=hz-1000000 && r.frequency<=hz+1000000);
        assert(r.edge_seconds[1]==(r.edge_seconds[0]+1)%60);
        assert(r.edge_seconds[2]==(r.edge_seconds[1]+1)%60);
    } else assert(!r.frequency);
    return r;
}
int main(void) {
    /* Reproduce the old code accepting an update-cycle glitch as a second. */
    fixture_t f={.fault=UIP_GARBAGE,.ns=998100000,.tsc=1996200000};
    uint8_t previous=read_cmos(&f,0),current;
    do{current=read_cmos(&f,0);}while(current==previous);
    uint64_t start=cycles(&f);previous=current;
    do{current=read_cmos(&f,0);}while(current==previous);
    uint64_t bogus_hz=cycles(&f)-start;
    assert(bogus_hz<1000000); /* ~microseconds advertised as one whole second */
    run(NORMAL,0);run(BINARY,0);
    rtc_clock_result_t r=run(UIP_GARBAGE,0);assert(r.rejected_uip);
    r=run(TORN,-1);assert(r.rejected_torn);
    r=run(FROZEN,-1);assert(r.rejected_invalid);
    r=run(BAD_BCD,-1);assert(r.rejected_invalid);
    run(BACKWARDS,-2);
    r=run(SKIP,0);assert(r.discontinuities);
    r=run(RATE_CHANGE,0);assert(r.inconsistent_intervals);
    puts("rtc-clock: PASS old premature frequency reproduced; UIP, torn/invalid/frozen data, BCD/binary, rollover, skips, TSC consistency/monotonicity");
}
