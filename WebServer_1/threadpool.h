#ifndef THREADPOOL
#define THREADPOOL
#include "requestData.h"
#include <pthread.h>


const int MAX_THREADS=1024;
const int MAX_QUEUE=65535;

const int THREADPOOL_INVALID=-1;
const int THREADPOOL_LOCK_FAILURE=-2;
const int THREADPOOL_QUEUE_FULL=-3;
const int THREADPOOL_SHUTDOWN=-4;
const int THREADPOOL_THREAD_FAILURE=-5;
const int THREADPOOL_GRACEFUL=1;

typedef enum
{
    immediate_shutdown=1,
    graceful_shutdown=2
}threadpool_shutdown_t;

typedef struct{
    void (*function)(void *);
    void *argument;
}threadpool_task_t;

struct threadpool_t
{
    pthread_mutex_t lock;//互斥锁
    pthread_cond_t notify;//条件变量，通知线程
    pthread_t *threads;//线程数组的起始指针
    threadpool_task_t *queue;//任务队列的起始指针
    int thread_cnt;//线程数量
    int queue_len;//队列长度
    int head;//当前队头
    int tail;//当前队尾
    int task_cnt;//当前待运行任务数
    int shutdown;//线程池当前状态是否关闭
    int started;//当前运行的线程数
};

threadpool_t *threadpool_create(int thread_cnt,int queue_len,int flags);
int threadpool_add(threadpool_t *pool,void (*function)(void *),void *argument,int flags);
int threadpool_destory(threadpool_t *pool,int flags);
int threadpool_free(threadpool_t *pool);
static void *threadpool_thread(void *threadpool);

#endif
