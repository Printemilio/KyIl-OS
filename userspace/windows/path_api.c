#include "windows.h"
#include "process.h"
#include <unos/vex_abi.h>

typedef struct {uint32_t attributes;uint32_t times[6];uint32_t size_high,size_low;} file_attribute_data;
_Static_assert(sizeof(file_attribute_data)==36,"WIN32_FILE_ATTRIBUTE_DATA");
static unsigned attributes(const vex_stat_ex_t *s) {
    unsigned a=(s->mode&0170000)==0040000?0x10:0;
    if(!(s->mode&0222))a|=1;
    return a?a:0x80;
}
static uint64_t filetime(int64_t sec,unsigned ns,unsigned present) {
    if(!present || sec < -11644473600ll || ns>=1000000000u ||
       sec> (int64_t)(UINT64_MAX/10000000ull)-11644473601ll)return 0;
    return (uint64_t)(sec+11644473600ll)*10000000ull+ns/100;
}
static int stat_path(const uint16_t *path,vex_stat_ex_t *out) {
    if(!path || !*path){wr_current_teb()->last_error=123;return 0;}
    int r=wr_namespace_call(VEX_FS_ROOT_STAT,path,0,0,out,0);
    if(r){wr_current_teb()->last_error=wr_file_error(-r);return 0;}
    return 1;
}
static unsigned WR_WIN GetFileAttributesW(const uint16_t *path) {
    vex_stat_ex_t st;return stat_path(path,&st)?attributes(&st):UINT32_MAX;
}
static int WR_WIN GetFileAttributesExW(const uint16_t *path,unsigned kind,file_attribute_data *out) {
    if(kind || !out){wr_current_teb()->last_error=87;return 0;}
    vex_stat_ex_t st;if(!stat_path(path,&st))return 0;
    file_attribute_data value={.attributes=attributes(&st)};
    /* Vex STAT_EX reports change, not creation time. Do not fabricate a birth
     * time from ctime. Zero represents unavailable backend creation metadata. */
    uint64_t at=filetime(st.atime_sec,st.atime_nsec,st.valid&VEX_STAT_EX_HAS_ATIME);
    uint64_t mt=filetime(st.mtime_sec,st.mtime_nsec,st.valid&VEX_STAT_EX_HAS_MTIME);
    memcpy(value.times+2,&at,8);memcpy(value.times+4,&mt,8);
    uint64_t size=(st.mode&0170000)==0040000?0:(uint64_t)st.size;
    value.size_high=(unsigned)(size>>32);value.size_low=(unsigned)size;*out=value;return 1;
}
static int remove_path(const uint16_t *path,unsigned flags) {
    if(!path || !*path){wr_current_teb()->last_error=123;return 0;}
    int r=wr_namespace_call(VEX_FS_ROOT_REMOVE,path,flags,0,0,1);
    if(r){wr_current_teb()->last_error=wr_file_error(-r);return 0;}return 1;
}
static int WR_WIN DeleteFileW(const uint16_t *path){return remove_path(path,0);}
static int WR_WIN RemoveDirectoryW(const uint16_t *path){return remove_path(path,VEX_FS_REMOVE_DIRECTORY);}
static int move_result(int result){if(result)wr_current_teb()->last_error=result==-16?32:(result==-18?17:wr_file_error(-result));return !result;}
static int WR_WIN MoveFileW(const uint16_t *source,const uint16_t *destination){int r=wr_namespace_move(source,destination,0,0);if(r==-17){wr_current_teb()->last_error=183;return 0;}return move_result(r);}
static int WR_WIN ReplaceFileW(const uint16_t *destination,const uint16_t *source,const uint16_t *backup,unsigned flags,void *exclude,void *reserved){
    if(exclude || reserved || (flags&~6u)){wr_current_teb()->last_error=flags&~7u?87:50;return 0;}
    /* IGNORE_MERGE_ERRORS / IGNORE_ACL_ERRORS need no relaxation: native
       replacement preserves all supported metadata under one transaction. */
    return move_result(wr_namespace_move(source,destination,backup,1));
}
static int WR_WIN SetFileAttributesW(const uint16_t *name,unsigned value){
    if(!value || value==0x81u){wr_current_teb()->last_error=87;return 0;}
    if(value&~0x81u){wr_current_teb()->last_error=50;return 0;}
    /* This backend exposes READONLY through actual native write permissions.
       Hidden/system/archive/encryption are not silently discarded. */
    int r=wr_namespace_call(VEX_FS_ROOT_MODE,name,0222u,(value&1)?0:0200u,0,1);
    return move_result(r);
}
static int ansi(const char *path,uint16_t *out,size_t size) {
    if(!path || wr_utf16(path,out,size)<0){wr_current_teb()->last_error=1113;return 0;}return 1;
}
static unsigned WR_WIN GetFileAttributesA(const char *path) {
    uint16_t text[1024];return ansi(path,text,1024)?GetFileAttributesW(text):UINT32_MAX;
}
static int WR_WIN DeleteFileA(const char *path) {
    uint16_t text[1024];return ansi(path,text,1024)?DeleteFileW(text):0;
}
static int WR_WIN RemoveDirectoryA(const char *path) {
    uint16_t text[1024];return ansi(path,text,1024)?RemoveDirectoryW(text):0;
}
void *wr_path_metadata_exports(const char *name) {
#define E(f) if(wr_equal(name,#f,0))return (void *)&f
    E(GetFileAttributesW);E(GetFileAttributesExW);E(GetFileAttributesA);
    E(DeleteFileW);E(DeleteFileA);E(RemoveDirectoryW);E(RemoveDirectoryA);
    E(MoveFileW);E(ReplaceFileW);E(SetFileAttributesW);
#undef E
    return 0;
}
