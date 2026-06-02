#ifndef UNOS_TIMEZONE_H
#define UNOS_TIMEZONE_H

#include <stdint.h>

void timezone_init(void);
int8_t timezone_offset_hours(void);

#endif
