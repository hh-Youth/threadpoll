#include <stdio.h>
#include"threadpoll.h"
#include <unistd.h>
#include <pthread.h>
#include<stdlib.h>


void taskFunc(void* arg) {
    int num = *(int*)arg;
    printf("thread  %ld is working, number = %d\n", pthread_self(), num);
    sleep(1);
}



int main()
{
    //创建线程池
    ThreadPool* pool = threadPoolCreate(5, 15, 20);  // 最小5个线程，最多15个线程，任务队列容量20个任务
    int* num = (int*)malloc(sizeof(int));
    *num = 100;
    pthread_t tid;
    Args *args=(Args*)malloc(sizeof(Args));
    args->pool = pool;
    args->func = taskFunc;
    args->num = num;
    pthread_create(&tid,NULL, threadPoolAdd,args);

   
    pthread_join(tid,NULL);
    sleep(30);
   
    threadPoolDestroy(pool);

    return 0;
}