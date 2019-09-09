#include <errno.h>
#include <sys/epoll.h>
#include <stdio.h>

#include "epoll.h"

using namespace std;

struct epoll_event *events;

int epoll_init()
{
    int epfd=epoll_create(LISTENQ+1);
    if(epfd==-1)
    {
        perror("epoll_init error");
        return -1;
    }
    events=new epoll_event[MAXEVENTS];
    return epfd;
}

int epoll_add(int epfd,int lfd,void *request,uint32_t events)
{
    struct epoll_event event;
    event.data.ptr=request;
    event.events=events;
    if(epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&event)<0)
    {
        perror("epoll_add error");
        return -1;
    }
    return 0;
}

int epoll_del(int epfd,int lfd,void *request,uint32_t events)
{
    struct epoll_event event;
    event.data.ptr=request;
    event.events=events;
    if(epoll_ctl(epfd,EPOLL_CTL_DEL,lfd,&event)<0)
    {
        perror("epoll_del error");
        return -1;
    }
    return 0;
}

int epoll_mod(int epfd,int lfd,void *request,uint32_t events)
{
    struct epoll_event event;
    event.data.ptr=request;
    event.events=events;
    if(epoll_ctl(epfd,EPOLL_CTL_MOD,lfd,&event)<0)
    {
        perror("epoll_mod error");
        return -1;
    }
    return 0;
}

int my_epoll_wait(int epfd,struct epoll_event* events,int max_events,int timeout)
{
    int ret_cnt=epoll_wait(epfd,events,max_events,timeout);
    if(ret_cnt<0)
    {
        perror("epoll_wait error");
        return -1;
    }
    return ret_cnt;
}

