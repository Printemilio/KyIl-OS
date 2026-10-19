#include <stdint.h>
typedef uint16_t W;
typedef struct {uint16_t length,maximum;unsigned pad;W *buffer;} unicode;
typedef struct {unsigned size,pad;void *root;unicode *name;unsigned flags,pad2;void *security,*qos;} attributes;
__declspec(dllimport) unsigned NtCreateKey(void **,unsigned,attributes *,unsigned,unicode *,unsigned,unsigned *),NtOpenKeyEx(void **,unsigned,attributes *,unsigned),NtDeleteKey(void *),NtSetValueKey(void *,unicode *,unsigned,unsigned,const void *,unsigned),NtQueryValueKey(void *,unicode *,unsigned,void *,unsigned,unsigned *),NtClose(void *),RtlFormatCurrentUserKeyPath(unicode *);
__declspec(dllimport) void RtlInitUnicodeString(unicode *,const W *),RtlFreeUnicodeString(unicode *);
__declspec(dllimport) unsigned GetCurrentProcessId(void),WaitForSingleObject(void *,unsigned);
__declspec(dllimport) unsigned GetLastError(void);
__declspec(dllimport) int CloseHandle(void *),GetExitCodeProcess(void *,unsigned *),CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,void *,void *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
static void *open_root(void){unicode name;unsigned formatted=RtlFormatCurrentUserKeyPath(&name);if(formatted){char text[]="registry: identity status 0x00000000\n";for(unsigned i=0;i<8;i++)text[28+i]="0123456789abcdef"[(formatted>>(28-i*4))&15];OutputDebugStringA(text);}
    check(!formatted,"registry: real user identity FAIL\n");attributes a={48,0,0,&name,0x40,0,0,0};void *root=0;unsigned status=NtOpenKeyEx(&root,0xf003fu,&a,0);RtlFreeUnicodeString(&name);check(!status && root,"registry: persistent user root FAIL\n");return root;}
static void *create(void *parent,const W *name,unsigned *disposition){unicode n;RtlInitUnicodeString(&n,name);attributes a={48,0,parent,&n,0x40,0,0,0};void *key=0;check(!NtCreateKey(&key,0xf003fu,&a,0,0,0,disposition) && key,"registry: real key creation FAIL\n");return key;}
static void put(void *h,const W *name,unsigned data){unicode n;RtlInitUnicodeString(&n,name);check(!NtSetValueKey(h,&n,0,4,&data,4),"registry: durable set FAIL\n");}
static unsigned get(void *h,const W *name){unicode n;RtlInitUnicodeString(&n,name);struct {unsigned title,type,length,data;} value;unsigned required=0;
    check(!NtQueryValueKey(h,&n,2,&value,sizeof(value),&required) && required==16 && value.type==4 && value.length==4,"registry: real query FAIL\n");return value.data;}
static void producer(unsigned index){void *root=open_root();unsigned d;void *h=create(root,(const W *)L"KyIlOS-Registry-Batch",&d);
    W name[]={ 'P','r','o','d','u','c','e','r','-',(W)('0'+index),'-','0','0',0};
    for(unsigned i=0;i<24;i++){name[11]=(W)('0'+i/10);name[12]=(W)('0'+i%10);put(h,name,index*100+i);}
    check(!NtClose(h) && !NtClose(root),"registry: producer close FAIL\n");}
#ifdef REGISTRY_CHILD
__declspec(dllimport) W *GetCommandLineW(void);
void mainCRTStartup(void){const W *s=GetCommandLineW();unsigned n=0;while(s[n])n++;check(n && s[n-1]>='0' && s[n-1]<='3',"registry: child argv FAIL\n");producer(s[n-1]-'0');ExitProcess(0);}
#elif defined(REGISTRY_READBACK)
void mainCRTStartup(void){void *root=open_root();unicode name;RtlInitUnicodeString(&name,(const W *)L"KyIlOS-Registry-Batch");
    attributes a={48,0,root,&name,0x40,0,0,0};void *h=0;check(!NtOpenKeyEx(&h,1,&a,0),"registry: reboot lost key FAIL\n");
    check(get(h,(const W *)L"mixed")==0x78563412,"registry: reboot lost value FAIL\n");
    for(unsigned index=0;index<4;index++){W n[]={ 'P','r','o','d','u','c','e','r','-',(W)('0'+index),'-','0','0',0};
        for(unsigned i=0;i<24;i++){n[11]=(W)('0'+i/10);n[12]=(W)('0'+i%10);check(get(h,n)==index*100+i,"registry: reboot lost concurrent value FAIL\n");}}
    check(!NtClose(h) && !NtClose(root),"registry: readback close FAIL\n");
    OutputDebugStringA("windows-registry-readback: 97 preexisting values after actual VM shutdown/reboot PASS\n");ExitProcess(0);}
