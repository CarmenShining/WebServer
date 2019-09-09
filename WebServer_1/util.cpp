#include "util.h"
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <memory.h>
#include <stdio.h>
ssize_t readn(int fd,void *buf,size_t n)
{
   size_t nleft;//还剩多少没读
   ssize_t nread=0;//这次读了多少
   char *ptr=(char*)buf;
   nleft=n;
   while(nleft>0)
   {
       if((nread=read(fd,ptr,nleft))<0)
       {
            if(errno==EINTR)
                nread=0;
            else if(errno==EAGAIN)
                return (n-nleft);
            else
                return -1;
       }
       else if(nread==0)
           break;

       nleft-=nread;
       ptr+=nread;
   }
   printf("Recieve: %s\n",ptr);
   return (n-nleft);
}

ssize_t writen(int fd,void *buf,size_t n)
{
    size_t nleft=n;
    ssize_t nwritten=0;
    char *ptr=(char *)buf;

    printf("writen start! fd=%d\n",fd);
    while(nleft>0)
    {
        if((nwritten=write(fd,ptr,nleft))<=0)
        {
            if(nwritten<0)
            {
                if(errno==EINTR||errno==EAGAIN)
                {
                    nwritten=0;
                }
                else
                    return -1;
            }
        }
        nleft-=nwritten;
        ptr+=nwritten;
    }
    return n;
}
/*
*
*
*/
void handle_for_sigpipe()
{
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=SIG_IGN;//忽略信号的处理程序即设定接受到指定信号后的动作为忽略
    sa.sa_flags=0;
    if(sigaction(SIGPIPE,&sa,NULL)) //屏蔽SIGPIPE信号
        return;
}

int setSocketNonBlocking(int fd)
{
    int flag=fcntl(fd,F_GETFL,0);
    if(flag==-1)
        return -1;
    flag|=O_NONBLOCK;
    if(fcntl(fd,F_SETFL,flag)==-1)
        return -1;
    return 0;

}

