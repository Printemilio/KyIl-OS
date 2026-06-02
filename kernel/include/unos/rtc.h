#ifndef UNOS_RTC_H
#define UNOS_RTC_H

#include <stdint.h>

void rtc_init(void);
uint8_t rtc_hours(void);
uint8_t rtc_minutes(void);
uint8_t rtc_seconds(void);
void rtc_get_hms(uint8_t *h, uint8_t *m, uint8_t *s);
uint8_t rtc_day(void);
uint8_t rtc_month(void);
uint16_t rtc_year(void);

#endif
