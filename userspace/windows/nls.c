#include "windows.h"
#include "nls_data.h"
static unsigned locale_id(unsigned locale){return locale==0x400 || locale==0x800?0x409:locale;}
static int nls_error(unsigned code){wr_current_teb()->last_error=code;return 0;}
unsigned wr_unicode_case(unsigned c,int upper) {
    unsigned low=0,high=sizeof(nls_case)/sizeof(nls_case[0]);
    while(low<high){unsigned at=low+(high-low)/2;if(c<nls_case[at].source)high=at;else if(c>nls_case[at].source)low=at+1;else return upper?nls_case[at].upper:nls_case[at].lower;}
    return c;
}
static int WR_WIN GetStringTypeW(unsigned kind,const uint16_t *source,int count,uint16_t *out) {
    if(kind!=1 && kind!=2 && kind!=4)return nls_error(1004);
    if(!source || !out || (const void *)source==(void *)out || count==0)return nls_error(87);
    if(count<0){count=0;while(source[count]){if(count==INT32_MAX-1)return nls_error(8);count++;}count++;}
    for(int i=0;i<count;i++)if(source[i]>=0xd800 && source[i]<=0xdfff)return nls_error(50);
    unsigned column=kind==1?0:(kind==2?1:2);
    for(int i=0;i<count;i++){unsigned c=source[i],low=0,high=sizeof(nls_types)/sizeof(nls_types[0]);
        while(low<high){unsigned at=low+(high-low)/2;if(c<nls_types[at].first)high=at;else if(c>nls_types[at].last)low=at+1;else {out[i]=nls_types[at].value[column];break;}}}
    return 1;
}
static int WR_WIN LCMapStringW(unsigned locale,unsigned flags,const uint16_t *source,int count,uint16_t *out,int capacity) {
    locale=locale_id(locale);if(locale!=0x409 && locale!=0x7f)return nls_error(87);
    if(flags!=0x100 && flags!=0x200){
        const unsigned known=0x00000f3fu|0x00071000u|0x07f00000u|0x08000000u|0x20000000u|0x40000000u;
        return nls_error(!flags || (flags&~known)?1004:50);
    }
    if(!source || !count || capacity<0 || (!out && capacity))return nls_error(87);
    if(count<0){count=0;while(source[count]){if(count==INT32_MAX-1)return nls_error(8);count++;}count++;}
    for(int i=0;i<count;i++)if(source[i]>=0xd800 && source[i]<=0xdfff)return nls_error(50);
    if(!capacity)return count;if(capacity<count)return nls_error(122);
    for(int i=0;i<count;i++)out[i]=(uint16_t)wr_unicode_case(source[i],flags==0x200);return count;
}
static int WR_WIN GetLocaleInfoW(unsigned locale,unsigned type,uint16_t *out,int capacity) {
    locale=locale_id(locale);if(capacity<0 || (!out && capacity))return nls_error(87);
    unsigned flags=type&0xffff0000u;type&=0xffff;
    if(flags&~0xa0000000u)return nls_error(1004);
    for(unsigned i=0;i<sizeof(nls_locales)/sizeof(nls_locales[0]);i++)if(nls_locales[i].locale==locale && nls_locales[i].type==type) {
        if(flags&0x20000000u){if(!nls_locales[i].has_number)return nls_error(1004);if(!capacity)return 2;if(capacity<2)return nls_error(122);memcpy(out,&nls_locales[i].number,4);return 2;}
        unsigned length=nls_locales[i].length;if(!capacity)return (int)length;
        if((unsigned)capacity<length)return nls_error(122);memcpy(out,nls_locales[i].text,length*2);return (int)length;
    }
    return nls_error(87);
}
static unsigned WR_WIN GetUserDefaultLCID(void){return 0x409;}
static unsigned WR_WIN GetSystemDefaultLCID(void){return 0x409;}
static unsigned WR_WIN GetThreadLocale(void){return wr_current_teb()->current_locale;}
static int WR_WIN SetThreadLocale(unsigned locale){
    locale=locale_id(locale);if(locale!=0x409 && locale!=0x7f)return nls_error(87);
    wr_current_teb()->current_locale=locale;return 1;
}
static int WR_WIN GetUserDefaultLocaleName(uint16_t *out,int capacity){
    /* LOCALE_SNAME from the installed NLS data, including its terminator. */
    if(!out || capacity<6)return nls_error(122);
    return GetLocaleInfoW(0x409,0x5c,out,capacity);
}
static int WR_WIN EnumSystemLocalesEx(int (WR_WIN *callback)(uint16_t *,unsigned,intptr_t),unsigned flags,intptr_t param,void *reserved){
    if(!callback || reserved)return nls_error(87);
    if(flags&~0x37u)return nls_error(1004);
    /* The personality currently installs one specific Windows-format locale.
       Other filters legitimately enumerate no entries; no unavailable locale
       or alternate sort is advertised. Combined filters form a union. */
    if(flags && !(flags&(1u|0x20u)))return 1;
    uint16_t name[6];if(!GetUserDefaultLocaleName(name,6))return 0;
    callback(name,0x21,param);return 1;
}
static uint16_t WR_WIN GetUserDefaultLangID(void){return 0x409;}
static int WR_WIN IsValidLocale(unsigned locale,unsigned flags){return (flags==1 || flags==2) && (locale==0x409 || locale==0x7f);}
static int WR_WIN EnumSystemLocalesW(int (WR_WIN *callback)(uint16_t *),unsigned flags) {
    if(!callback || (flags!=1 && flags!=2))return nls_error(87);
    uint16_t name[]={'0','0','0','0','0','4','0','9',0};callback(name);return 1;
}
typedef struct {unsigned max;uint8_t fallback[2],lead[12];} cp_info;
static int WR_WIN GetCPInfo(unsigned page,cp_info *out) {
    if(!out)return nls_error(87);if(page!=0 && page!=1 && page!=3 && page!=65001)return nls_error(87);
    *out=(cp_info){4,{'?',0},{0}};return 1;
}
/* Initial exact subset: invariant/en-US alphanumeric basic-Latin collation.
 * Unsupported weights/locales/flags fail explicitly; this is not ordinal
 * comparison advertised as full Windows NLS. Locale data expands separately. */
