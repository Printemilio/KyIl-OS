/* Exact bytes of two real ISO libraries while readers and lookups overlap.
 * Host-generated constants describe the staged artifacts, never synthetic data. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "iso_reader_expected.h"
static int failed;
static void *reader(void *argument){
    uintptr_t id=(uintptr_t)argument;
    const char *path=id%2?ISO_FILE_1:ISO_FILE_0;
    uint64_t expected=id%2?ISO_HASH_1:ISO_HASH_0;
    size_t size=id%2?ISO_SIZE_1:ISO_SIZE_0;
    unsigned char bytes[8192];
    for(unsigned iteration=0;iteration<12;iteration++){
        struct stat directory,info;
        int fd=open(path,O_RDONLY);
        if(fd<0 || fstat(fd,&info) || (size_t)info.st_size!=size ||
           stat("/usr/share/suchu/examples",&directory) || !S_ISDIR(directory.st_mode)){
            __atomic_store_n(&failed,1,__ATOMIC_RELEASE);if(fd>=0)close(fd);return NULL;
        }
        uint64_t hash=1469598103934665603ull;
        for(size_t offset=0;offset<size;){
            size_t want=size-offset;if(want>sizeof(bytes))want=sizeof(bytes);
            ssize_t n=pread(fd,bytes,want,(off_t)offset);
            if(n<=0){__atomic_store_n(&failed,1,__ATOMIC_RELEASE);break;}
            for(ssize_t i=0;i<n;i++)hash=(hash^bytes[i])*1099511628211ull;
            offset+=(size_t)n;
        }
        close(fd);
        if(hash!=expected){fprintf(stderr,"ISO reader %zu mismatch %s: %016llx != %016llx\n",id,path,
            (unsigned long long)hash,(unsigned long long)expected);__atomic_store_n(&failed,1,__ATOMIC_RELEASE);return NULL;}
    }return NULL;
}
int main(void){
    pthread_t threads[4];
    for(uintptr_t i=0;i<4;i++)if(pthread_create(&threads[i],NULL,reader,(void *)i))return 2;
    for(unsigned i=0;i<4;i++)if(pthread_join(threads[i],NULL))return 3;
    fprintf(stderr,"ISO concurrent libraries/directory: %s\n",failed?"FAIL":"PASS");
    return failed?1:0;
}
