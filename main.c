#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "threadpool.h"

void taskFunc(void *arg){
    int num = *(int*)arg;
    printf("thread %ld is working, task number = %d\n", 
        pthread_self(), num);
    sleep(1);
}

int main(){

    ThreadPool *pool = threadPoolCreate(3,10,100);

    if(pool == NULL){
        printf("pool creation error\n");
        return 0;
    }

    printf("pool creation success\n");

    for(int i=0; i<100; i++){
        int *num = (int*)malloc(sizeof(int));
        *num = i+100;
        threadPoolAdd(pool, taskFunc, num);
    }

    sleep(30);

    threadPoolDestroy(pool);

    return 0;

}