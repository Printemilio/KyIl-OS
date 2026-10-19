#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../userspace/windows/windows.h"
static module m;static unsigned checks,allocations,fail_alloc,fail_protect;
static void check(int value,const char *s){checks++;if(!value){fprintf(stderr,"longjump-table FAIL: %s\n",s);exit(1);}}
void wr_loader_lock(void){}void wr_loader_unlock(void){}
unsigned wr_modules_count(void){return 1;}module *wr_module_at(unsigned n){return n?NULL:&m;}
void *wr_map(void *a,size_t n,int permissions){(void)a;check(permissions==3,"snapshot initially RW");if(fail_alloc)return NULL;void *p=calloc(1,n);if(p)allocations++;return p;}
int wr_protect(void *p,size_t n,int permissions){(void)p;(void)n;check(permissions==1,"snapshot sealed RO");return fail_protect?-1:0;}
int wr_unmap(void *p,size_t n){(void)n;if(p){allocations--;free(p);}return 0;}
int wr_random(void *p,size_t n){memset(p,0x71,n);return 0;}void wr_log(const char *s){fputs(s,stderr);}
_Noreturn void wr_exit(unsigned code){fprintf(stderr,"unexpected exit %x\n",code);exit(1);}
void wr_cfg_check(void){}void wr_cfg_dispatch(void){}
static void put32(void *p,uint32_t v){memcpy(p,&v,4);}
static void reset(unsigned stride){
 wr_load_config_release(&m);free(m.base);memset(&m,0,sizeof(m));m.base=calloc(1,0x4000);check(m.base!=NULL,"image allocation");
 m.image.size=0x4000;m.image.headers=0x1000;m.image.count=3;
 m.image.sections[0]=(pe_section){.rva=0x1000,.size=0x1000,.flags=PE_R|PE_X};
 m.image.sections[1]=(pe_section){.rva=0x2000,.size=0x1000,.flags=PE_R};
 m.image.sections[2]=(pe_section){.rva=0x3000,.size=0x1000,.flags=PE_R|PE_W};
 m.image.dirs[10][0]=0x2000;m.image.dirs[10][1]=0xc0;put32(m.base+0x2000,0xc0);
 put32(m.base+0x2090,0x10000u|((stride-4)<<28));pe_put64(m.base+0x20b0,(uintptr_t)m.base+0x2500);pe_put64(m.base+0x20b8,2);
 put32(m.base+0x2500,0x1001);put32(m.base+0x2500+stride,0x1020);fail_alloc=fail_protect=0;
}
int main(void){
 reset(4);check(!wr_load_config(&m),"valid table");check(wr_longjump_allowed((uintptr_t)m.base+0x1001),"unaligned exact target");
 check(!wr_longjump_allowed((uintptr_t)m.base+0x1002),"neighbor denied");check(!wr_longjump_allowed((uintptr_t)m.base+0x3000),"NX target denied");check(!wr_longjump_allowed(1),"nonimage denied");
 put32(m.base+0x2500,0x1010);check(wr_longjump_allowed((uintptr_t)m.base+0x1001) && !wr_longjump_allowed((uintptr_t)m.base+0x1010),"source mutation cannot alter published snapshot");
 reset(4);pe_put64(m.base+0x20b8,0);check(!wr_load_config(&m) && !wr_longjump_allowed((uintptr_t)m.base+0x1001),"flagged empty table denies all");
 reset(4);put32(m.base+0x2090,0);pe_put64(m.base+0x20b8,0);pe_put64(m.base+0x20b0,0);check(!wr_load_config(&m) && wr_longjump_allowed((uintptr_t)m.base+0x1002),"legacy PE distinction");
 reset(5);check(!wr_load_config(&m),"metadata stride");
 reset(5);m.base[0x2504]=1;check(wr_load_config(&m)!=NULL,"reserved metadata rejected");
 reset(4);put32(m.base+0x2504,0x1001);check(wr_load_config(&m)!=NULL,"duplicate rejected");
 reset(4);put32(m.base+0x2504,0x1000);check(wr_load_config(&m)!=NULL,"unsorted rejected");
 reset(4);put32(m.base+0x2504,0x3000);check(wr_load_config(&m)!=NULL,"nonexec rejected");
 reset(4);put32(m.base+0x2504,0xffffffff);check(wr_load_config(&m)!=NULL,"outside RVA rejected");
 reset(4);pe_put64(m.base+0x20b8,UINT64_MAX);check(wr_load_config(&m)!=NULL,"overflow count rejected");
 reset(4);pe_put64(m.base+0x20b0,(uintptr_t)m.base+0x3ffc);check(wr_load_config(&m)!=NULL,"table crossing image rejected");
 reset(4);put32(m.base+0x2000,0xb8);check(wr_load_config(&m)!=NULL,"truncated fields rejected");
 reset(4);put32(m.base+0x2090,0);check(wr_load_config(&m)!=NULL,"unflagged nonempty rejected");
 reset(4);fail_alloc=1;check(wr_load_config(&m)!=NULL && !m.longjump_targets,"OOM refuses publication");
 reset(4);fail_protect=1;check(wr_load_config(&m)!=NULL,"RO protection failure refuses publication");
 reset(4);m.image.preferred=(uintptr_t)m.base-0x10000;m.image.dirs[5][0]=0x2800;m.image.dirs[5][1]=12;
 put32(m.base+0x2800,0x2000);put32(m.base+0x2804,12);put32(m.base+0x2808,0xa0b0);pe_put64(m.base+0x20b0,m.image.preferred+0x2500);
 check(!pe_relocate(m.base,&m.image,(uintptr_t)m.base) && !wr_load_config(&m) && wr_longjump_allowed((uintptr_t)m.base+0x1001),"relocated table VA; target entries remain RVAs");
 wr_load_config_release(&m);wr_load_config_release(&m);free(m.base);check(!allocations,"all snapshots released including failures");
 printf("windows-longjump-table: %u checks PASS (allocator/protection faults injected; VM exercises actual protection)\n",checks);return 0;
}
