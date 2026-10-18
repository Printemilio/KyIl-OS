#include <stdint.h>
typedef uint16_t W;
#define ABI __attribute__((ms_abi))
__declspec(dllimport) void *LoadLibraryExW(const W *,void *,unsigned),*GetProcAddress(void *,const char *),*LocalFree(void *),*CreateThread(void *,uint64_t,unsigned (*)(void *),void *,unsigned,unsigned *),*GetStdHandle(unsigned);
__declspec(dllimport) int FreeLibrary(void *),CloseHandle(void *),WriteFile(void *,const void *,unsigned,unsigned *,void *);
__declspec(dllimport) unsigned WaitForSingleObject(void *,unsigned),GetLastError(void);
__declspec(dllimport) void OutputDebugStringA(const char *),ExitProcess(unsigned);
static int (ABI *convert)(const W *,unsigned,void **,unsigned *),(ABI *to_sid)(const W *,void **),(ABI *valid_sid)(const void *),(ABI *valid_acl)(const void *),(ABI *valid_sd)(const void *);
static unsigned (ABI *sid_length)(const void *);
static int (ABI *equal_sid)(const void *,const void *),(ABI *get_dacl)(const void *,int *,void **,int *),(ABI *get_sacl)(const void *,int *,void **,int *),(ABI *get_owner)(const void *,void **,int *),(ABI *get_group)(const void *,void **,int *),(ABI *get_control)(const void *,W *,unsigned *),(ABI *get_ace)(void *,unsigned,void **);
static int (ABI *init_acl)(void *,unsigned,unsigned),(ABI *init_sd)(void *,unsigned),(ABI *set_dacl)(void *,int,void *,int),(ABI *set_owner)(void *,void *,int),(ABI *set_group)(void *,void *,int),(ABI *add_allow)(void *,unsigned,unsigned,const void *),(ABI *add_deny)(void *,unsigned,unsigned,const void *),(ABI *add_audit)(void *,unsigned,unsigned,const void *,int,int);
typedef struct {void *multiple;unsigned operation,form,type,pad;void *name;} trustee;
typedef struct {unsigned access,mode,inherit,pad;trustee trustee;} access_entry;
static void (ABI *trustee_sid)(trustee *,void *);
static unsigned (ABI *set_entries)(unsigned,const access_entry *,void *,void **),(ABI *build_sd)(const trustee *,const trustee *,unsigned,const access_entry *,unsigned,const access_entry *,const void *,unsigned *,void **);
static void check(int ok,const char *why){if(!ok){unsigned n=0,done,error=GetLastError();while(why[n])n++;OutputDebugStringA(why);WriteFile(GetStdHandle((unsigned)-12),why,n,&done,0);char text[]="error=0x00000000\n";
    for(unsigned i=0;i<8;i++)text[8+i]="0123456789abcdef"[(error>>(28-4*i))&15];WriteFile(GetStdHandle((unsigned)-12),text,sizeof(text)-1,&done,0);ExitProcess(1);}}
static const W document[]=L"O:SYG:BAD:PAI(A;CIIO;GA;;;SY)(D;;0x00000003;;;WD)S:(ML;;NW;;;LW)";
static unsigned rights_for(void *acl,void *sid,unsigned kind){unsigned result=0;
    for(unsigned i=0;i<((W *)acl)[2];i++){void *ace=0;check(get_ace(acl,i,&ace),"security-data: merged ACE traversal FAIL\n");
        if(*(uint8_t *)ace==kind && equal_sid((uint8_t *)ace+8,sid))result|=*(unsigned *)((uint8_t *)ace+4);}return result;}