static int WR_WIN CompareStringW(unsigned locale,unsigned flags,const uint16_t *a,int na,const uint16_t *b,int nb) {
    if((locale!=0x7f && locale!=0x409) || (flags&~1u)) {wr_current_teb()->last_error=50;return 0;}
    if(!a || !b) {wr_current_teb()->last_error=87;return 0;}
    if(na<0) {na=0;while(a[na])na++;}if(nb<0) {nb=0;while(b[nb])nb++;}
    for(int i=0;i<na;i++)if(!((a[i]>='A'&&a[i]<='Z') || (a[i]>='a'&&a[i]<='z') || (a[i]>='0'&&a[i]<='9'))) {wr_current_teb()->last_error=50;return 0;}
    for(int i=0;i<nb;i++)if(!((b[i]>='A'&&b[i]<='Z') || (b[i]>='a'&&b[i]<='z') || (b[i]>='0'&&b[i]<='9'))) {wr_current_teb()->last_error=50;return 0;}
    int tertiary=0;
    for(int i=0;i<na && i<nb;i++) {
        unsigned x=a[i],y=b[i],xx=(x>='A'&&x<='Z')?x+32:x,yy=(y>='A'&&y<='Z')?y+32:y;
        if(xx!=yy)return xx<yy?1:3;
        if(!tertiary && x!=y)tertiary=x>y?-1:1;
    }
    if(na!=nb)return na<nb?1:3;
    return (flags&1) || !tertiary?2:(tertiary<0?1:3);
}
static unsigned WR_WIN GetACP(void) {return 65001; /* Actual configured UTF-8 narrow encoding. */}
/* lstrcmpi uses linguistic comparison in the current configured locale,
 * not an ordinal casefold. Preserve CompareString's explicit subset errors. */
