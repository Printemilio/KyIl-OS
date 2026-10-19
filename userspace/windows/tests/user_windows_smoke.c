#include <stdint.h>
typedef uint16_t W;
typedef intptr_t (*procedure)(void *,unsigned,uintptr_t,intptr_t);
typedef struct {unsigned size,style;procedure proc;int class_extra,window_extra;void *instance,*icon,*cursor,*brush;const W *menu,*name;void *small;} klass;
typedef struct {unsigned size,pad;W *reserved,*desktop,*title;unsigned x,y,w,h,xc,yc,fill,flags;uint16_t show,bytes;unsigned pad2;void *reserved2,*input,*output,*error;} startup;
typedef struct {void *process,*thread;unsigned pid,tid;} process_info;
typedef struct {unsigned size,pad;void *descriptor;int inherit,pad2;} attributes;
__declspec(dllimport) void *LoadLibraryW(const W *),*GetProcAddress(void *,const char *),*GetStdHandle(unsigned),*CreateEventW(void *,int,int,const W *);
__declspec(dllimport) int WriteFile(void *,const void *,unsigned,unsigned *,void *),CloseHandle(void *),SetEvent(void *),CreateProcessW(const W *,W *,void *,void *,int,unsigned,void *,const W *,startup *,process_info *),GetExitCodeProcess(void *,unsigned *),TerminateProcess(void *,unsigned);
__declspec(dllimport) unsigned GetLastError(void),GetCurrentProcessId(void),GetCurrentThreadId(void),GetModuleFileNameW(void *,W *,unsigned),WaitForSingleObject(void *,unsigned),GetEnvironmentVariableW(const W *,W *,unsigned),GetFileAttributesW(const W *);
__declspec(dllimport) W *GetCommandLineW(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){unsigned n=0,done;while(why[n])n++;WriteFile(GetStdHandle(-12),why,n,&done,0);OutputDebugStringA(why);ExitProcess(1);}}
static uint16_t (*register_class)(const klass *);
static int (*unregister_class)(const W *,void *),(*destroy_window)(void *),(*is_window)(void *),(*get_text)(void *,W *,int),(*get_class)(void *,W *,int),(*set_text)(void *,const W *);
static void *(*create_window)(unsigned,const W *,const W *,unsigned,int,int,int,int,void *,void *,void *,void *),*(*find_window)(void *,void *,const W *,const W *);
static unsigned (*owner)(void *,unsigned *);
static intptr_t (*defproc)(void *,unsigned,uintptr_t,intptr_t),(*get_long)(void *,int),(*set_long)(void *,int,intptr_t);
static unsigned nc,created,destroyed,ncd;
static intptr_t wndproc(void *h,unsigned msg,uintptr_t w,intptr_t l){
    if(msg==0x81){nc++;check(l!=0,"user-windows: NCCREATE context FAIL\n");}
    if(msg==1){created++;set_long(h,-21,73);set_long(h,0,91);}
    if(msg==2)destroyed++;if(msg==0x82)ncd++;return defproc(h,msg,w,l);
}
static void bind(void){void *m=LoadLibraryW(L"user32.dll");check(m!=0,"user-windows: USER32 provider FAIL\n");
#define B(var,name) var=GetProcAddress(m,name);check(var!=0,"user-windows: export " name " FAIL\n")
    B(register_class,"RegisterClassExW");B(unregister_class,"UnregisterClassW");B(destroy_window,"DestroyWindow");B(is_window,"IsWindow");
    B(get_text,"GetWindowTextW");B(get_class,"GetClassNameW");B(set_text,"SetWindowTextW");B(create_window,"CreateWindowExW");B(find_window,"FindWindowExW");
    B(owner,"GetWindowThreadProcessId");B(defproc,"DefWindowProcW");B(get_long,"GetWindowLongPtrW");B(set_long,"SetWindowLongPtrW");
#undef B
}
static int same(const W *a,const W *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
static unsigned append(W *line,unsigned at,uintptr_t n){W d[24];unsigned count=0;do{d[count++]=(W)('0'+n%10);n/=10;}while(n);while(count)line[at++]=d[--count];line[at++]=' ';line[at]=0;return at;}
static uintptr_t parse(const W **p){uintptr_t n=0;while(**p==' ')(*p)++;while(**p>='0' && **p<='9'){n=n*10+*(*p)++-'0';}return n;}
static void join(process_info *p,unsigned expected){unsigned code=0;check(!WaitForSingleObject(p->process,15000) && GetExitCodeProcess(p->process,&code) && code==expected,"user-windows: actual child result FAIL\n");check(CloseHandle(p->thread) && CloseHandle(p->process),"user-windows: process lifetime FAIL\n");}
static void launch(const W *line,int inherit,void *env,unsigned flags,process_info *p){W image[512],copy[160];check(GetModuleFileNameW(0,image,512),"user-windows: image path FAIL\n");unsigned i=0;do{copy[i]=line[i];}while(line[i++]);startup s={.size=sizeof(s)};check(CreateProcessW(image,copy,0,0,inherit,flags,env,0,&s,p),"user-windows: CreateProcess FAIL\n");}
void mainCRTStartup(void){bind();const W *line=GetCommandLineW();
    if(line[0]=='-' && line[1]=='-' && line[2]=='e'){W value[128];check(GetEnvironmentVariableW(L"LOCALAPPDATA",value,128)==6 && same(value,L"custom"),"profile-env: explicit child value FAIL\n");
        check(!GetEnvironmentVariableW(L"USERPROFILE",value,128) && GetLastError()==203,"profile-env: no implicit widening of explicit block FAIL\n");ExitProcess(19);}
    if(line[0]=='-' && line[1]=='-' && line[2]=='l'){const W *p=line+3;void *h=(void *)parse(&p);unsigned expected=(unsigned)parse(&p),pid=0;
        check(find_window((void *)-3,0,L"KyIl.Message.Test",L"one")==h && owner(h,&pid) && pid==expected,"user-windows: crossprocess discovery and authenticated owner FAIL\n");
        check(!destroy_window(h) && GetLastError()==5,"user-windows: foreign destruction denied FAIL\n");ExitProcess(23);}
    if(line[0]=='-' && line[1]=='-' && line[2]=='o'){const W *p=line+3;void *ready=(void *)parse(&p),*stop=(void *)parse(&p);klass c={.size=sizeof(c),.proc=wndproc,.name=L"KyIl.Orphan"};
        check(register_class(&c) && create_window(0,c.name,L"orphan",0,0,0,0,0,(void *)-3,0,0,0),"user-windows: orphan creation FAIL\n");
        check(SetEvent(ready),"user-windows: orphan ready FAIL\n");WaitForSingleObject(stop,UINT32_MAX);ExitProcess(29);}
    W env[512];const W *keys[]={L"USERPROFILE",L"LOCALAPPDATA",L"APPDATA",L"TEMP",L"SystemRoot"};
    for(unsigned i=0;i<5;i++){check(GetEnvironmentVariableW(keys[i],env,512)>0,"profile-env: actual default path FAIL\n");unsigned a=GetFileAttributesW(env);check(a!=UINT32_MAX && (a&16),"profile-env: default resolves to actual directory FAIL\n");}
    W explicit_env[]=L"LOCALAPPDATA=custom\0";process_info child={0};launch(L"--e",0,explicit_env,0x400,&child);join(&child,19);
    for(unsigned cycle=0;cycle<12;cycle++){
        klass c={.size=sizeof(c),.proc=wndproc,.window_extra=8,.name=L"KyIl.Message.Test"};uint16_t atom=register_class(&c);check(atom!=0,"user-windows: class registration FAIL\n");
        check(!register_class(&c) && GetLastError()==1410,"user-windows: duplicate class FAIL\n");
        void *first=create_window(0,(const W *)(uintptr_t)atom,L"one",0,0,0,0,0,(void *)-3,0,0,0),*second=create_window(0,c.name,L"two",0,0,0,0,0,(void *)-3,0,0,0);
        check(first && second && first!=second && get_long(first,-21)==73 && get_long(first,0)==91,"user-windows: real lifecycle and per-window extra bytes FAIL\n");
        check(find_window((void *)-3,0,L"kyil.message.test",L"ONE")==first && find_window((void *)-3,0,(const W *)(uintptr_t)atom,0)==second && find_window((void *)-3,second,c.name,0)==first,"user-windows: title/class/atom/continuation FAIL\n");
        unsigned pid=0;check(owner(first,&pid)==GetCurrentThreadId() && pid==GetCurrentProcessId(),"user-windows: PID/TID identity FAIL\n");
        W text[64];check(get_class(first,text,64)>0 && same(text,c.name) && get_text(first,text,64)==3 && same(text,L"one") && set_text(second,L"changed") && get_text(second,text,4)==3 && same(text,L"cha"),"user-windows: actual text and short buffer FAIL\n");
        check(!unregister_class(c.name,0) && GetLastError()==1412,"user-windows: class lifetime held by windows FAIL\n");
        if(!cycle){W args[96]={'-','-','l',' ',0};unsigned at=append(args,4,(uintptr_t)first);append(args,at,GetCurrentProcessId());launch(args,0,0,0,&child);join(&child,23);}
        check(destroy_window(first) && !is_window(first) && !destroy_window(first) && GetLastError()==1400 && destroy_window(second) && unregister_class(c.name,0),"user-windows: exact cleanup and stale HWND FAIL\n");
    }
    check(nc==24 && created==24 && destroyed==24 && ncd==24,"user-windows: callback counts FAIL\n");
    for(unsigned kill=0;kill<2;kill++){attributes a={sizeof(a),0,0,1,0};void *ready=CreateEventW(&a,1,0,0),*stop=CreateEventW(&a,1,0,0);check(ready && stop,"user-windows: shared handshake FAIL\n");
        W args[96]={'-','-','o',' ',0};unsigned at=append(args,4,(uintptr_t)ready);append(args,at,(uintptr_t)stop);launch(args,1,0,0,&child);
        check(!WaitForSingleObject(ready,10000),"user-windows: orphan startup FAIL\n");void *h=find_window((void *)-3,0,L"KyIl.Orphan",L"orphan");check(h!=0,"user-windows: child window visible FAIL\n");
        check(kill?TerminateProcess(child.process,29):SetEvent(stop),"user-windows: child exit or kill FAIL\n");join(&child,29);
        check(!find_window((void *)-3,0,L"KyIl.Orphan",L"orphan") && !is_window(h),"user-windows: killed owner leaves no ghost FAIL\n");check(CloseHandle(ready) && CloseHandle(stop),"user-windows: handshake release FAIL\n");}
#ifdef KYIL_NATIVE_TEST
    check(!find_window((void *)-3,0,(W *)0xffff800000000000ull,0) && GetLastError()==998,"user-windows: kernel pointer rejected FAIL\n");
    check(!create_window(0,L"absent",L"visible",0x10000000,0,0,10,10,0,0,0,0) && GetLastError()==50,"user-windows: visible window honest refusal FAIL\n");
#endif
    OutputDebugStringA("windows-user-windows: real crossprocess message windows / callbacks / atoms / text / owner rights / stale handles / exit-kill cleanup / exact child environment PASS\n");ExitProcess(0);
}