#else
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,cx,cy,fill,flags;uint16_t show,extra;void *bytes,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} process_info;
void mainCRTStartup(void) {
    void *root=open_root();unsigned d;void *h=create(root,(const W *)L"KyIlOS-Registry-Batch",&d);check(d==1 || d==2,"registry: disposition FAIL\n");
    put(h,(const W *)L"MiXeD",0x78563412);check(get(h,(const W *)L"mixed")==0x78563412,"registry: case identity FAIL\n");
    unicode name;RtlInitUnicodeString(&name,(const W *)L"mixed");unsigned required=0;uint8_t short_buffer[14];
    check(NtQueryValueKey(h,&name,2,0,0,&required)==0xc0000023u && required==16,"registry: query sizing FAIL\n");
    check(NtQueryValueKey(h,&name,2,short_buffer,sizeof(short_buffer),&required)==0x80000005u && required==16,"registry: partial buffer FAIL\n");
    check(NtQueryValueKey(h,&name,2,(void *)0xffff800000000000ull,16,&required)==0xc0000005u,"registry: kernel output denied FAIL\n");
    check(NtSetValueKey(h,&name,0,4,(void *)0xffff800000000000ull,4)==0xc0000005u,"registry: kernel input denied FAIL\n");
    attributes a={48,0,root,&name,0x40,0,0,0};RtlInitUnicodeString(&name,(const W *)L"KyIlOS-Registry-Batch");void *readonly=0;
    check(!NtOpenKeyEx(&readonly,1,&a,0),"registry: readonly view FAIL\n");RtlInitUnicodeString(&name,(const W *)L"denied");unsigned data=5;
    check(NtSetValueKey(readonly,&name,0,4,&data,4)==0xc0000022u && !NtClose(readonly),"registry: rights attenuation FAIL\n");
    process_info children[4];startup si={0};si.size=sizeof(si);
    for(unsigned i=0;i<4;i++){W command[]={ 'r','e','g','i','s','t','r','y',' ',(W)('0'+i),0};
        check(CreateProcessW((const W *)L"A:\\windows-registry-child.exe",command,0,0,0,0,0,0,&si,&children[i]),"registry: child process launch FAIL\n");}
    for(unsigned i=0;i<4;i++){unsigned status=99,wait=WaitForSingleObject(children[i].process,0xffffffffu),error=GetLastError();
        int queried=!wait && GetExitCodeProcess(children[i].process,&status);
        if(wait || !queried || status){char detail[]="registry: producer wait/status/error=00000000/00000000/00000000\n";
            unsigned values[]={wait,status,error};for(unsigned j=0;j<3;j++)for(unsigned k=0;k<8;k++)detail[37+j*9+k]="0123456789abcdef"[(values[j]>>(28-4*k))&15];OutputDebugStringA(detail);}
        check(!wait && queried && !status && CloseHandle(children[i].thread) && CloseHandle(children[i].process),"registry: concurrent producer completion FAIL\n");}
    for(unsigned index=0;index<4;index++){W n[]={ 'P','r','o','d','u','c','e','r','-',(W)('0'+index),'-','0','0',0};
        for(unsigned i=0;i<24;i++){n[11]=(W)('0'+i/10);n[12]=(W)('0'+i%10);check(get(h,n)==index*100+i,"registry: lost cross-process update FAIL\n");}}
    unsigned disposition;void *victim=create(h,(const W *)L"Deleted-Recreated",&disposition);put(victim,(const W *)L"v",17);
    RtlInitUnicodeString(&name,(const W *)L"Deleted-Recreated");a=(attributes){48,0,h,&name,0x40,0,0,0};void *old=0;check(!NtOpenKeyEx(&old,1,&a,0),"registry: second identity reference FAIL\n");
    check(!NtDeleteKey(victim),"registry: actual tombstone FAIL\n");RtlInitUnicodeString(&name,(const W *)L"v");
    check(NtQueryValueKey(old,&name,2,short_buffer,sizeof(short_buffer),&required)==0xc000017cu,"registry: deleted handle FAIL\n");
    void *replacement=create(h,(const W *)L"Deleted-Recreated",&disposition);check(disposition==1,"registry: new incarnation FAIL\n");put(replacement,(const W *)L"v",99);
    check(get(replacement,(const W *)L"v")==99 && NtQueryValueKey(old,&name,2,short_buffer,sizeof(short_buffer),&required)==0xc000017cu,"registry: stale identity rebound FAIL\n");
    check(!NtClose(old) && !NtClose(victim) && NtClose(victim)==0xc0000008u && !NtDeleteKey(replacement) && !NtClose(replacement),"registry: exact close lifecycle FAIL\n");
    check(!NtClose(h) && !NtClose(root),"registry: final close FAIL\n");
    OutputDebugStringA("windows-registry: persistent hive / NT buffers / rights / 4 real processes / 96 values / delete-recreate identity PASS\n");ExitProcess(0);
}
#endif
