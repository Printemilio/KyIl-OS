#include <unos/timer_calibration.h>
#include <assert.h>
#include <stdio.h>
enum { NORMAL,BAD_MODE,NEVER_LOAD,WRONG_COUNT,BAD_GATE,BAD_APIC,NEVER_APIC_LOAD,
       STOP_PIT,NO_OUT,STOP_APIC,EXPIRE_APIC,TOO_SLOW,GATE_IGNORED,LONG_INTERVAL,
       EARLY_OUT,REVERSE_PIT,OUT_MISMATCH,REVERSE_TSC,REVERSE_APIC };
typedef struct {
    int fault,programmed,gate,null_count,load_delay,apic_delay,active,terminal;
    unsigned read_phase,write_phase,cal_samples,bad_writes,scale;
    uint8_t port,status_latched;
    uint16_t count,reload,count_latched;
    uint32_t apic,div,lvt;
    uint64_t clock;
    int slow_tick;
} fixture_t;
static void advance(fixture_t *f) {
    f->clock+=10*(f->scale?f->scale:1);
    if(f->programmed && f->null_count && f->fault!=NEVER_LOAD && --f->load_delay<=0)
        f->null_count=0;
    if((f->gate || f->fault==GATE_IGNORED) && f->programmed && !f->null_count && f->fault!=STOP_PIT) {
        if(!f->terminal && f->count<=10)f->terminal=1;
        f->count-=10;
    }
    if(f->active && !f->apic_delay && f->fault!=STOP_APIC) {
        uint32_t step=f->fault==TOO_SLOW?0:400;
        if(f->fault==TOO_SLOW && f->gate && f->count<f->reload-11850 && !f->slow_tick) {
            f->slow_tick=1;step=1;
        }
        f->apic=f->apic>step?f->apic-step:0;
        if(f->fault==EXPIRE_APIC && f->gate && f->cal_samples>=1)f->apic=0;
        if(f->fault==LONG_INTERVAL && f->cal_samples && f->count<f->reload-11920)f->clock+=100000;
    }
}
static uint8_t input(void *context,uint16_t port) {
    fixture_t *f=context;advance(f);
    if(port==0x61)return f->port|0xd0|
        (((f->terminal || (f->fault==EARLY_OUT && f->gate)) && f->fault!=NO_OUT)?0x20:0);
    assert(port==0x42 && f->read_phase);
    if(f->read_phase--==3)return f->status_latched;
    return f->read_phase==1?(uint8_t)f->count_latched:(uint8_t)(f->count_latched>>8);
}
static void output(void *context,uint16_t port,uint8_t value) {
    fixture_t *f=context;advance(f);
    if(port==0x61) {
        if(value&0xf0)++f->bad_writes;
        f->port=value&15;f->gate=value&1;
        if(f->fault==BAD_GATE)f->port|=2;
    } else if(port==0x43) {
        assert(!f->read_phase); /* each readback must drain status AND count */
        if(value==0xb0) {
            f->programmed=0;f->write_phase=0;f->null_count=1;f->terminal=0;
        } else {
            assert(value==0xc8);f->read_phase=3;
            f->count_latched=f->count;
            if(f->active && f->gate && !f->terminal)++f->cal_samples;
            if(f->fault==REVERSE_PIT && f->cal_samples==2)f->count_latched+=2000;
            f->status_latched=(f->fault==BAD_MODE?0x34:0x30)|(f->null_count?0x40:0)|
                (f->terminal && f->fault!=OUT_MISMATCH?0x80:0);
            /* Terminal counter values are deliberately different/irrelevant. */
            if(f->terminal)f->count_latched=0x0ff4;
        }
    } else {
        assert(port==0x42);
        if(!f->write_phase++)f->reload=value;
        else {
            f->reload|=(uint16_t)value<<8;f->programmed=1;f->load_delay=4;
            f->count=f->reload+(f->fault==WRONG_COUNT);
        }
    }
}
static uint32_t apic_read(void *context,uint32_t reg) {
    fixture_t *f=context;advance(f);
    if(reg==0x3e0)return f->fault==BAD_APIC?0:f->div;
    if(reg==0x320)return f->lvt;
    assert(reg==0x390);
    if(f->fault==NEVER_APIC_LOAD || (f->apic_delay && --f->apic_delay))return 0;
    if(f->fault==REVERSE_APIC && f->cal_samples>=2)return UINT32_MAX;
    return f->apic;
}
static void apic_write(void *context,uint32_t reg,uint32_t value) {
    fixture_t *f=context;advance(f);
    if(reg==0x3e0)f->div=value;
    else if(reg==0x320)f->lvt=value;
    else {
        assert(reg==0x380);f->apic=value;f->active=value!=0;
        if(value)f->apic_delay=3;
    }
}
static uint64_t cycles(void *context) {
    fixture_t *f=context;f->clock+=10*(f->scale?f->scale:1);
    if(f->fault==REVERSE_TSC && f->cal_samples==2)return 0;
    return f->clock;
}
static timer_calibration_result_t run_scaled(int fault,uint64_t hz,unsigned scale) {
    fixture_t f={.fault=fault,.port=0x0e,.terminal=1,.scale=scale};
    timer_calibration_io_t io={&f,input,output,apic_read,apic_write,cycles,hz};
    timer_calibration_result_t r;timer_calibration_measure(&io,&r);
    assert(!f.bad_writes && !f.active && f.port==0x0e && !f.read_phase);
    return r;
}
static timer_calibration_result_t run(int fault) {return run_scaled(fault,1000000,1);}
int main(void) {
    timer_calibration_result_t r=run(NORMAL);
    assert(r.error==TIMER_CAL_OK && r.exit_reason==TIMER_EXIT_VERIFIED);
    assert(r.reload==23864 && r.target_ticks==11932 && r.pit_loaded==r.reload);
    assert(r.start.pit>r.end.pit && r.end.pit && !(r.start.status&0x80) && !(r.end.status&0x80));
    assert(r.reference_ticks==r.start.pit-r.end.pit && r.reference_ticks>=r.target_ticks);
    assert(r.apic_ticks==r.start.apic-r.end.apic);
    assert(r.cycles_elapsed==r.end.tsc-r.start.tsc);
    assert(r.count_per_ms>=47720 && r.count_per_ms<47740);
    /* Post-terminal 0x0ff4 is neither the measured END nor an operand. */
    assert(r.terminal_pit==0x0ff4 && r.end.pit!=0x0ff4 && r.terminal_status==0xb0);
    assert(r.rate_denominator==(uint64_t)r.reference_ticks*1000);
    assert(r.start.apic_before>=r.start.apic && r.start.apic>=r.start.apic_after);
    assert(r.end.tsc_before<=r.end.tsc && r.end.tsc<=r.end.tsc_after);
    const timer_calibration_error_t expected[]={TIMER_CAL_PIT_PROGRAM,TIMER_CAL_PIT_LOAD_TIMEOUT,
        TIMER_CAL_PIT_COUNT,TIMER_CAL_GATE,TIMER_CAL_APIC_PROGRAM,TIMER_CAL_APIC_LOAD_TIMEOUT,
        TIMER_CAL_PIT_STOPPED,TIMER_CAL_TERMINAL_TIMEOUT,TIMER_CAL_APIC_STOPPED,
        TIMER_CAL_APIC_EXPIRED,TIMER_CAL_RATE_ZERO};
    for(int fault=BAD_MODE;fault<=TOO_SLOW;++fault) {
        r=run(fault);
        if(r.error!=expected[fault-1])printf("fault %d got %s expected %s\n",fault,
            timer_calibration_error_name(r.error),timer_calibration_error_name(expected[fault-1]));
        assert(r.error==expected[fault-1] && !r.count_per_ms);
    }
    r=run(GATE_IGNORED);assert(!r.error && r.pit_loaded<r.reload);
    assert(run(LONG_INTERVAL).error==TIMER_CAL_REFERENCE_WINDOW);
    assert(run(EARLY_OUT).error==TIMER_CAL_EARLY_TERMINAL);
    assert(run(REVERSE_PIT).error==TIMER_CAL_PIT_REVERSED);
    assert(run(REVERSE_TSC).error==TIMER_CAL_TSC_REVERSED);
    assert(run(REVERSE_APIC).error==TIMER_CAL_APIC_REVERSED);
    r=run(OUT_MISMATCH);
    assert(r.error==TIMER_CAL_OUT_MISMATCH && r.rate_raw>0 && !r.count_per_ms);
    assert(r.end.pit>0 && r.end.status==0x30 && r.reference_ticks==r.start.pit-r.end.pit);
    assert(run_scaled(NORMAL,0,1).error==TIMER_CAL_NO_CLOCK);
    r=run_scaled(NORMAL,0x1548c,100);
    assert(r.error==TIMER_CAL_WINDOW_TIMEOUT && r.exit_reason==TIMER_EXIT_WINDOW_WATCHDOG);
    r=run_scaled(NORMAL,100000000,100);assert(!r.error);
    uint64_t n,d,rate;
    assert(timer_calibration_rate(124745,11943,&n,&d,&rate)==TIMER_CAL_OK && rate==12463);
    assert(timer_calibration_rate(124745,7847,&n,&d,&rate)==TIMER_CAL_OK && rate>0);
    assert(timer_calibration_rate(UINT32_MAX,1,&n,&d,&rate)==TIMER_CAL_RATE_OVERFLOW);
    assert(timer_calibration_rate(UINT32_MAX,UINT32_MAX,&n,&d,&rate)==TIMER_CAL_OK && rate==1193);
    assert(timer_calibration_rate(1,11932,&n,&d,&rate)==TIMER_CAL_RATE_ZERO);
    assert(timer_calibration_rate(100,0,&n,&d,&rate)==TIMER_CAL_PIT_COUNT);
    assert(timer_calibration_rate(625000,11932,&n,&d,&rate)==TIMER_CAL_OK && rate==62499);
    puts("timer-calibration: PASS paired pre-zero samples, exact frozen operands, independent OUT verification, post-terminal bytes ignored by rate, cleanup, 64-bit arithmetic/Dell values/watchdog");
}
