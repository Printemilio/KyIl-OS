#include <unos/storage_volume.h>
#include <string.h>
static uint32_t load32(const unsigned char *p){uint32_t value;memcpy(&value,p,4);return value;}
int storage_volume_partition(volume_read_t read,void *context,uint64_t sectors,unsigned index,storage_volume_t *out) {
    unsigned char mbr[512];
    if(!index || index>4 || read(context,0,1,mbr) || mbr[510]!=0x55 || mbr[511]!=0xaa)return -1;
    const unsigned char *p=mbr+446+(index-1)*16;
    uint64_t base=load32(p+8),length=load32(p+12);
    if(p[4]!=0xda || !base || length<=4864 || base>=sectors || length>sectors-base)return -1;
    for(unsigned i=1;i<=4;i++)if(i!=index) {
        const unsigned char *other=mbr+446+(i-1)*16;
        uint64_t start=load32(other+8),count=load32(other+12);
        if(!other[4] || !count)continue;
        if(start>=sectors || count>sectors-start || (base<start+count && start<base+length))return -1;
    }
    *out=(storage_volume_t){base,length,index};return 0;
}
static int native_header(volume_read_t read,void *ctx,storage_volume_t volume) {
    unsigned char header[512];uint64_t magic;
    if(volume.sectors<=4864 || read(ctx,volume.base+2048,1,header))return 0;
    memcpy(&magic,header,8);
    return magic==UINT64_C(0x554e4f5346533035) || magic==UINT64_C(0x554e4f5346533034);
}
int storage_volume_discover(volume_read_t read,void *context,uint64_t sectors,storage_volume_t *out) {
    storage_volume_t raw={0,sectors,0};
    unsigned char mbr[512];int partitioned=0;
    if(!read(context,0,1,mbr) && mbr[510]==0x55 && mbr[511]==0xaa)
        for(unsigned i=0;i<4;i++)partitioned|=mbr[446+i*16+4]!=0;
    if(!partitioned && native_header(read,context,raw)){*out=raw;return 0;}
    unsigned found=0;
    for(unsigned i=1;i<=4;i++) {
        storage_volume_t candidate;
        if(!storage_volume_partition(read,context,sectors,i,&candidate) && native_header(read,context,candidate)) {
            *out=candidate;++found;
        }
    }
    return found==1?0:found>1?-2:-1;
}
