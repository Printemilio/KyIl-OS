/* Component differential tests. Host APIs are references only; this executable
 * is never staged into KyIlOS. The guest has its own C implementations/tables. */
#include "../userspace/windows/windows.h"
extern int printf(const char *,...);
__declspec(dllimport) void *LoadLibraryA(const char *),*GetProcAddress(void *,const char *);
__declspec(dllimport) unsigned GetLastError(void);
int wr_equal(const char *a,const char *b,int fold){(void)fold;while(*a==*b){if(!*a)return 1;a++;b++;}return 0;}
void *wr_system_exports(const char *name){return GetProcAddress(LoadLibraryA("kernel32.dll"),name);}
static unsigned seed=0x98e40156,cases;
static unsigned next(void){seed=seed*1664525+1013904223;return seed;}
static int equal16(const uint16_t *a,const uint16_t *b){while(*a==*b){if(!*a)return 1;a++;b++;}return 0;}
typedef struct {uint16_t y,m,w,d,h,min,s,ms;} time;
typedef struct {int32_t bias;uint16_t names[32];time standard;int32_t sbias;uint16_t dnames[32];time daylight;int32_t dbias;} zone;
int main(void) {
    typedef int (WR_WIN *ctype)(unsigned,const uint16_t *,int,uint16_t *);
    typedef int (WR_WIN *map)(unsigned,unsigned,const uint16_t *,int,uint16_t *,int);
    typedef int (WR_WIN *format)(unsigned,unsigned,const time *,const uint16_t *,uint16_t *,int);
    typedef int (WR_WIN *convert)(const time *,uint64_t *);
    typedef int (WR_WIN *local)(const zone *,const time *,time *);
    ctype hc=wr_system_exports("GetStringTypeW"),vc=wr_nls_exports("GetStringTypeW");
    map hm=wr_system_exports("LCMapStringW"),vm=wr_nls_exports("LCMapStringW");
    for(unsigned code=0;code<65536;code++) {
        if(code>=0xd800 && code<=0xdfff)continue;uint16_t c=(uint16_t)code,a[8],b[8];
        for(unsigned kind=1;kind<=4;kind*=2){if(!hc(kind,&c,1,a) || !vc(kind,&c,1,b) || a[0]!=b[0]){printf("profile: ctype mismatch %x/%u\n",code,kind);return 1;}cases++;}
        for(unsigned flag=0x100;flag<=0x200;flag+=0x100){int x=hm(0x7f,flag,&c,1,a,8),y=vm(0x7f,flag,&c,1,b,8);
            if(x!=y || x!=1 || a[0]!=b[0]){printf("profile: case mismatch %x/%x\n",code,flag);return 1;}cases++;}
    }
    format hd=wr_system_exports("GetDateFormatW"),vd=wr_calendar_exports("GetDateFormatW"),ht=wr_system_exports("GetTimeFormatW"),vt=wr_calendar_exports("GetTimeFormatW");
    convert hcvt=wr_system_exports("SystemTimeToFileTime"),vcvt=wr_calendar_exports("SystemTimeToFileTime");
    local hl=wr_system_exports("SystemTimeToTzSpecificLocalTime"),vl=wr_calendar_exports("SystemTimeToTzSpecificLocalTime");
    const uint16_t *dates[]={0,(const uint16_t *)L"yyyy-MM-dd",(const uint16_t *)L"dddd, MMMM d, yyyy",(const uint16_t *)L"d 'it''s' MMM yy"};
    const uint16_t *times[]={0,(const uint16_t *)L"HH:mm:ss",(const uint16_t *)L"h 'o''clock' tt"};
    zone zones[3]={{0},{.bias=300,.standard={0,11,0,1,2,0,0,0},.daylight={0,3,0,2,2,0,0,0},.dbias=-60},
        {.bias=-600,.standard={0,4,0,1,3,0,0,0},.daylight={0,10,0,1,2,0,0,0},.dbias=-60}};
    for(unsigned k=0;k<10000;k++) {
        time t={(uint16_t)(1900+next()%200),(uint16_t)(1+next()%12),0,(uint16_t)(1+next()%28),
            (uint16_t)(next()%24),(uint16_t)(next()%60),(uint16_t)(next()%60),(uint16_t)(next()%1000)};
        uint64_t a,b;if(!hcvt(&t,&a) || !vcvt(&t,&b) || a!=b){printf("profile: calendar mismatch\n");return 1;}cases++;
        for(unsigned j=0;j<3;j++){time x,y;if(!hl(&zones[j],&t,&x) || !vl(&zones[j],&t,&y))return 2;
            const unsigned char *aa=(const void *)&x,*bb=(const void *)&y;for(unsigned i=0;i<sizeof(x);i++)if(aa[i]!=bb[i]){printf("profile: timezone mismatch\n");return 1;}cases++;}
        for(unsigned j=0;j<4;j++){uint16_t x[256],y[256];int xx=hd(0x409,dates[j]?0:0x80000000u,&t,dates[j],x,256),yy=vd(0x409,dates[j]?0:0x80000000u,&t,dates[j],y,256);
            if(xx!=yy || !xx || !equal16(x,y)){printf("profile: date mismatch iteration=%u pattern=%u native=%d vex=%d err=%u\n",k,j,xx,yy,GetLastError());return 1;}cases++;}
        for(unsigned j=0;j<3;j++){uint16_t x[256],y[256];int xx=ht(0x409,times[j]?0:0x80000000u,&t,times[j],x,256),yy=vt(0x409,times[j]?0:0x80000000u,&t,times[j],y,256);
            if(xx!=yy || !xx || !equal16(x,y)){printf("profile: time mismatch iteration=%u pattern=%u native=%d vex=%d\n",k,j,xx,yy);return 1;}cases++;}
    }
    printf("windows-profile-reference: %u BMP/case/calendar/format/DST actual Windows comparisons PASS\n",cases);return 0;
}
