#include <unos/rtc_clock_calibration.h>
typedef struct { uint64_t before, after; uint8_t second, mode; } snapshot_t;
typedef struct { uint64_t lower, upper; uint8_t second; } edge_t;

static int snapshot(const rtc_clock_io_t *io, rtc_clock_result_t *r, snapshot_t *s) {
    s->before=io->cycles(io->context);
    if (io->read(io->context,0x0a)&0x80) { ++r->rejected_uip; return 0; }
    uint8_t first=io->read(io->context,0), mode=io->read(io->context,0x0b);
    uint8_t second=io->read(io->context,0), status=io->read(io->context,0x0a);
    uint8_t mode_end=io->read(io->context,0x0b);
    s->after=io->cycles(io->context);
    if (s->after<s->before) return -2;
    if (status&0x80) { ++r->rejected_uip; return 0; }
    if (first!=second || ((mode^mode_end)&0x84)) { ++r->rejected_torn; return 0; }
    if (mode&0x80) { ++r->rejected_invalid; return 0; } /* SET: clock frozen */
    if (!(mode&4)) {
        if ((first&15)>9 || (first>>4)>5) { ++r->rejected_invalid; return 0; }
        first=(first&15)+(first>>4)*10;
    }
    if (first>=60) { ++r->rejected_invalid; return 0; }
    s->second=first; s->mode=mode&4;
    return 1;
}
int rtc_clock_measure(const rtc_clock_io_t *io, rtc_clock_result_t *r) {
    *r=(rtc_clock_result_t){0};
    snapshot_t previous={0}; edge_t edges[3];
    unsigned have_previous=0, edge_count=0;
    for (uint32_t poll=0;poll<io->poll_budget;++poll) {
        ++r->polls;
        snapshot_t current;
        int valid=snapshot(io,r,&current);
        if (valid<0) return valid;
        if (!valid) continue;
        if (have_previous && current.before<previous.after) return -2;
        if (!have_previous) { previous=current; have_previous=1; continue; }
        if (current.mode!=previous.mode ||
            (current.second!=previous.second && current.second!=(previous.second+1)%60)) {
            ++r->discontinuities; edge_count=0; previous=current; continue;
        }
        if (current.second==previous.second) { previous=current; continue; }
        /* The real RTC edge lies between our last old-second observation and
         * our first stable new-second observation. This includes UIP/read
         * latency without pretending the timestamp is an exact edge. */
        edges[edge_count++]=(edge_t){previous.before,current.after,current.second};
        previous=current;
        if (edge_count!=3) continue;
        uint64_t lo[2],hi[2];
        for (unsigned i=0;i<2;++i) {
            lo[i]=edges[i+1].lower>edges[i].upper ? edges[i+1].lower-edges[i].upper : 0;
            hi[i]=edges[i+1].upper-edges[i].lower;
            r->interval[i]=edges[i+1].upper-edges[i].upper;
        }
        uint64_t lower=lo[0]>lo[1]?lo[0]:lo[1];
        uint64_t upper=hi[0]<hi[1]?hi[0]:hi[1];
        if (!lower || lower>upper) {
            ++r->inconsistent_intervals;
            edges[0]=edges[2]; edge_count=1; continue;
        }
        r->lower_hz=lower; r->upper_hz=upper;
        r->frequency=lower+(upper-lower)/2;
        for (unsigned i=0;i<3;++i) r->edge_seconds[i]=edges[i].second;
        return 0;
    }
    return -1;
}
