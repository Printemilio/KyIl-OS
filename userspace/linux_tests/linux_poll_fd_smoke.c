#include <poll.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>

int main(void) {
    struct pollfd ignored[]={{-1,POLLIN|POLLOUT,123},{INT_MIN,POLLIN,123}};
    int r=poll(ignored,2,0);
    if(r!=0 || ignored[0].revents || ignored[1].revents){
        printf("linux_poll_fd_smoke: FAIL negative FD r=%d revents=%x/%x\n",r,ignored[0].revents,ignored[1].revents);return 1;
    }
    struct pollfd invalid={INT_MAX,0,0};
    if(poll(&invalid,1,0)!=1 || invalid.revents!=POLLNVAL){puts("linux_poll_fd_smoke: FAIL invalid FD must be POLLNVAL");return 2;}
    int pair[2];if(pipe(pair))return 3;
    char c='x';if(write(pair[1],&c,1)!=1)return 4;
    struct pollfd mixed[]={{-1,POLLIN,123},{pair[0],POLLIN,0},{INT_MAX,POLLIN,0}};
    if(poll(mixed,3,0)!=2 || mixed[0].revents || mixed[1].revents!=POLLIN || mixed[2].revents!=POLLNVAL)return 5;
    if(read(pair[0],&c,1)!=1 || c!='x')return 6;
    close(pair[0]);close(pair[1]);
    puts("linux_poll_fd_smoke: negative ignored, invalid POLLNVAL and mixed readiness verified");return 0;
}
