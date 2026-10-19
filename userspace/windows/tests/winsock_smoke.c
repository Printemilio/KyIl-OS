#include <stdint.h>
#include <stddef.h>
void *memset(void *out,int value,size_t n){volatile unsigned char *p=out;for(size_t i=0;i<n;i++)p[i]=(unsigned char)value;return out;}
__declspec(dllimport) void *LoadLibraryExW(const uint16_t *,void *,unsigned),*GetProcAddress(void *,const char *);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static void check(int ok,const char *why){if(!ok){OutputDebugStringA(why);ExitProcess(1);}}
typedef struct {uint16_t version,high,maxs,maxu;void *vendor;char description[257],status[129];} data;
typedef struct {uint16_t family,port;uint32_t address;char pad[8];} address;
typedef struct ai {int flags,family,type,protocol;size_t size;char *canonical;address *address;struct ai *next;} ai;
typedef struct {unsigned count,pad;uintptr_t sockets[64];} fdset;
typedef struct {int seconds,micros;} timeout;
#define GET(result,name,args) result (*name) args=GetProcAddress(m,#name);check(name!=0,"winsock: missing " #name " FAIL\n")
void mainCRTStartup(void){void *m=LoadLibraryExW(L"ws2_32.dll",0,0x800);check(m!=0,"winsock: provider FAIL\n");
    GET(int,WSAStartup,(unsigned short,data *));GET(int,WSACleanup,(void));GET(int,WSAGetLastError,(void));
    GET(uintptr_t,socket,(int,int,int));GET(int,closesocket,(uintptr_t));GET(int,bind,(uintptr_t,const address *,int));
    GET(int,getsockname,(uintptr_t,address *,int *));GET(int,sendto,(uintptr_t,const char *,int,int,const address *,int));
    GET(int,recvfrom,(uintptr_t,char *,int,int,address *,int *));GET(int,select,(int,fdset *,fdset *,fdset *,const timeout *));
    GET(int,getaddrinfo,(const char *,const char *,const ai *,ai **));GET(void,freeaddrinfo,(ai *));GET(const char *,inet_ntop,(int,const void *,char *,size_t));
    GET(void *,WSACreateEvent,(void));GET(int,WSASetEvent,(void *));GET(int,WSAResetEvent,(void *));GET(int,WSACloseEvent,(void *));
    check(GetProcAddress(m,(void *)115)==(void *)WSAStartup && GetProcAddress(m,(void *)23)==(void *)socket,"winsock: real stable ordinals FAIL\n");
    check(GetProcAddress(m,(void *)194)==(void *)inet_ntop && GetProcAddress(m,(void *)50)==(void *)WSACreateEvent,"winsock: verified named-export ordinals FAIL\n");
    check(WSACleanup()==-1 && WSAGetLastError()==10093,"winsock: initialization requirement FAIL\n");
    data d;check(WSAStartup(0,&d)==10092 && !WSAStartup(0x202,&d) && d.version==0x202 && d.high==0x202 && !WSAStartup(0x202,&d),"winsock: negotiation and refs FAIL\n");
    ai hint={.family=2,.type=1},*result=0;
    check(!getaddrinfo("127.0.0.1","443",&hint,&result) && result && result->size==16 && result->address->family==2 && result->address->port==0xbb01 && result->address->address==0x0100007f,"winsock: actual IPv4 and service layout FAIL\n");
    char text[16];check(inet_ntop(2,&result->address->address,text,sizeof(text))==text && text[0]=='1' && text[9]==0,"winsock: address presentation FAIL\n");freeaddrinfo(result);
    result=(void *)1;hint.flags=4;check(getaddrinfo("not-numeric","443",&hint,&result)==11001 && !result,"winsock: invalid numeric host FAIL\n");
    void *event=WSACreateEvent();check(event && WSASetEvent(event) && WSAResetEvent(event) && WSACloseEvent(event),"winsock: real event lifecycle FAIL\n");
    check(socket(0x7fff,1,0)==UINTPTR_MAX && WSAGetLastError()==10047 && closesocket((uintptr_t)0x1234)==-1 && WSAGetLastError()==10038,"winsock: family and handle validation FAIL\n");
    uintptr_t receiver=socket(2,2,17);
    if(receiver==UINTPTR_MAX){
#ifdef HOST_REFERENCE
        check(0,"winsock: host requires actual UDP transport FAIL\n");
#else
        check(WSAGetLastError()==10013,"winsock: native network authority denied FAIL\n");
        OutputDebugStringA("windows-winsock: real provider / ordinals / address layout / events / native NET_CONNECT denial PASS\n");
#endif
    }else {
        address local={.family=2,.address=0x0100007f};int n=16;
        check(!bind(receiver,&local,16) && !getsockname(receiver,&local,&n) && n==16 && local.port,"winsock: actual bind/name FAIL\n");
        uintptr_t sender=socket(2,2,17);check(sender!=UINTPTR_MAX && sendto(sender,"KSW!",4,0,&local,16)==4,"winsock: actual UDP send FAIL\n");
        fdset ready={.count=1,.sockets={receiver}};timeout t={3,0};check(select(0,&ready,0,0,&t)==1 && ready.count==1 && ready.sockets[0]==receiver,"winsock: real readiness FAIL\n");
        address peer;n=16;check(recvfrom(receiver,text,16,0,&peer,&n)==4 && text[0]=='K' && text[3]=='!' && n==16,"winsock: actual UDP receive FAIL\n");
        check(sendto(sender,"TRUNCATE",8,0,&local,16)==8,"winsock: truncation send FAIL\n");
        n=16;check(recvfrom(receiver,text,2,0,&peer,&n)==-1 && WSAGetLastError()==10040 && text[0]=='T' && text[1]=='R',"winsock: consumed truncated datagram must report WSAEMSGSIZE FAIL\n");
        check(closesocket(sender)==0 && closesocket(receiver)==0,"winsock: actual UDP close FAIL\n");
        check(closesocket(receiver)==-1 && WSAGetLastError()==10038,"winsock: stale handle rejected FAIL\n");
        OutputDebugStringA("windows-winsock: real UDP bytes / select / local handles / ordinals / resolver layout / event lifecycle PASS\n");
    }
    check(!WSACleanup() && !WSACleanup() && WSACleanup()==-1 && WSAGetLastError()==10093,"winsock: exact initialization lifetime FAIL\n");ExitProcess(0);
}
