#ifndef EVENTPOLL
#define EVENTPOLL
#include <stdint.h>

const int MAXEVENTS=5000;
const int LISTENQ=1024;

int epoll_init();
int epoll_add(int epfd,int lfd,void *request,uint32_t events);
int epoll_del(int epfd,int lfd,void *request,uint32_t events);
int epoll_mod(int epfd,int lfd,void *request,uint32_t events);
int my_epoll_wait(int epfd,struct epoll_event* events,int max_events,int timeout);

#endif
