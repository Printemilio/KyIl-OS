#include <unos/storage_volume.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char disk[32*1024*1024];
static int read(void *ctx,uint64_t lba,uint32_t count,void *out){(void)ctx;if(lba>=sizeof(disk)/512 || count>sizeof(disk)/512-lba)return -1;memcpy(out,disk+lba*512,count*512);return 0;}
static void part(unsigned index,unsigned type,uint32_t base,uint32_t count){unsigned char *p=disk+446+(index-1)*16;p[4]=type;memcpy(p+8,&base,4);memcpy(p+12,&count,4);disk[510]=0x55;disk[511]=0xaa;}
static void header(unsigned base){uint64_t magic=UINT64_C(0x554e4f5346533035);memcpy(disk+(base+2048)*512,&magic,8);}
int main(void){
    storage_volume_t v;header(0);assert(!storage_volume_discover(read,NULL,sizeof(disk)/512,&v)&&v.base==0);
    memset(disk,0,sizeof(disk));part(1,7,2048,16000);header(0);
    assert(storage_volume_discover(read,NULL,sizeof(disk)/512,&v)<0); /* foreign MBR never adopted as raw */
    part(2,0xda,20000,30000);header(20000);
    assert(!storage_volume_discover(read,NULL,sizeof(disk)/512,&v)&&v.base==20000&&v.sectors==30000&&v.partition==2);
    part(1,0xda,2048,16000);header(2048);
    assert(storage_volume_discover(read,NULL,sizeof(disk)/512,&v)==-2);
    part(1,7,2048,16000);
    assert(storage_volume_partition(read,NULL,sizeof(disk)/512,1,&v)<0);
    part(2,0xda,10000,30000);assert(storage_volume_partition(read,NULL,sizeof(disk)/512,2,&v)<0); /* overlap */
    part(2,0xda,60000,10000);assert(storage_volume_partition(read,NULL,sizeof(disk)/512,2,&v)<0);
    part(2,0xda,20000,4000);assert(storage_volume_partition(read,NULL,sizeof(disk)/512,2,&v)<0);
    puts("storage-volume: PASS raw legacy, tagged partition, foreign rejection, overlap/truncation/bounds");
}
