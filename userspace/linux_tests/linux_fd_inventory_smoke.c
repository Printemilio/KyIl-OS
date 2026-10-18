#define _GNU_SOURCE
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
extern char **environ;
int main(int argc,char **argv) {
    if(argc>1) return fcntl(70,F_GETFD)==0 && fcntl(71,F_GETFD)==-1 && errno==EBADF ? 0 : 9;
    int descriptors[40],gate[2],status,result=0;pid_t children[4]={0},exec_child=0;
    for(int i=0;i<40;i++)if((descriptors[i]=open("/dev/null",O_RDWR))<0)return 1;
    if(pipe(gate))return 2;
    if(fcntl(descriptors[39],F_DUPFD,70)!=70 ||
       fcntl(descriptors[39],F_DUPFD_CLOEXEC,70)!=71 ||
       fcntl(70,F_GETFD)!=0 || fcntl(71,F_GETFD)!=FD_CLOEXEC)return 12;
    if(fcntl(descriptors[39],F_DUPFD,-1)!=-1 || errno!=EINVAL)return 13;
    for(int i=0;i<4;i++) {
        children[i]=fork();
        if(children[i]<0){fprintf(stderr,"fd-inventory: fork error=%d\n",errno);result=3;break;}
        if(!children[i]) {
            for(int j=0;j<40;j++)if(fcntl(descriptors[j],F_GETFD)!=0)_exit(4);
            char token;close(gate[1]);
            if(read(gate[0],&token,1)!=1)_exit(5);
            _exit(0);
        }
    }
    /* Actual glibc spawn while four descriptor-owning children are alive. */
    posix_spawn_file_actions_t actions;
    if(posix_spawn_file_actions_init(&actions))result=6;
    else {
        if(posix_spawn_file_actions_adddup2(&actions,descriptors[39],70))result=6;
        else {
            char *args[]={"linux_fd_inventory_smoke","check",NULL};
            int error=posix_spawn(&exec_child,argv[0],&actions,NULL,args,environ);
            if(error){fprintf(stderr,"fd-inventory: posix_spawn error=%d\n",error);result=7;}
        }
        posix_spawn_file_actions_destroy(&actions);
    }
    close(gate[0]);if(write(gate[1],"abcd",4)!=4 && !result)result=8;close(gate[1]);
    for(int i=0;i<4;i++)if(children[i]>0 &&
        (waitpid(children[i],&status,0)!=children[i] || !WIFEXITED(status) || WEXITSTATUS(status)))result=10;
    if(exec_child>0 && (waitpid(exec_child,&status,0)!=exec_child || !WIFEXITED(status) || WEXITSTATUS(status)))result=11;
    close(70);close(71);
    for(int i=0;i<40;i++)close(descriptors[i]);
    fprintf(stderr,"fd-inventory: four live children + real glibc spawn/FD70/exec/wait %s\n",result?"FAIL":"PASS");
    return result;
}
