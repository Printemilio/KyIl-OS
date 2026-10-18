#include <stdint.h>
#define ULTRA_EXPORT_PROBE UINT64_C(0xcafe1234)
typedef struct ultra_probe {uint32_t pid,tid,uid,service,authority;int32_t stdio,ambient,legacy,gui;uint64_t value;} ultra_probe;
