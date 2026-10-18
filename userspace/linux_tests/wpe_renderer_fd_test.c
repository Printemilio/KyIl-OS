/* Exercise the backend's actual WPE export callback. The caller owns and
 * closes each returned descriptor; the view must retain its endpoint. */
#define _GNU_SOURCE
#include <assert.h>
#include "../libvex/src/vex_wpe_backend.c"

int main(void) {
    int pair[2];
    assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair)==0);
    vex_view_backend_t view={.renderer_host_fd=pair[0]};
    for(unsigned i=0;i<1000;i++) {
        int exported=view_backend_get_renderer_host_fd(&view);
        assert(exported>=0 && exported!=pair[0]);
        assert(fcntl(exported,F_GETFD)&FD_CLOEXEC);
        char byte=(char)i,received=0;
        assert(send(exported,&byte,1,0)==1);
        assert(recv(pair[1],&received,1,0)==1 && received==byte);
        assert(close(exported)==0);
        assert(fcntl(pair[0],F_GETFD)>=0);
    }
    assert(close(pair[0])==0);
    assert(view_backend_get_renderer_host_fd(&view)==-1);
    assert(view_backend_get_renderer_host_fd(NULL)==-1);
    assert(close(pair[1])==0);
    puts("WPE renderer FD: 1000 exports, independent close, CLOEXEC, stale/null PASS");
    return 0;
}
