#include "threadpool.h"


threadpool_t *threadpool_create(int thread_cnt,int queue_len,int flags)
{
    threadpool_t *pool;
    int i;
    
    if(thread_cnt<=0||thread_cnt>MAX_THREADS||queue_len<=0||queue_len>MAX_QUEUE)
        return NULL;
    
    if((pool=(threadpool_t*)malloc(sizeof(threadpool_t)))==NULL)
        goto err;
    
    pool->thread_cnt=0;
    pool->queue_len=queue_len;
    pool->head=pool->tail=pool->task_cnt=0;
    pool->shutdown=pool->started=0;

    pool->threads=(pthread_t *)malloc(sizeof(pthread_t)*thread_cnt);
    pool->queue=(threadpool_task_t *)malloc(sizeof(threadpool_task_t)*queue_len);
    if(pool->queue==NULL||pool->threads==NULL)
        goto err;

    if(pthread_mutex_init(&(pool->lock),NULL)!=0||
            pthread_cond_init(&(pool->notify),NULL)!=0)
        goto err;

    for(i=0;i<thread_cnt;i++)
    {
        if(pthread_create(&(pool->threads[i]),NULL,threadpool_thread,(void*)pool)!=0)
        {
            threadpool_destory(pool,0);
            return NULL;
        }
        pool->thread_cnt++;
        pool->started++;
    }
    
    return pool;

err:
    if(pool)
        threadpool_free(pool);
    return NULL;
}

int threadpool_add(threadpool_t *pool,void (*function)(void *),void *argument,int flags)
{
    int err=0;
    int next=0;
    if(pool==NULL||function==NULL)
    {
        return THREADPOOL_INVALID;
    }

    if(pthread_mutex_lock(&(pool->lock))!=0)
    {
        return THREADPOOL_LOCK_FAILURE;
    }

    next=(pool->tail+1)%pool->queue_len;

    do{
        if(pool->task_cnt==pool->queue_len)
        {
            err=THREADPOOL_QUEUE_FULL;
            break;
        }

        if(pool->shutdown)
        {
            err=THREADPOOL_SHUTDOWN;
            break;
        }

        pool->queue[pool->tail].function=function;
        pool->queue[pool->tail].argument=argument;
        pool->tail=next;
        pool->task_cnt+=1;

        if(pthread_cond_signal(&(pool->notify))!=0)
        {
            err=THREADPOOL_LOCK_FAILURE;
            break;
        }
    }while(0);

    if(pthread_mutex_unlock(&(pool->lock))!=0)
    {
        err=THREADPOOL_LOCK_FAILURE;
    }
    
    return err;
}

int threadpool_destory(threadpool_t *pool,int flags)
{
    int i=0,err=0;

    if(pool==NULL)
    {
        return THREADPOOL_INVALID;
    }

    if(pthread_mutex_lock(&(pool->lock))!=0)
    {
        return THREADPOOL_LOCK_FAILURE;
    }

    do{
        if(pool->shutdown)
        {
            err=THREADPOOL_SHUTDOWN;
            break;
        }

        pool->shutdown=(flags&THREADPOOL_GRACEFUL)?
            graceful_shutdown:immediate_shutdown;

        if((pthread_cond_broadcast(&(pool->notify))!=0)||
                (pthread_mutex_unlock(&(pool->lock))!=0))
        {
            err=THREADPOOL_LOCK_FAILURE;
            break;
        }

        for(i=0;i<pool->thread_cnt;i++)
        {
            if(pthread_join(pool->threads[i],NULL)!=0)
            {
                err=THREADPOOL_THREAD_FAILURE;
                break;
            }
        }
    }while(false);

    if(!err)
    {
        threadpool_free(pool);
    }

    return err;
}

int threadpool_free(threadpool_t *pool)
{
    if(pool==NULL||pool->started>0)
    {
        return -1;
    }

    if(pool->threads)
    {
        free(pool->threads);
        free(pool->queue);

        pthread_mutex_lock(&(pool->lock));
        pthread_mutex_destroy(&(pool->lock));
        pthread_cond_destroy(&(pool->notify));
    }

    free(pool);
    return 0;
}
static void *threadpool_thread(void *threadpool)
{
    threadpool_t *pool=(threadpool_t*)threadpool;
    threadpool_task_t task;

    for(;;)
    {
        pthread_mutex_lock(&(pool->lock));

        //等待条件变量
        while((pool->task_cnt==0)&&(!pool->shutdown))
        {
            pthread_cond_wait(&(pool->notify),&(pool->lock));
        }

        if((pool->shutdown==immediate_shutdown)||
                ((pool->shutdown==graceful_shutdown)&&(pool->task_cnt==0)))
            break;

        task.function=pool->queue[pool->head].function;
        task.argument=pool->queue[pool->head].argument;
        pool->head=(pool->head+1)%pool->queue_len;
        pool->task_cnt-=1;

        pthread_mutex_unlock(&(pool->lock));

        (*(task.function))(task.argument);
    }

    --pool->started;

    pthread_mutex_unlock(&(pool->lock));
    pthread_exit(NULL);
    return (NULL);
}

