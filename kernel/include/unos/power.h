#ifndef KYIL_POWER_H
#define KYIL_POWER_H
#include <stdint.h>
enum { POWER_OFF=1, POWER_REBOOT=2 };
void power_start(void);
int power_request(unsigned operation); /* requires SYSTEM_ADMIN */
void power_info(void);
#endif