static int WR_WIN lstrcmpiW(const uint16_t *a,const uint16_t *b) {
    return CompareStringW(locale_id(0x400),1,a,-1,b,-1)-2;
}
static int WR_WIN lstrcmpiA(const char *a,const char *b) {
    if(!a || !b)return nls_error(87)-2;
    /* The configured ACP is UTF-8. All strings in the currently supported
       linguistic weight subset are ASCII, so conversion is lossless here. */
    const char *strings[2]={a,b};
    for(unsigned k=0;k<2;k++)for(const unsigned char *p=(const void *)strings[k];*p;p++)
        if(!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9')))return nls_error(50)-2;
    while(*a && *b){unsigned x=(uint8_t)*a++,y=(uint8_t)*b++;if(x>='A' && x<='Z')x+=32;if(y>='A' && y<='Z')y+=32;
        if(x!=y)return x<y?-1:1;}
    return *a?1:*b?-1:0;
}
static int WR_WIN CompareStringOrdinal(const uint16_t *a,int na,const uint16_t *b,int nb,int ignore_case){
    if(!a || !b || na< -1 || nb< -1)return nls_error(87);
    if(na==-1){na=0;while(a[na]){if(na==INT32_MAX-1)return nls_error(87);na++;}}
    if(nb==-1){nb=0;while(b[nb]){if(nb==INT32_MAX-1)return nls_error(87);nb++;}}
    /* UTF-16 code-unit ordering. The installed case table is BMP only;
       don't pretend supplementary case folding is represented. */
    if(ignore_case){for(int i=0;i<na;i++)if(a[i]>=0xd800 && a[i]<=0xdfff)return nls_error(50);
        for(int i=0;i<nb;i++)if(b[i]>=0xd800 && b[i]<=0xdfff)return nls_error(50);}
    for(int i=0;i<na && i<nb;i++){unsigned x=a[i],y=b[i];if(ignore_case){x=wr_unicode_case(x,1);y=wr_unicode_case(y,1);}if(x!=y)return x<y?1:3;}
    return na==nb?2:(na<nb?1:3);
}
static unsigned WR_WIN LocaleNameToLCID(const uint16_t *name,unsigned flags){
    if(flags&~0x08000000u){nls_error(1004);return 0;}
    if(!name)return 0x409;
    if(!name[0])return 0x7f;
    const uint16_t expected[]={'e','n','-','U','S',0};
    unsigned i=0;while(name[i] && expected[i] && wr_unicode_case(name[i],1)==wr_unicode_case(expected[i],1))i++;
    if(!name[i] && !expected[i])return 0x409;
    nls_error(87);return 0;
}
static int WR_WIN LCIDToLocaleName(unsigned locale,uint16_t *out,int count,unsigned flags){
    if(count<0 || (!out && count) || (flags&~0x08000000u))return nls_error(87);
    locale=locale_id(locale);const uint16_t en[]={'e','n','-','U','S',0};
    if(locale!=0x409 && locale!=0x7f)return nls_error(87);unsigned need=locale==0x7f?1:6;
    if(!count)return (int)need;if((unsigned)count<need)return nls_error(122);
    if(locale==0x7f)*out=0;else memcpy(out,en,sizeof(en));return (int)need;
}
static int WR_WIN ResolveLocaleName(const uint16_t *locale,uint16_t *out,int count){
    unsigned id=LocaleNameToLCID(locale,0);return id?LCIDToLocaleName(id,out,count,0):0;
}
static int WR_WIN GetLocaleInfoEx(const uint16_t *locale,unsigned type,uint16_t *out,int count){
    unsigned id=LocaleNameToLCID(locale,0);return id?GetLocaleInfoW(id,type,out,count):0;
}
static int WR_WIN LCMapStringEx(const uint16_t *locale,unsigned flags,const uint16_t *source,int n,uint16_t *out,int cap,void *version,void *reserved,intptr_t param){
    if(version || reserved || param)return nls_error(50);unsigned id=LocaleNameToLCID(locale,0);
    return id?LCMapStringW(id,flags,source,n,out,cap):0;
}
static uint16_t WR_WIN GetUserDefaultUILanguage(void){return GetUserDefaultLangID();}
void *wr_nls_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(lstrcmpiA);E(lstrcmpiW);E(GetStringTypeW);E(LCMapStringW);E(GetLocaleInfoW);E(GetUserDefaultLCID);E(GetUserDefaultLangID);E(IsValidLocale);E(EnumSystemLocalesW);E(GetCPInfo);
    E(GetSystemDefaultLCID);E(GetThreadLocale);E(SetThreadLocale);E(GetUserDefaultLocaleName);E(EnumSystemLocalesEx);
    E(CompareStringOrdinal);E(LocaleNameToLCID);E(LCIDToLocaleName);E(ResolveLocaleName);E(GetLocaleInfoEx);E(LCMapStringEx);E(GetUserDefaultUILanguage);
#undef E
    if(wr_equal(name,"CompareStringW",0))return (void *)&CompareStringW;
    if(wr_equal(name,"GetACP",0))return (void *)&GetACP;
    return 0;
}
