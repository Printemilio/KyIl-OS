#include <unos/rtc.h>

#include <stdint.h>
#include <unos/kernel_guard.h>
#include <unos/driver_manager.h>
static kernel_gate_t rtc_gate=KERNEL_GATE_INIT;

#define CMOS_ADDRESS_PORT 0x70u
#define CMOS_DATA_PORT 0x71u
#define CMOS_REG_SECONDS 0x00u
#define CMOS_REG_MINUTES 0x02u
#define CMOS_REG_HOURS 0x04u
#define CMOS_REG_DAY 0x07u
#define CMOS_REG_MONTH 0x08u
#define CMOS_REG_YEAR 0x09u
#define CMOS_REG_STATUS_A 0x0au
#define CMOS_REG_STATUS_B 0x0bu
#define CMOS_UPDATE_IN_PROGRESS 0x80u
#define CMOS_STATUS_24H 0x02u
#define CMOS_STATUS_BINARY 0x04u
#define CMOS_NMI_DISABLE 0x80u

static uint8_t cached_hours;
static uint8_t cached_minutes;
static uint8_t cached_seconds;
static uint8_t cached_day;
static uint8_t cached_month;
static uint16_t cached_year;
static uint8_t rtc_ready;
static uint8_t rtc_valid;
int rtc_driver_transaction(int (*operation)(void *),void *context) {
    KERNEL_GUARD(rtc_gate);return operation(context);
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDRESS_PORT, (uint8_t)(CMOS_NMI_DISABLE | reg));
    return inb(CMOS_DATA_PORT);
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0fu) + ((value >> 4u) * 10u));
}

static void wait_for_stable_rtc(void) {
    while ((cmos_read(CMOS_REG_STATUS_A) & CMOS_UPDATE_IN_PROGRESS) != 0) {
    }
}

static void rtc_read_current(void) {
    KERNEL_GUARD(rtc_gate);
    uint64_t date[8]={0};
    int described=driver_clock_read(date);
    if(described!=DRV_NOT_BOUND) {
        rtc_valid=described==0 && date[0]>=1970 && date[0]<=9999 && date[1]>=1 && date[1]<=12 &&
            date[2]>=1 && date[2]<=31 && date[3]<24 && date[4]<60 && date[5]<60;
        if(rtc_valid){cached_year=date[0];cached_month=date[1];cached_day=date[2];
            cached_hours=date[3];cached_minutes=date[4];cached_seconds=date[5];}
        return;
    }
    uint8_t first_seconds;
    uint8_t second_seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t status_b;

    do {
        wait_for_stable_rtc();
        first_seconds = cmos_read(CMOS_REG_SECONDS);
        minutes = cmos_read(CMOS_REG_MINUTES);
        hours = cmos_read(CMOS_REG_HOURS);
        day = cmos_read(CMOS_REG_DAY);
        month = cmos_read(CMOS_REG_MONTH);
        year = cmos_read(CMOS_REG_YEAR);
        status_b = cmos_read(CMOS_REG_STATUS_B);
        wait_for_stable_rtc();
        second_seconds = cmos_read(CMOS_REG_SECONDS);
    } while (first_seconds != second_seconds);

    if ((status_b & CMOS_STATUS_BINARY) == 0) {
        first_seconds = bcd_to_binary(first_seconds);
        minutes = bcd_to_binary(minutes);
        hours = (uint8_t)((hours & 0x80u) | bcd_to_binary((uint8_t)(hours & 0x7fu)));
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }

    if ((status_b & CMOS_STATUS_24H) == 0 && (hours & 0x80u) != 0) {
        hours = (uint8_t)(((hours & 0x7fu) + 12u) % 24u);
    }

    cached_hours = (uint8_t)(hours % 24u);
    cached_minutes = (uint8_t)(minutes % 60u);
    cached_seconds = (uint8_t)(first_seconds % 60u);
    cached_day = day == 0 ? 1 : (uint8_t)(((day - 1u) % 31u) + 1u);
    cached_month = month == 0 ? 1 : (uint8_t)(((month - 1u) % 12u) + 1u);
    cached_year = (uint16_t)(2000u + (uint16_t)(year % 100u));
    rtc_valid=1;
}

void rtc_init(void) {
    rtc_ready = 1;
    rtc_read_current();
}

uint8_t rtc_hours(void) {
    if (!rtc_ready) {
        rtc_init();
    } else {
        rtc_read_current();
    }

    return cached_hours;
}

uint8_t rtc_minutes(void) {
    if (!rtc_ready) {
        rtc_init();
    } else {
        rtc_read_current();
    }

    return cached_minutes;
}

uint8_t rtc_seconds(void) {
    if (!rtc_ready) {
        rtc_init();
    } else {
        rtc_read_current();
    }

    return cached_seconds;
}

void rtc_get_hms(uint8_t *h, uint8_t *m, uint8_t *s) {
    KERNEL_GUARD(rtc_gate);
    if (!rtc_ready) {
        rtc_init();
    } else {
        rtc_read_current();
    }
    if (h != ((void *)0)) { *h = cached_hours; }
    if (m != ((void *)0)) { *m = cached_minutes; }
    if (s != ((void *)0)) { *s = cached_seconds; }
}

uint8_t rtc_day(void) {
    if (!rtc_ready) {
        rtc_init();
    } else {
        rtc_read_current();
    }

    return cached_day;
}

uint8_t rtc_month(void) {
    if (!rtc_ready) {
        rtc_init();
    } else {
        rtc_read_current();
    }

    return cached_month;
}

uint16_t rtc_year(void) {
    if (!rtc_ready) {
        rtc_init();
    } else {
        rtc_read_current();
    }

    return cached_year;
}

int rtc_epoch_seconds(uint64_t *out) {
    KERNEL_GUARD(rtc_gate);
    if(!out)return -1;if(!rtc_ready)rtc_init();else rtc_read_current();
    if(!rtc_valid)return -1;
    unsigned y=cached_year,m=cached_month,d=cached_day;
    static const unsigned md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<1970 || y>9999 || m<1 || m>12 || !d || cached_hours>23 || cached_minutes>59 || cached_seconds>59)return -1;
    unsigned leap=y%4==0 && (y%100!=0 || y%400==0);
    if(d>md[m-1]+(m==2 && leap))return -1;
    uint64_t days=0;
    for(unsigned i=1970;i<y;i++)days+=365+(i%4==0 && (i%100!=0 || i%400==0));
    for(unsigned i=1;i<m;i++)days+=md[i-1]+(i==2 && leap);
    days+=d-1;*out=days*86400+cached_hours*3600+cached_minutes*60+cached_seconds;return 0;
}
