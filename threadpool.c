#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>


typedef struct Task{
    void (*function)(void *arg);
    void *arg;
}Task;

struct ThreadPool{
    Task* taskQ;
    int queueCapacity;
    int queueSize;  // 当前任务数
    int queueFront;
    int queueRear;

    pthread_t managerID;
    pthread_t *threadIDs; //数组
    int minNum;
    int maxNum;
    int busyNum;
    int liveNum;
    int exitNum;
    pthread_mutex_t mutexPool;
    pthread_mutex_t mutexBusy;
    pthread_cond_t notFull;
    pthread_cond_t notEmpty;

    int shutdown;
};


//创建线程池
ThreadPool *threadPoolCreate(int min, int max, int queueCapacity){  // 线程最大数和最小数
    ThreadPool *pool = (ThreadPool*)malloc(sizeof(ThreadPool));
    if(pool == NULL){
        printf("malloc thread pool error\n");
        return NULL;
    }

    pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
    if(pool->threadIDs == NULL){
        printf("malloc threadID error\n");
        //释放掉之前的资源
        free(pool);
        return NULL;
    }

    memset(pool->threadIDs, 0, sizeof(pthread_t)*max);

    pool->minNum = min;
    pool->maxNum = max;
    pool->busyNum = 0;
    pool->liveNum = min;
    pool->exitNum = 0;

    //初始化 锁和条件变量
    if(pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
        pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
        pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
        pthread_cond_init(&pool->notFull, NULL) != 0){
        printf("mutex init error\n");
        free(pool);
        free(pool->threadIDs);
        return NULL;
    }

    pool->taskQ = (Task*)malloc(sizeof(Task) * queueCapacity);
    pool->queueCapacity = queueCapacity;
    pool->queueSize = 0;
    pool->queueFront = 0;
    pool->queueRear = 0;
    
    pool->shutdown = 0;

    // 创建管理者线程和工作线程
    pthread_create(&pool->managerID, NULL, manager, pool);

    for(int i=0; i<min; i++){
        pthread_create(&pool->threadIDs[i], NULL, worker, pool);
    } 

    //
    return pool;

}

//任务添加函数
void threadPoolAdd(ThreadPool *pool, void(*func)(void*), void* arg){
    pthread_mutex_lock(&pool->mutexPool);
    // 阻塞条件 ： 队列满
    while(pool->queueSize == pool->queueCapacity && !pool->shutdown){
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);
    }
    if(pool->shutdown){
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }

    pool->taskQ[pool->queueRear].function = func;
    pool->taskQ[pool->queueRear].arg = arg;
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;

    pthread_cond_signal(&pool->notEmpty);

    pthread_mutex_unlock(&pool->mutexPool);
}

// 销毁线程池
// 这个写法正确性有待商榷
/*

*/
int threadPoolDestroy(ThreadPool *pool){
    if(pool == NULL){
        return -1;
    }
    // 设置为1后 manager就会结束
    pool->shutdown = 1;
    //首先要等manager结束，这样线程池的线程数量稳定
    pthread_join(pool->managerID, NULL);

    for(int i=0; i<pool->liveNum; i++){
        pthread_cond_signal(&pool->notEmpty);
    }
    // free malloc
    if(pool->taskQ){
        free(pool->taskQ);
    }
    if(pool->threadIDs){
        free(pool->threadIDs);
    }
    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);

    free(pool);
    pool = NULL;
    return 0;
}


void *worker(void *arg){
    ThreadPool *pool = (ThreadPool*)arg;

    while(1){
        //获取线程池池的锁
        pthread_mutex_lock(&pool->mutexPool);

        while(pool->queueSize==0 && !pool->shutdown){
            
            //没有任务就阻塞在条件变量
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

            //解除阻塞，有可能是 空闲->销毁
            if(pool->exitNum > 0){
                pool->exitNum--;
                //加下面这个判断的原因是，min和max和每次add/exit线程数量的取余问题
                if(pool->liveNum > pool->minNum){
                    pool->liveNum--;
                    //释放锁， 退出
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool);
                }
            }

        }

        //执行一个任务
        if(pool->shutdown){
            //释放锁， 退出
            pool->liveNum--;
            pthread_mutex_unlock(&pool->mutexPool);
            threadExit(pool);
        }

        Task task;
        //由于上面判断了queuesize!=0，所以front<rear
        task.function = pool->taskQ[pool->queueFront].function;
        task.arg = pool->taskQ[pool->queueFront].arg;
        pool->queueFront = (pool->queueFront + 1)%pool->queueCapacity;
        pool->queueSize--;
        //解锁
        pthread_cond_signal(&pool->notFull);

        pthread_mutex_unlock(&pool->mutexPool);
        
        //开始执行任务
        printf("thread %ld start working....\n", pthread_self());
        //busy这个锁，如果别人拿了线程池的锁是不是也可以操作？
        //不是，只要所有人操作的时候都拿这个锁就行
        //这里有个问题， 你没有拿到pool的锁，却操作了其中一个变量
        //可以理解为pool的锁是对其他剩余变量加的
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);
        task.function(task.arg);
        free(task.arg);
        task.arg = NULL;

        printf("thread %ld end working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);

    }
    return NULL;
}

int getBusyNum(ThreadPool *pool){
    pthread_mutex_lock(&pool->mutexBusy);
    int ret = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return ret;
}

void *manager(void *arg){
    ThreadPool *pool = (ThreadPool*)arg;
    while(!pool->shutdown){
        sleep(3);

        pthread_mutex_lock(&pool->mutexPool);

        int queueSize = pool->queueSize;
        int liveNum = pool->liveNum;

        pthread_mutex_unlock(&pool->mutexPool);
        
        int busyNum = getBusyNum(pool);

        //添加线程
        if(queueSize > liveNum && liveNum < pool->maxNum){
            pthread_mutex_lock(&pool->mutexPool);

            int cnt = 0;
            for(int i=0; i<pool->maxNum && cnt<NUMBER && 
            pool->liveNum < pool->maxNum; i++){
                if(pool->threadIDs[i] == 0){
                    pthread_create(&pool->threadIDs[i], NULL, worker, pool);
                    cnt++;
                    pool->liveNum++;
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }

        //销毁空闲线程
        if(busyNum*2<liveNum && liveNum > pool->minNum){
            pthread_mutex_lock(&pool->mutexPool);
            pool->exitNum = NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);
            for(int i=0; i<NUMBER; i++)
                pthread_cond_signal(&pool->notEmpty);
        }

    }

    return NULL;
}

//这个函数并写的变量没有竞争， 因此不需要获取锁
//
void threadExit(ThreadPool *pool){
    pthread_t tid = pthread_self();
    for(int i=0; i<pool->maxNum; i++){
        if(pool->threadIDs[i] == tid){
            pool->threadIDs[i] = 0;
            printf("threadExit() called, %ld exiting...\n", tid);
            break;
        }
    }
    pthread_exit(NULL);
}