#ifndef KYIL_PROCESS_RESOURCE_H
#define KYIL_PROCESS_RESOURCE_H
#include <unos/vex_process_abi.h>
int64_t vex_process_resource_call(const vex_process_resource_req_t *);
struct vex_web_fd_object;
int vex_ultra_export_endpoint(struct vex_web_fd_object **);
int vex_process_resource_transfer_allowed(void *);
#endif
