#ifndef UNOS_RTC_H
#define UNOS_RTC_H

#include <stdint.h>

void rtc_init(void);
/* Serialize a native CLOCK provider handoff with the early CMOS fallback. */
int rtc_driver_transaction(int (*operation)(void *),void *context);
/* Platform RTC is UTC; invalid/unavailable dates fail explicitly. */
int rtc_epoch_seconds(uint64_t *out);
uint8_t rtc_hours(void);
uint8_t rtc_minutes(void);
uint8_t rtc_seconds(void);
void rtc_get_hms(uint8_t *h, uint8_t *m, uint8_t *s);
uint8_t rtc_day(void);
uint8_t rtc_month(void);
uint16_t rtc_year(void);

#endif
