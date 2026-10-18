#include <stdint.h>
typedef uint16_t W;
typedef struct {W year,month,weekday,day,hour,minute,second,ms;} time;
typedef struct {int32_t bias;W standard_name[32];time standard;int32_t sbias;W daylight_name[32];time daylight;int32_t dbias;} zone;
__declspec(dllimport) int GetStringTypeW(unsigned,const W *,int,W *),LCMapStringW(unsigned,unsigned,const W *,int,W *,int),GetLocaleInfoW(unsigned,unsigned,W *,int),GetDateFormatW(unsigned,unsigned,const time *,const W *,W *,int),GetTimeFormatW(unsigned,unsigned,const time *,const W *,W *,int),SystemTimeToFileTime(const time *,uint64_t *),SystemTimeToTzSpecificLocalTime(const zone *,const time *,time *),EnumSystemLocalesW(int (*)(W *),unsigned),GetCPInfo(unsigned,void *);
__declspec(dllimport) unsigned GetUserDefaultLCID(void),GetTimeZoneInformation(zone *),GetLastError(void);
__declspec(dllimport) W GetUserDefaultLangID(void);
__declspec(dllimport) void GetLocalTime(time *),OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static int equal(const W *a,const W *b){while(*a==*b){if(!*a)return 1;a++;b++;}return 0;}
static unsigned locales;
static int visit(W *name){check(equal(name,(const W *)L"00000409"),"profile: installed locale FAIL\n");locales++;return 0;}
void mainCRTStartup(void) {
    W text[128],types[4];check(GetStringTypeW(1,(const W *)L"A9 \u00e9",4,types) && (types[0]&1) && (types[1]&4) && (types[2]&8) && (types[3]&2),"profile: Unicode properties FAIL\n");
    check(LCMapStringW(0x409,0x200,(const W *)L"\u00e9clair",-1,text,128)==7 && equal(text,(const W *)L"\u00c9CLAIR"),"profile: real casing FAIL\n");
    check(!LCMapStringW(0x409,0x400,(const W *)L"abc",-1,text,128) && GetLastError()==50,"profile: sort key honest refusal FAIL\n");
    W pair[]={0xd801,0xdc00,0};check(!LCMapStringW(0x409,0x200,pair,-1,text,128) && GetLastError()==50,"profile: supplementary refusal FAIL\n");
    check(GetUserDefaultLCID()==0x409 && GetUserDefaultLangID()==0x409 && GetLocaleInfoW(0x409,0xe,text,128)==2 && equal(text,(const W *)L"."),"profile: installed locale policy FAIL\n");
    unsigned number=99;check(GetLocaleInfoW(0x409,0x20000011u,(W *)&number,2)==2 && number==2,"profile: numeric locale info FAIL\n");
    check(EnumSystemLocalesW(visit,1) && locales==1,"profile: stop enumeration FAIL\n");
    struct {unsigned max;unsigned char fallback[2],lead[12];} cp;check(GetCPInfo(65001,&cp) && cp.max==4 && cp.fallback[0]=='?' && !cp.lead[0],"profile: UTF8 code page FAIL\n");
    time epoch={1601,1,0,1,0,0,0,0},t={2024,2,0,29,13,7,9,0},local;uint64_t ticks;
    check(SystemTimeToFileTime(&epoch,&ticks) && !ticks,"profile: calendar epoch FAIL\n");
    check(GetDateFormatW(0x409,0,&t,(const W *)L"dddd, yyyy-MM-dd",text,128) && equal(text,(const W *)L"Thursday, 2024-02-29"),"profile: date format FAIL\n");
    check(GetTimeFormatW(0x409,0,&t,(const W *)L"h:mm:ss tt",text,128) && equal(text,(const W *)L"1:07:09 PM"),"profile: time format FAIL\n");
    zone utc;check(!GetTimeZoneInformation(&utc) && !utc.bias && !utc.standard.month,"profile: UTC policy FAIL\n");
    static const zone us={.bias=300,.standard={0,11,0,1,2,0,0,0},.daylight={0,3,0,2,2,0,0,0},.dbias=-60};
    t.month=7;t.day=1;check(SystemTimeToTzSpecificLocalTime(&us,&t,&local) && local.hour==9,"profile: DST rules FAIL\n");
    GetLocalTime(&t);check(t.year>=2020 && t.month>=1 && t.month<=12,"profile: actual realtime FAIL\n");
    OutputDebugStringA("windows-profile: BMP / locale / UTF8 / calendar / UTC-DST / negative semantics PASS\n");ExitProcess(0);
}
