#include <unos/sched_clock.h>
#include <unos/debug_log.h>
#include <unos/boot_console.h>
#include <unos/rtc_clock_calibration.h>
static uint64_t epoch, frequency;
static inline uint8_t input(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
static inline void output(uint16_t port,uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(port)); }
static uint8_t clock_cmos_read(void *context,uint8_t reg) {
    (void)context; output(0x70,reg); return input(0x71);
}
static uint64_t clock_read_cycles(void *context) { (void)context; return sched_clock_cycles(); }
static void clock_boot_value(const char *name,uint64_t value) {
    boot_console_puts(name);boot_console_hex(value);
    char digits[19]="0x0000000000000000";
    const char *hex="0123456789abcdef";
    for(unsigned i=0;i<16;++i)digits[17-i]=hex[(value>>(i*4))&15];
    debug_log_puts(name);debug_log_puts(digits);
}
static void clock_boot_newline(void) { boot_console_puts("\n");debug_log_puts("\n"); }
uint64_t sched_clock_cycles(void) { uint32_t a,d; __asm__ volatile("lfence; rdtsc":"=a"(a),"=d"(d)::"memory"); return ((uint64_t)d<<32)|a; }
void sched_clock_calibrate(void) {
    boot_console_stage("Clock: CPUID frequency / RTC calibration",0);
    uint32_t a,b,c,d,max;
    __asm__ volatile("cpuid":"=a"(max),"=b"(b),"=c"(c),"=d"(d):"a"(0),"c"(0));
    if(max>=0x15) {
        __asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x15),"c"(0));
        if(a && b && c)frequency=(uint64_t)c*b/a;
    }
    if(!frequency) {
        boot_console_stage("Clock: waiting for RTC seconds edges",0);
        /* Slow independent reference when the architectural crystal ratio
           is absent. CPUID.16 base MHz is not a TSC frequency contract. */
        const rtc_clock_io_t io={NULL,clock_cmos_read,clock_read_cycles,100000000};
        rtc_clock_result_t result;
        int rc=rtc_clock_measure(&io,&result);
        clock_boot_value("  RTC polls=",result.polls);
        clock_boot_value(" UIP rejects=",result.rejected_uip);
        clock_boot_value(" torn=",result.rejected_torn);clock_boot_newline();
        clock_boot_value("  RTC invalid=",result.rejected_invalid);
        clock_boot_value(" skipped seconds=",result.discontinuities);
        clock_boot_value(" inconsistent=",result.inconsistent_intervals);clock_boot_newline();
        clock_boot_value("  RTC edge seconds=",result.edge_seconds[0]);
        clock_boot_value("/",result.edge_seconds[1]);clock_boot_value("/",result.edge_seconds[2]);clock_boot_newline();
        clock_boot_value("  RTC TSC cycles per full second=",result.interval[0]);
        clock_boot_value("/",result.interval[1]);clock_boot_newline();
        clock_boot_value("  RTC frequency bounds (Hz)=",result.lower_hz);
        clock_boot_value("/",result.upper_hz);clock_boot_newline();
        if(rc) boot_console_fail(rc==-2 ? "Clock: nonmonotonic TSC" : "Clock: no consistent full RTC seconds");
        frequency=result.frequency;
    }
    epoch=sched_clock_cycles();
    boot_console_puts("  TSC Hz=");boot_console_hex(frequency);boot_console_puts("\n");
    boot_console_stage("Clock calibrated",1);
    debug_log_puts("FlowSched: monotonic clock uses measured TSC crystal/RTC second\n");
    char digits[24];uint64_t value=frequency;unsigned n=0;
    do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
    debug_log_puts("FlowSched: TSC Hz=");while(n){char c[2]={digits[--n],0};debug_log_puts(c);}debug_log_puts("\n");
}
uint64_t sched_clock_frequency(void) { return frequency; }
uint64_t sched_clock_ns(void) {
    if(!frequency)return 0;
    uint64_t delta=sched_clock_cycles()-epoch;
    /* Split division avoids wrap after a few seconds on a GHz TSC. */
    return (delta/frequency)*1000000000ull+(delta%frequency)*1000000000ull/frequency;
}
