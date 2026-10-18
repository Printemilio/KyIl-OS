#ifndef KYIL_WINDOWS_WER_H
#define KYIL_WINDOWS_WER_H
#include "exception_abi.h"
typedef struct {uint32_t size,pad;void *process,*thread;exception_record record;win_context context;const uint16_t *report_id;int32_t fatal;uint32_t reserved;} wer_information;
_Static_assert(sizeof(wer_information)==1424 && offsetof(wer_information,context)==176,"AMD64 WER information");
int wr_wer_prepare(void);
int wr_wer_execute(module *);
void wr_wer_report(exception_record *,win_context *);
void *wr_wer_exports(const char *);
int wr_wer_thread(void *,win_context *,unsigned *);
int wr_wer_thread_close(void *);
#endif
