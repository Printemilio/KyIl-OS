#include <assert.h>
#include <stdio.h>
#include "../userspace/windows/handle_value.h"
int main(void){
    for(unsigned type=0x53;type<=0x5e;type++)for(unsigned slot=0;slot<256;slot++){
        for(unsigned g=1;g<=WR_HANDLE_GENERATION_MAX;g+=127){uintptr_t h=wr_handle_pack(type,g,slot);
            assert(h && h==(uintptr_t)(int32_t)h && !(h&3));
            assert(wr_handle_tag(h)==type && wr_handle_generation(h)==g && wr_handle_slot(h)==slot);
            assert(!wr_handle_tag(h|1) && !wr_handle_tag(h|((uintptr_t)1<<32)));
            assert(h!=wr_handle_pack(type,g==WR_HANDLE_GENERATION_MAX?1:g+1,slot));
        }
        assert(wr_handle_pack(type,WR_HANDLE_GENERATION_MAX,slot));
        assert(!wr_handle_pack(type,WR_HANDLE_GENERATION_MAX+1,slot));
    }
    assert(!wr_handle_tag((uintptr_t)-1) && !wr_handle_tag((uintptr_t)-2));
    assert(!wr_handle_pack(0x80,1,0) && !wr_handle_pack(0x54,0,0) && !wr_handle_pack(0x54,1,256));
    puts("windows-handle-values: signed32 roundtrip / low-bit alignment / types / generations / bounds / no wrap PASS");return 0;
}
