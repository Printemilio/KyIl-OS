#ifndef KYIL_WINDOWS_JOB_H
#define KYIL_WINDOWS_JOB_H
#include "process.h"
#include <unos/vex_abi.h>
int wr_native_service(const char *,const vex_process_grant_t *,unsigned);
int wr_process_capability(void *);
int wr_job_service(void);
void *wr_job_exports(const char *);
int wr_job_close(void *);
void *wr_job_duplicate(void *,int);
int wr_job_wait(void *,unsigned,unsigned *);
int wr_job_flags(void *,unsigned,unsigned,unsigned *);
void wr_job_quiesce(int);
int wr_job_current_fd(void);
int wr_job_adopt(void);
int wr_job_add_child(int);
int wr_job_launch_begin(void);
void wr_job_launch_end(void);
#endif