static unsigned worker(void *ignored){(void)ignored;for(unsigned i=0;i<32;i++){void *sd=0;unsigned size;check(convert(document,1,&sd,&size) && size>20 && valid_sd(sd) && !LocalFree(sd),"security-data: concurrent conversion and ownership FAIL\n");}return 0;}
void mainCRTStartup(void){void *library=LoadLibraryExW(L"advapi32",0,0x800);check(library!=0,"security-data: actual provider FAIL\n");
#define R(target,name) check((target=GetProcAddress(library,name))!=0,"security-data: export " name " FAIL\n")
    R(convert,"ConvertStringSecurityDescriptorToSecurityDescriptorW");R(to_sid,"ConvertStringSidToSidW");R(valid_sid,"IsValidSid");R(sid_length,"GetLengthSid");R(equal_sid,"EqualSid");
    R(valid_acl,"IsValidAcl");R(valid_sd,"IsValidSecurityDescriptor");R(get_dacl,"GetSecurityDescriptorDacl");R(get_sacl,"GetSecurityDescriptorSacl");R(get_owner,"GetSecurityDescriptorOwner");R(get_group,"GetSecurityDescriptorGroup");R(get_control,"GetSecurityDescriptorControl");R(get_ace,"GetAce");
    R(init_acl,"InitializeAcl");R(init_sd,"InitializeSecurityDescriptor");R(set_dacl,"SetSecurityDescriptorDacl");R(set_owner,"SetSecurityDescriptorOwner");R(set_group,"SetSecurityDescriptorGroup");R(add_allow,"AddAccessAllowedAce");R(add_deny,"AddAccessDeniedAce");R(add_audit,"AddAuditAccessAce");
    R(trustee_sid,"BuildTrusteeWithSidW");R(set_entries,"SetEntriesInAclW");R(build_sd,"BuildSecurityDescriptorW");
#undef R
    void *system=0,*admin=0,*numeric=0;check(to_sid(L"SY",&system) && to_sid(L"BA",&admin) && to_sid(L"S-1-5-18",&numeric) && valid_sid(system) && sid_length(system)==12 && sid_length(admin)==16 && equal_sid(system,numeric),"security-data: actual SID encoding FAIL\n");
    check(((uint8_t *)system)[0]==1 && ((uint8_t *)system)[1]==1 && ((uint8_t *)system)[7]==5 && *(unsigned *)((uint8_t *)system+8)==18,"security-data: real authority and subauthority bytes FAIL\n");
    void *sd=0;unsigned size=0;W control;unsigned revision;int present,defaulted;void *owner=0,*group=0,*acl=0,*ace=0;
    check(convert(document,1,&sd,&size) && size>20 && valid_sd(sd) && get_control(sd,&control,&revision) && control==0x9414 && revision==1,"security-data: self-relative SD and exact controls FAIL\n");
    check(get_owner(sd,&owner,&defaulted) && !defaulted && equal_sid(owner,system) && get_group(sd,&group,&defaulted) && !defaulted && equal_sid(group,admin),"security-data: owner and group offsets FAIL\n");
    check(get_dacl(sd,&present,&acl,&defaulted) && present && !defaulted && valid_acl(acl) && ((W *)acl)[2]==2 && get_ace(acl,0,&ace),"security-data: actual DACL and ACE count FAIL\n");
    check(((uint8_t *)ace)[0]==0 && ((uint8_t *)ace)[1]==10 && *(unsigned *)((uint8_t *)ace+4)==0x10000000 && equal_sid((uint8_t *)ace+8,system),"security-data: flags / rights / embedded SID FAIL\n");
    check(get_ace(acl,1,&ace) && ((uint8_t *)ace)[0]==1 && *(unsigned *)((uint8_t *)ace+4)==3,"security-data: denied ACE remains ordered FAIL\n");
    check(get_sacl(sd,&present,&acl,&defaulted) && present && get_ace(acl,0,&ace) && ((uint8_t *)ace)[0]==17 && *(unsigned *)((uint8_t *)ace+4)==1 && *(unsigned *)((uint8_t *)ace+16)==4096,"security-data: actual integrity label FAIL\n");
    check(!LocalFree(sd),"security-data: LocalFree ownership FAIL\n");
    check(convert(L"D:(A;;GA;;;SY)(A;;GWGR;;;S-1-15-2-1)S:(ML;;;;;S-1-16-0)",1,&sd,&size) && valid_sd(sd) && get_sacl(sd,&present,&acl,&defaulted) && get_ace(acl,0,&ace) && !*(unsigned *)((uint8_t *)ace+4) && !LocalFree(sd),"security-data: package SID and zero-mask untrusted label FAIL\n");
    check(convert(L"D:NO_ACCESS_CONTROL",1,&sd,0) && get_dacl(sd,&present,&acl,&defaulted) && present && !acl && !LocalFree(sd),"security-data: null DACL semantics FAIL\n");
    check(convert(L"D:",1,&sd,0) && get_dacl(sd,&present,&acl,&defaulted) && present && acl && !((W *)acl)[2] && !LocalFree(sd),"security-data: empty DACL differs from null FAIL\n");
    check(convert(L"O:SY",1,&sd,0) && get_dacl(sd,&present,&acl,&defaulted) && !present && !LocalFree(sd),"security-data: absent DACL presence FAIL\n");
    check(convert(L"D:(OA;;RP;00112233-4455-6677-8899-aabbccddeeff;;WD)",1,&sd,0) && get_dacl(sd,&present,&acl,&defaulted) && *(uint8_t *)acl==4 && get_ace(acl,0,&ace),"security-data: object ACE / DS ACL FAIL\n");
    const uint8_t guid[]={0x33,0x22,0x11,0,0x55,0x44,0x77,0x66,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    check(*(uint8_t *)ace==5 && *(unsigned *)((uint8_t *)ace+8)==1,"security-data: object flags FAIL\n");for(unsigned i=0;i<16;i++)check(((uint8_t *)ace)[12+i]==guid[i],"security-data: actual GUID byte order FAIL\n");check(!LocalFree(sd),"security-data: DS buffer release FAIL\n");
    __declspec(align(8)) uint8_t absolute[40],buffer[256];
    check(init_acl(buffer,sizeof(buffer),2) && add_allow(buffer,2,1,system) && add_deny(buffer,2,2,admin) && add_audit(buffer,2,3,system,1,1) && valid_acl(buffer) && init_sd(absolute,1) && set_owner(absolute,system,0) && set_group(absolute,admin,1) && set_dacl(absolute,1,buffer,0) && valid_sd(absolute),"security-data: actual absolute construction FAIL\n");
    check(get_group(absolute,&group,&defaulted) && defaulted && group==admin && get_dacl(absolute,&present,&acl,&defaulted) && acl==buffer && present && get_ace(buffer,2,&ace) && ((uint8_t *)ace)[0]==2 && ((uint8_t *)ace)[1]==0xc0,"security-data: absolute borrowed pointers / audit flags FAIL\n");
    check(set_dacl(absolute,1,0,0) && get_dacl(absolute,&present,&acl,&defaulted) && present && !acl,"security-data: absolute null ACL transition FAIL\n");
    check(init_acl(buffer,sizeof(buffer),2) && add_allow(buffer,2,1,system) && add_deny(buffer,2,2,admin),"security-data: merge source FAIL\n");
    void *world=0;check(to_sid(L"WD",&world),"security-data: merge trustee FAIL\n");access_entry entries[2]={{.access=4,.mode=1},{.access=8,.mode=3}};
    trustee_sid(&entries[0].trustee,system);trustee_sid(&entries[1].trustee,world);
    void *merged=0;check(!set_entries(2,entries,buffer,&merged) && valid_acl(merged) && rights_for(merged,system,0)==5 && rights_for(merged,admin,1)==2 && rights_for(merged,world,1)==8 && get_ace(merged,0,&ace) && *(uint8_t *)ace==1 && equal_sid((uint8_t *)ace+8,world),"security-data: real grant / deny ordering / preserved rights FAIL\n");
    entries[0].access=16;entries[0].mode=2;void *replaced=0;check(!set_entries(1,entries,merged,&replaced) && rights_for(replaced,system,0)==16 && rights_for(replaced,admin,1)==2,"security-data: SET_ACCESS replacement FAIL\n");
    entries[0].mode=4;trustee_sid(&entries[0].trustee,system);void *revoked=0;
    unsigned revoked_status=set_entries(1,entries,replaced,&revoked);
    check(!revoked_status && rights_for(revoked,admin,1)==2 && !rights_for(revoked,system,0),"security-data: REVOKE_ACCESS removes grant and preserves denial FAIL\n");
    check(!LocalFree(merged) && !LocalFree(replaced) && !LocalFree(revoked),"security-data: merged ACL ownership FAIL\n");
    entries[0].mode=5;entries[0].access=3;trustee_sid(&entries[0].trustee,system);entries[1]=entries[0];entries[1].mode=6;
    check(!set_entries(2,entries,0,&merged),"security-data: audit construction FAIL\n");unsigned audit_flags=0;
    for(unsigned i=0;i<((W *)merged)[2];i++){check(get_ace(merged,i,&ace) && *(uint8_t *)ace==2 && *(unsigned *)((uint8_t *)ace+4)==3,"security-data: audit rights FAIL\n");audit_flags|=((uint8_t *)ace)[1];}
    check(audit_flags==0xc0 && !LocalFree(merged),"security-data: real audit-success/failure merge FAIL\n");
    check(convert(document,1,&sd,0),"security-data: old descriptor FAIL\n");entries[0].mode=1;entries[0].access=16;trustee_sid(&entries[0].trustee,admin);void *built=0;
    check(!build_sd(0,0,1,entries,0,0,sd,&size,&built) && valid_sd(built) && get_dacl(built,&present,&acl,&defaulted) && rights_for(acl,admin,0)==16 && get_sacl(built,&present,&acl,&defaulted) && present && get_ace(acl,0,&ace) && *(uint8_t *)ace==17,"security-data: real BuildSecurityDescriptor preserves old SACL / adds access FAIL\n");
    check(!LocalFree(sd) && valid_sd(built) && get_owner(built,&owner,&defaulted) && equal_sid(owner,system) && !LocalFree(built) && !LocalFree(world),"security-data: built descriptor independent lifetime FAIL\n");
    trustee current={.form=1,.name=L"CURRENT_USER"};entries[0].trustee=current;
    check(!build_sd(&current,0,1,entries,0,0,0,&size,&built) && get_owner(built,&owner,&defaulted) && valid_sid(owner) && get_dacl(built,&present,&acl,&defaulted) && rights_for(acl,owner,0)==16,"security-data: CURRENT_USER resolves same real identity for owner and grant FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(((uint8_t *)owner)[1]==2 && ((uint8_t *)owner)[7]==22 && *(unsigned *)((uint8_t *)owner+8)==1,"security-data: native UID SID namespace FAIL\n");
#endif
    check(!LocalFree(built),"security-data: CURRENT_USER descriptor lifetime FAIL\n");
    check(!convert(document,2,&sd,0) && GetLastError()==1305 && !convert(L"D:(A;;BOGUS;;;SY)",1,&sd,0) && !to_sid(L"S-1-5-",&sd),"security-data: invalid revision and syntax FAIL\n");
#ifdef KYIL_NATIVE_TEST
    check(!convert(L"D:(XA;;GA;;;SY;(@User.test == 1))",1,&sd,0) && GetLastError()==50,"security-data: unsupported conditional ACE honest FAIL\n");
    check(!valid_sid((void *)0xffff800000000000ull) && !valid_sd((void *)0xffff800000000000ull),"security-data: kernel pointers refused FAIL\n");
#endif
    void *threads[4];for(unsigned i=0;i<4;i++){threads[i]=CreateThread(0,0,worker,0,0,0);check(threads[i]!=0,"security-data: real concurrent callers FAIL\n");}
    for(unsigned i=0;i<4;i++)check(!WaitForSingleObject(threads[i],10000) && CloseHandle(threads[i]),"security-data: completion FAIL\n");
    check(!LocalFree(system) && !LocalFree(admin) && !LocalFree(numeric) && FreeLibrary(library),"security-data: final heap ownership FAIL\n");
    OutputDebugStringA("windows-security-data: real SID / SD / ACL merge-revoke-audit / CURRENT_USER / GUID / integrity labels / invalid syntax / 128 concurrent conversions / ownership PASS\n");ExitProcess(0);
}
