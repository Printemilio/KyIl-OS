#ifndef UNOS_SECURITY_WRITE_WINDOW_H
#define UNOS_SECURITY_WRITE_WINDOW_H
#include <stdint.h>
/* A bounded exact set of native file identities, never content/path hashes.
 * Nine identities preserve the old threshold of eight changes from the first
 * target, without treating an A/B journal as eight unrelated victim files. */
#define SECURITY_WRITE_IDENTITIES 9u
#define SECURITY_WRITE_WINDOW_TICKS 80ull
#define SECURITY_WRITE_THRESHOLD 14u
typedef struct security_write_window {
    uint64_t identities[SECURITY_WRITE_IDENTITIES];
    uint64_t start;
    uint32_t writes,distinct;
} security_write_window_t;
static inline unsigned security_write_window_record(security_write_window_t *w,uint64_t now,uint64_t identity) {
    if(now-w->start>SECURITY_WRITE_WINDOW_TICKS){w->start=now;w->writes=w->distinct=0;}
    if(w->writes!=UINT32_MAX)++w->writes;
    unsigned i=0;while(i<w->distinct && w->identities[i]!=identity)++i;
    if(i==w->distinct && w->distinct<SECURITY_WRITE_IDENTITIES)w->identities[w->distinct++]=identity;
    return w->writes>=SECURITY_WRITE_THRESHOLD && w->distinct>=SECURITY_WRITE_IDENTITIES;
}
#endif
