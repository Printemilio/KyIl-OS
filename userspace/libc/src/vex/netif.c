/* Network interface names. Vex has one Ethernet interface, eth0 (index 1),
 * when the kernel found a network card; there is no loopback interface. */
#define _GNU_SOURCE
#include <net/if.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "syscall.h"

static int have_nic(void)
{
	vex_bulk_result_t r;
	vex_bulk_req_t req = { .mask = VEX_BULK_NETWORK, .result = &r };
	memset(&r, 0, sizeof r);
	if (__vex_call1(VEX_SYS_BULK, __vex_ptr(&req)) == (uint64_t)-1) return 0;
	return (r.valid_mask & VEX_BULK_NETWORK) != 0;
}

unsigned if_nametoindex(const char *name)
{
	if (name && !strcmp(name, "eth0") && have_nic()) return 1;
	errno = ENXIO;
	return 0;
}

char *if_indextoname(unsigned index, char *name)
{
	if (index == 1 && have_nic()) return strcpy(name, "eth0");
	errno = ENXIO;
	return 0;
}

struct if_nameindex *if_nameindex(void)
{
	struct if_nameindex *list = calloc(2, sizeof *list + IF_NAMESIZE);
	if (!list) return 0;
	if (have_nic()) {
		char *name = (char *)(list + 2);
		strcpy(name, "eth0");
		list[0].if_index = 1;
		list[0].if_name = name;
	}
	return list;
}
