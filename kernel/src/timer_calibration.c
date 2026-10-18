#include <unos/timer_calibration.h>
#define PIT_HZ 1193182u
#define TARGET_TICKS ((PIT_HZ+50u)/100u)
/* Measure ~10ms before zero; another 10ms provides headroom. OUT is checked
 * subsequently, without using a post-terminal count as a rate operand. */
#define RELOAD (2u*TARGET_TICKS)
#define APIC_LVT 0x320u
#define APIC_INIT 0x380u
#define APIC_CURRENT 0x390u
#define APIC_DIV 0x3e0u
static uint16_t read_latched(const timer_calibration_io_t *io,uint8_t *status,
                            uint8_t *low,uint8_t *high) {
    *status=io->input(io->context,0x42);
    *low=io->input(io->context,0x42);*high=io->input(io->context,0x42);
    return (uint16_t)*low|((uint16_t)*high<<8);
}
static uint16_t pit_snapshot(const timer_calibration_io_t *io,uint8_t *status,
                            uint8_t *low,uint8_t *high) {
    /* Read-back COUNT+STATUS, counter 2 only. Status first, then both bytes;
     * always fully drain the latches before the next command. */
    io->output(io->context,0x43,0xc8);
    return read_latched(io,status,low,high);
}
static timer_calibration_error_t sample(const timer_calibration_io_t *io,
                                      timer_calibration_sample_t *s) {
    *s=(timer_calibration_sample_t){0};
    s->tsc_before=io->cycles(io->context);
    s->apic_before=io->apic_read(io->context,APIC_CURRENT);
    io->output(io->context,0x43,0xc8);
    s->apic_after=io->apic_read(io->context,APIC_CURRENT);
    s->tsc_after=io->cycles(io->context);
    s->pit=read_latched(io,&s->status,&s->low,&s->high);
    s->port=io->input(io->context,0x61);
    if(s->tsc_after<s->tsc_before)return TIMER_CAL_TSC_REVERSED;
    if(s->apic_after>s->apic_before)return TIMER_CAL_APIC_REVERSED;
    if(!s->apic_after)return TIMER_CAL_APIC_EXPIRED;
    /* Latch time lies inside these brackets. Preserve raw bounds so I/O/MMIO
     * skew is observable; midpoint is used consistently at both endpoints. */
    s->tsc=s->tsc_before+(s->tsc_after-s->tsc_before)/2;
    s->apic=s->apic_after+(s->apic_before-s->apic_after)/2;
    if((s->status&0x7f)!=0x30)return TIMER_CAL_PIT_PROGRAM;
    if((s->port&3)!=1)return TIMER_CAL_GATE;
    if((s->status&0x80)||(s->port&0x20))return TIMER_CAL_EARLY_TERMINAL;
    if(!s->pit)return TIMER_CAL_PIT_COUNT;
    return TIMER_CAL_OK;
}
timer_calibration_error_t timer_calibration_rate(uint32_t ticks,uint32_t reference,
    uint64_t *numerator,uint64_t *denominator,uint64_t *rate) {
    *numerator=(uint64_t)ticks*PIT_HZ;
    *denominator=(uint64_t)reference*1000u;*rate=0;
    if(!reference)return TIMER_CAL_PIT_COUNT;
    /* Max uint32 inputs keep product + half denominator below UINT64_MAX. */
    *rate=(*numerator+*denominator/2)/ *denominator;
    if(!*rate)return TIMER_CAL_RATE_ZERO;
    if(*rate>UINT32_MAX)return TIMER_CAL_RATE_OVERFLOW;
    return TIMER_CAL_OK;
}
timer_calibration_error_t timer_calibration_measure(const timer_calibration_io_t *io,
                                                   timer_calibration_result_t *r) {
    *r=(timer_calibration_result_t){0};r->reload=RELOAD;r->target_ticks=TARGET_TICKS;
    r->watchdog_cycles=io->watchdog_cycles;
    uint64_t setup_start=io->cycles(io->context);
    r->setup_tsc=setup_start;
    r->port_before=io->input(io->context,0x61);
    uint8_t controls=r->port_before&0x0c; /* ICH8 bits 7:4 must be zero on writes */
    io->output(io->context,0x61,controls);
    if(!io->watchdog_cycles){r->error=TIMER_CAL_NO_CLOCK;goto done;}
    io->output(io->context,0x43,0xb0);
    io->output(io->context,0x42,(uint8_t)RELOAD);
    io->output(io->context,0x42,(uint8_t)(RELOAD>>8));
    do {
        uint8_t low,high;++r->pit_load_iterations;
        r->pit_loaded=pit_snapshot(io,&r->status_armed,&low,&high);
        if((r->status_armed&0x3f)!=0x30){r->error=TIMER_CAL_PIT_PROGRAM;goto done;}
        if(!(r->status_armed&0x40))break;
        if(io->cycles(io->context)-setup_start>=io->watchdog_cycles) {
            r->exit_reason=TIMER_EXIT_PIT_LOAD_WATCHDOG;
            r->error=TIMER_CAL_PIT_LOAD_TIMEOUT;goto done;
        }
    } while(1);
    r->port_armed=io->input(io->context,0x61);
    if(r->port_armed&3){r->error=TIMER_CAL_GATE;goto done;}
    if(r->pit_loaded<=TARGET_TICKS || r->pit_loaded>RELOAD ||
       (r->status_armed&0x80)||(r->port_armed&0x20)) {
        r->error=TIMER_CAL_PIT_COUNT;goto done;
    }
    io->apic_write(io->context,APIC_INIT,0);
    io->apic_write(io->context,APIC_DIV,3);
    io->apic_write(io->context,APIC_LVT,0x10030);
    r->apic_div=io->apic_read(io->context,APIC_DIV);
    r->apic_lvt=io->apic_read(io->context,APIC_LVT);
    if((r->apic_div&0xb)!=3 || (r->apic_lvt&0x700ff)!=0x10030) {
        r->error=TIMER_CAL_APIC_PROGRAM;goto done;
    }
    io->apic_write(io->context,APIC_INIT,UINT32_MAX);
    do {
        ++r->apic_load_iterations;
        if(io->apic_read(io->context,APIC_CURRENT))break;
        if(io->cycles(io->context)-setup_start>=io->watchdog_cycles) {
            r->exit_reason=TIMER_EXIT_APIC_LOAD_WATCHDOG;
            r->error=TIMER_CAL_APIC_LOAD_TIMEOUT;goto done;
        }
    } while(1);
    io->output(io->context,0x61,controls|1);
    r->start_valid=1;r->error=sample(io,&r->start);
    if(r->error)goto done;
    if(r->start.pit<=TARGET_TICKS){r->error=TIMER_CAL_PIT_COUNT;goto done;}
    uint16_t previous=r->start.pit;
    do {
        ++r->sample_iterations;r->end_valid=1;
        r->error=sample(io,&r->end);
        if(r->error)goto done;
        if(r->end.tsc<=r->start.tsc){r->error=TIMER_CAL_TSC_REVERSED;goto done;}
        r->cycles_elapsed=r->end.tsc-r->start.tsc;
        if(r->end.pit>previous){r->error=TIMER_CAL_PIT_REVERSED;goto done;}
        previous=r->end.pit;
        r->reference_ticks=(uint32_t)r->start.pit-r->end.pit;
        if(r->cycles_elapsed>=io->watchdog_cycles) {
            r->exit_reason=TIMER_EXIT_WINDOW_WATCHDOG;
            r->error=r->reference_ticks?TIMER_CAL_WINDOW_TIMEOUT:TIMER_CAL_PIT_STOPPED;goto done;
        }
        if(r->reference_ticks>=TARGET_TICKS)break;
    } while(1);
    uint64_t wrap_cycles=(io->watchdog_cycles/PIT_HZ)*65536u+
        ((io->watchdog_cycles%PIT_HZ)*65536u)/PIT_HZ;
    if(r->cycles_elapsed>=wrap_cycles){r->error=TIMER_CAL_REFERENCE_WINDOW;goto done;}
    if(r->end.apic>=r->start.apic){r->error=TIMER_CAL_APIC_STOPPED;goto done;}
    r->apic_ticks=r->start.apic-r->end.apic;
    r->error=timer_calibration_rate(r->apic_ticks,r->reference_ticks,
        &r->rate_numerator,&r->rate_denominator,&r->rate_raw);
    if(r->error)goto done;
    /* Verify OUT separately. Post-terminal bytes never become rate operands. */
    do {
        ++r->terminal_iterations;
        r->terminal_port=io->input(io->context,0x61);
        r->terminal_tsc=io->cycles(io->context);
        if((r->terminal_port&3)!=1){r->error=TIMER_CAL_GATE;goto done;}
        if(r->terminal_tsc<r->end.tsc){r->error=TIMER_CAL_TSC_REVERSED;goto done;}
        if(r->terminal_tsc-r->start.tsc>=io->watchdog_cycles) {
            r->exit_reason=TIMER_EXIT_TERMINAL_WATCHDOG;
            r->error=TIMER_CAL_TERMINAL_TIMEOUT;goto done;
        }
        if(r->terminal_port&0x20)break;
    } while(1);
    r->terminal_pit=pit_snapshot(io,&r->terminal_status,&r->terminal_low,&r->terminal_high);
    if((r->terminal_status&0x7f)!=0x30){r->error=TIMER_CAL_PIT_PROGRAM;goto done;}
    if(!(r->terminal_status&0x80)){r->error=TIMER_CAL_OUT_MISMATCH;goto done;}
    r->exit_reason=TIMER_EXIT_VERIFIED;r->count_per_ms=(uint32_t)r->rate_raw;
done:
    /* Immutable snapshots: exactly the accepted/rejected calculation operands.
     * Cleanup never substitutes later hardware observations. */
    r->exit_tsc=io->cycles(io->context);
    if(r->start_valid && r->end_valid && r->end.tsc>=r->start.tsc)
        r->cycles_elapsed=r->end.tsc-r->start.tsc;
    io->apic_write(io->context,APIC_INIT,0);
    io->output(io->context,0x61,r->port_before&0x0f);
    return r->error;
}
const char *timer_calibration_error_name(timer_calibration_error_t error) {
    static const char *const names[]={"OK","TSC watchdog unavailable","PIT mode/NULL COUNT changed",
        "PIT NULL COUNT never cleared","PIT count/headroom invalid","PIT gate/speaker mismatch",
        "LAPIC DIV/LVT mismatch","LAPIC counter never loaded","PIT count did not advance",
        "PIT decrement window watchdog","PIT terminal reached before measured window",
        "PIT OUT terminal watchdog","PIT OUT port/status mismatch","PIT count increased before terminal",
        "LAPIC did not decrement","LAPIC expired","LAPIC increased inside sample",
        "TSC nonmonotonic","rounded LAPIC rate is zero","LAPIC rate exceeds initial-count width",
        "measurement exceeded PIT 16-bit window"};
    return (unsigned)error<sizeof(names)/sizeof(names[0])?names[error]:"invalid error";
}
const char *timer_calibration_exit_name(timer_calibration_exit_t reason) {
    static const char *const names[]={"check rejected; snapshots frozen","PIT load watchdog",
        "LAPIC load watchdog","PIT decrement-window watchdog","separate OUT verification watchdog",
        "PIT decrement window measured before zero; subsequent OUT transition verified"};
    return (unsigned)reason<sizeof(names)/sizeof(names[0])?names[reason]:"invalid exit";
}
