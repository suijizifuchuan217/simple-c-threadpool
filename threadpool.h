#ifndef THREADPOOL_H
#define THREADPOOL_H

#define NUMBER 2
typedef struct ThreadPool ThreadPool;


//创建线程池
ThreadPool *threadPoolCreate(int min, int max, int queueCapacity);

void* worker(void *arg);

void threadPoolAdd(ThreadPool *pool, void(*func)(void*), void* arg);

int threadPoolDestroy(ThreadPool *pool);
int getBusyNum(ThreadPool *pool);
void *manager(void *arg);
void threadExit(ThreadPool *pool);



#endif