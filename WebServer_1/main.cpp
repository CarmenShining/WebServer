#include "requestData.h"
#include "epoll.h"
#include "threadpool.h"
#include "util.h"

#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <queue>
using namespace std;

const int TIMER_TIME_OUT = 500;
const int PORT = 8888;
const int ASK_STATIC_FILE = 1;
const int ASK_IMAGE_STITCH = 2;

const int THREADPOOL_THREAD_NUM = 4;
const int QUEUE_SIZE = 65535;

const string PATH = "/";

extern pthread_mutex_t queueLock;
extern struct epoll_event *events;
extern priority_queue<mytimer*,deque<mytimer*>,timerCmp> mTimerQueue;


int socket_bind_listen(int port)
{
    //检查port的值是否在正确范围内
    if(port<1024||port>65535)
        return -1;

    //创建socket(IPv4+TCP)，返回监听描述符
    int lfd=0;
    if((lfd=socket(AF_INET,SOCK_STREAM,0))==-1)
        return -1;

    //消除bind时“Address already in use”错误，允许套接口
    //和一个已经使用的地址捆绑
    int optval=1;
    if(setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&optval,sizeof(optval))==-1)
        return -1;

    //设置服务器的IP和port，和监听描述符绑定
    struct sockaddr_in serv_addr;
    bzero((char*)&serv_addr,sizeof(serv_addr));
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    serv_addr.sin_port=htons((unsigned short)port);
    if(bind(lfd,(struct sockaddr*)&serv_addr,sizeof(serv_addr))==-1)
    return -1;

    //开始监听，最大等待队列为LISTENQ
    if(listen(lfd,LISTENQ)==-1)
        return -1;

    //无效监听描述符
    if(lfd==-1)
    {
        close(lfd);
        return -1;
    }

    return lfd;
}

void myHandler(void *args)
{
    requestData *req=(requestData*)args;
    req->handleRequest();
}

//接受连接请求函数
void acceptConnection(int lfd,int epfd,const string &path)
{    
    struct sockaddr_in cli_addr;
    memset(&cli_addr,0,sizeof(struct sockaddr_in));
    socklen_t cli_addr_len=0;
    int accept_fd=0;
    while((accept_fd=accept(lfd,(struct sockaddr*)&cli_addr,&cli_addr_len))>0)
    {
        //设置为非阻塞模式
        int ret=setSocketNonBlocking(accept_fd);
        if(ret<0)
        {
            perror("Set non block failed");
            return;
        }

        requestData *req_info=new requestData(epfd,accept_fd,path);
        
        /*文件描述符可以读，边缘触发（Edge Triggered）模式，保证一个socket连接在任一时刻只被一个线程处理*/
        uint32_t _epo_event=EPOLLIN|EPOLLET|EPOLLONESHOT;
        epoll_add(epfd,accept_fd,static_cast<void *>(req_info),_epo_event);

        //新增时间信息
        mytimer *mtimer=new mytimer(req_info,TIMER_TIME_OUT);
        req_info->addTimer(mtimer);
        pthread_mutex_lock(&queueLock);
        mTimerQueue.push(mtimer);
        pthread_mutex_unlock(&queueLock);
    }

}

/*
 * 分发处理函数
 * 通过epoll机制直接诶将任务塞给线程池
 * */
void handle_events(int epfd,int lfd,struct epoll_event* events,int ev_num,const string &path,threadpool_t *pool)
{
    for(int i=0;i<ev_num;i++)
    {
        //获取事件产生的描述符
        requestData *req=(requestData*)(events[i].data.ptr);
        int fd=req->getFd();

        //有事件发生的描述符为监听描述符
        if(fd==lfd)
        {
            acceptConnection(lfd,epfd,path);
        }
        else
        {
            //排除错误事件
            if((events[i].events&EPOLLERR)||(events[i].events&EPOLLHUP)
                    ||(!(events[i].events&EPOLLIN)))
            {
                printf("error event\n");
                delete req;
                continue;
            }

            /*
             * 将请求任务加入到线程池中
             * 加入线程池之前将Timer和request分离
             * */
            req->seperateTimer();
            int rc=threadpool_add(pool,myHandler,events[i].data.ptr,0);
        }
    }
}


void handle_expired_event()
{
    pthread_mutex_lock(&queueLock);
    while(!mTimerQueue.empty())
    {
        mytimer *ptimer_now=mTimerQueue.top();
        if(ptimer_now->isDeleted())
        {
            mTimerQueue.pop();
            delete ptimer_now;
        }
        else if(ptimer_now->isValid()==false)
        {
            mTimerQueue.pop();
            delete ptimer_now;
        }
        else
        {
            break;
        }
    }
    pthread_mutex_unlock(&queueLock);
}
/*
 * 主函数的基本流程：
 * 初始化epoll树，边缘触发模式
 * 初始化线程池
 * 设置非阻塞IO
 * 然后进入事件循环体函数
 * */

int main()
{
    handle_for_sigpipe();
    int epfd=epoll_init();
    if(epfd<0)
    {
        perror("epoll init failed");
        return 1;
    }

    threadpool_t *threadpool=threadpool_create(THREADPOOL_THREAD_NUM,QUEUE_SIZE,0);
    int lfd=socket_bind_listen(PORT);
    if(lfd<0)
    {
        perror("socket bind failed");
        return 1;
    }
    if(setSocketNonBlocking(lfd)<0)
    {
        perror("set socket non block failed");
        return 1;
    }

    uint32_t event=EPOLLIN|EPOLLET;
    requestData *req=new requestData();
    req->setFd(lfd);
    epoll_add(epfd,lfd,static_cast<void *>(req),event);
    
    while(true)
    {
        int ev_num=my_epoll_wait(epfd,events,MAXEVENTS,-1);
        if(ev_num==0)
            continue;
        cout<<ev_num<<endl;

        handle_events(epfd,lfd,events,ev_num,PATH,threadpool);

        handle_expired_event();
    }

    return 0;
}

