#include "windows.h"
typedef struct {uint16_t year,month,weekday,day,hour,minute,second,ms;} calendar_time;
typedef struct {int32_t bias;uint16_t standard_name[32];calendar_time standard;int32_t standard_bias;
    uint16_t daylight_name[32];calendar_time daylight;int32_t daylight_bias;} timezone;
_Static_assert(sizeof(timezone)==172,"TIME_ZONE_INFORMATION");
static int error(unsigned e){wr_current_teb()->last_error=e;return 0;}
static unsigned days_month(unsigned y,unsigned m){static const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};return days[m-1]+(m==2 && (!(y%4) && (y%100 || !(y%400))));}
static int to_ticks(const calendar_time *s,uint64_t *out) {
    if(!s || s->year<1601 || s->year>30827 || s->month<1 || s->month>12 || s->day<1 || s->day>days_month(s->year,s->month) || s->hour>23 || s->minute>59 || s->second>59 || s->ms>999)return 0;
    int64_t y=s->year-(s->month<=2),era=y/400;unsigned yoe=(unsigned)(y-era*400),m=s->month;
    unsigned doy=(153*(m+(m>2?-3:9))+2)/5+s->day-1,doe=yoe*365+yoe/4-yoe/100+doy;
    int64_t days=era*146097+doe-719468+134774;
    *out=(((uint64_t)days*86400+s->hour*3600+s->minute*60+s->second)*1000+s->ms)*10000;return 1;
}
static int from_ticks(uint64_t ticks,calendar_time *out) {
    int (WR_WIN *convert)(const uint64_t *,calendar_time *)=wr_system_exports("FileTimeToSystemTime");return convert(&ticks,out);
}
static int WR_WIN SystemTimeToFileTime(const calendar_time *s,uint64_t *out){uint64_t time;if(!out || !to_ticks(s,&time))return error(87);*out=time;return 1;}
static unsigned WR_WIN GetTimeZoneInformation(timezone *out) {
    if(!out){error(87);return UINT32_MAX;}timezone value={0};value.standard_name[0]='U';value.standard_name[1]='T';value.standard_name[2]='C';*out=value;return 0;
    /* This personality has an explicit UTC profile, not the host Windows zone.
     * RTC/clock data remains the actual native realtime clock. */
}
static int shifted(uint64_t in,int64_t minutes,uint64_t *out) {
    if(minutes>INT64_MAX/600000000ll || minutes<INT64_MIN/600000000ll)return 0;
    int64_t ticks=minutes*600000000ll;
    if(ticks<0){uint64_t magnitude=(uint64_t)(-(ticks+1))+1;if(in<magnitude)return 0;*out=in-magnitude;}
    else {if(in>(uint64_t)INT64_MAX-(uint64_t)ticks)return 0;*out=in+(uint64_t)ticks;}return 1;
}
static int transition(calendar_time rule,unsigned year,uint64_t *out) {
    if(!rule.year) {
        if(rule.month<1 || rule.month>12 || rule.day<1 || rule.day>5 || rule.weekday>6)return 0;
        unsigned week=rule.day,dow=rule.weekday;rule.year=(uint16_t)year;rule.day=1;
        uint64_t first;if(!to_ticks(&rule,&first))return 0;calendar_time actual;if(!from_ticks(first,&actual))return 0;
        unsigned day=1+(dow+7-actual.weekday)%7+(week-1)*7,limit=days_month(year,rule.month);
        if(day>limit)day-=7;rule.day=(uint16_t)day;
    }else if(rule.year!=year)return 0;
    return to_ticks(&rule,out);
}
static int WR_WIN SystemTimeToTzSpecificLocalTime(const timezone *zone,const calendar_time *utc,calendar_time *local) {
    timezone current;if(!zone){GetTimeZoneInformation(&current);zone=&current;}
    uint64_t time,start,end,converted;calendar_time normalized;
    if(!local || !to_ticks(utc,&time) || !from_ticks(time,&normalized))return error(87);
    int64_t bias=zone->bias+ (int64_t)zone->standard_bias;
    if(zone->standard.month || zone->daylight.month) {
        if(!zone->standard.month || !zone->daylight.month || !transition(zone->daylight,normalized.year,&start) || !transition(zone->standard,normalized.year,&end) ||
            !shifted(start,bias,&start) || !shifted(end,zone->bias+(int64_t)zone->daylight_bias,&end))return error(87);
        if(start<end?(time>=start && time<end):(time>=start || time<end))bias=zone->bias+(int64_t)zone->daylight_bias;
    }
    if(!shifted(time,-bias,&converted) || !from_ticks(converted,local))return error(87);return 1;
}
static void WR_WIN GetLocalTime(calendar_time *out) {
    void (WR_WIN *clock)(calendar_time *)=wr_system_exports("GetSystemTime");clock(out);
}
typedef struct {uint16_t *out;unsigned count,capacity;int overflow;} writer;
static void emit(writer *w,unsigned c){if(w->count==INT32_MAX){w->overflow=1;return;}if(w->out && w->count<w->capacity)w->out[w->count]=(uint16_t)c;w->count++;}
static void number(writer *w,unsigned v,unsigned minimum){uint16_t digits[16];unsigned n=0;do{digits[n++]=(uint16_t)('0'+v%10);v/=10;}while(v);while(n<minimum)digits[n++]='0';while(n)emit(w,digits[--n]);}
static int locale_text(unsigned locale,unsigned type,uint16_t *out,unsigned capacity) {
    int (WR_WIN *get)(unsigned,unsigned,uint16_t *,int)=wr_nls_exports("GetLocaleInfoW");return get(locale,type|0x80000000u,out,(int)capacity);
}
static int format(unsigned locale,unsigned flags,const calendar_time *provided,const uint16_t *pattern,uint16_t *out,int capacity,int date) {
    if(capacity<0 || (!out && capacity))return error(87);if(locale==0x400 || locale==0x800)locale=0x409;
    if(locale!=0x409 && locale!=0x7f)return error(87);
    if(date && pattern && flags)return error(1004);
    flags&=~0x80000000u;if(date){if(flags&~11u)return error(1004);if(flags && flags!=1 && flags!=2 && flags!=8)return error(1004);}
    else {if(flags&~15u)return error(1004);if(flags&7)return error(50);}
    calendar_time time;if(provided)time=*provided;else GetLocalTime(&time);
    if(date){uint64_t ticks;time.hour=time.minute=time.second=time.ms=0;if(!to_ticks(&time,&ticks) || !from_ticks(ticks,&time))return error(87);}
    else if(time.hour>23 || time.minute>59 || time.second>59 || time.ms>999)return error(87);
    uint16_t automatic[256];if(!pattern){unsigned type=date?(flags==2?0x20:(flags==8?0x1006:0x1f)):0x1003;
        if(!locale_text(locale,type,automatic,256))return 0;pattern=automatic;}
    writer w={out,0,(unsigned)capacity,0};unsigned pos=0;
    while(pattern[pos]) {
        unsigned c=pattern[pos++];if(c=='\'') {
            if(pattern[pos]=='\''){emit(&w,'\'');pos++;continue;}
            while(pattern[pos]){if(pattern[pos]=='\''){pos++;if(pattern[pos]!='\'')break;}
                emit(&w,pattern[pos++]);}continue;
        }
        unsigned repeat=1;while(pattern[pos]==c){pos++;repeat++;}
        unsigned value=0,type=0;int numeric=0;
        if(date && c=='d'){if(repeat<3){value=time.day;numeric=1;}else type=(repeat==3?0x31:0x2a)+(time.weekday+6)%7;}
        else if(date && c=='M'){if(repeat<3){value=time.month;numeric=1;}else type=(repeat==3?0x44:0x38)+time.month-1;}
        else if(date && c=='y'){value=repeat<3?time.year%100:time.year;numeric=1;}
        else if(!date && (c=='h' || c=='H')){value=c=='H' || (flags&8)?time.hour:(time.hour%12?time.hour%12:12);numeric=1;}
        else if(!date && c=='m'){value=time.minute;numeric=1;}
        else if(!date && c=='s'){value=time.second;numeric=1;}
        else if(!date && c=='t')type=time.hour<12?0x28:0x29;
        if(numeric)number(&w,value,date && c=='y' && repeat>=3?4:(repeat>=2?2:1));
        else if(type){uint16_t text[128];if(!locale_text(locale,type,text,128))return 0;
            unsigned i=0;while(text[i]){emit(&w,text[i++]);if(c=='t' && repeat==1)break;}}
        else {if(date && c=='g')return error(50);while(repeat--)emit(&w,c);}
    }
    emit(&w,0);if(w.overflow)return error(8);if(!capacity)return (int)w.count;if(w.count>(unsigned)capacity)return error(122);return (int)w.count;
}
static int WR_WIN GetDateFormatW(unsigned l,unsigned f,const calendar_time *t,const uint16_t *p,uint16_t *o,int n){return format(l,f,t,p,o,n,1);}
static int WR_WIN GetTimeFormatW(unsigned l,unsigned f,const calendar_time *t,const uint16_t *p,uint16_t *o,int n){return format(l,f,t,p,o,n,0);}
void *wr_calendar_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(SystemTimeToFileTime);E(GetTimeZoneInformation);E(SystemTimeToTzSpecificLocalTime);E(GetLocalTime);E(GetDateFormatW);E(GetTimeFormatW);
#undef E
    return 0;
}
