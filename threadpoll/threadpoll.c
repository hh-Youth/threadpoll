#include "threadpoll.h"
#include <pthread.h>
#include<stdlib.h>
#include <string.h>
#include <unistd.h>
#include<stdio.h>

const int NUMBER = 2;

//任务结构体
typedef struct Task
{
	void (*function)(void * arg);
	void* arg;
}Task;

//线程池结构体
struct ThreadPool
{
	//任务队列
	Task* taskQ;
	int queueCapactity;    //容量
	int queueSize;         //当前任务个数
	int queueFront;        //队头 -> 取数据
	int queueRear;         //队尾 -> 放数据


	pthread_t managerID;      //管理者线程ID
	pthread_t *threadIDs;     //工作的线程ID
	int minNum;               //最小线程数量
	int maxNum;               //最大线程数量
	int busyNum;              //忙的线程的个数
	int liveNum;              //存活的线程的个数  
	int exitNum;              //要销毁的线程的个数

	pthread_mutex_t mutexPool;     //锁整个的线程池
	pthread_mutex_t mutexBusy;     //锁busyNum变量
	pthread_cond_t notFull;        //任务队列是不是满了的条件变量
	pthread_cond_t notEmpty;       //任务队列是不是空了的条件变量

	int shutdown;          // 是不是要销毁线程池，销毁为1，不销毁为0 

};



void* worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (1)
	{
		pthread_mutex_lock(&pool->mutexPool);
		//当前任务队列是否为空 && 线程池是否销毁
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//阻塞工作线程(条件变量)
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//判断是不是要销毁线程
			if (pool->exitNum > 0) {
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);   //线程退出
				}
			}
		}


		//判断线程池是否销毁
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);   //线程退出
		}

		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
	
		//移动头指针(顺序循环队列)
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapactity;
		pool->queueSize--;


		//唤醒生产者
		pthread_cond_signal(&pool->notFull);
		//解锁
		pthread_mutex_unlock(&pool->mutexPool);



		//对忙的线程数访问需要加锁和解锁
		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		//调用任务队列的任务函数
		task.function(task.arg);
		free(task.arg);     //创建任务时为每个任务的arg开辟了堆区，该任务执行完后应该free该堆区
		task.arg = NULL;

		//任务执行完毕
		//对忙的线程数访问需要加锁和解锁
		printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);

	}

	return NULL;
}

void* manager(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;

	while (!pool->shutdown)
	{
		//每隔三秒检测一次
		sleep(1);
		printf("Now livePthread Num is:%ld\n", threadPoolAliveNum(pool));
		printf("Now busyPthread Num is:%ld\n", threadPoolBusyNum(pool));
		//取出线程池中任务的数量和当前线程的数量
		pthread_mutex_lock(&pool->mutexPool);
		
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//取出忙的线程的数量
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//添加线程
		//规则：任务个数>存活的线程个数 && 存活的线程个数<最大线程数
		if (queueSize > liveNum && liveNum < pool->maxNum) {
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i) {
				if (pool->threadIDs[i] == 0) {
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
					
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}

		//销毁线程
		//规则：忙的线程*2 < 存活的线程数 && 存活的线程数 > 最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//让工作的线程自杀销毁
			for (int i = 0; i < NUMBER; ++i) {
				pthread_cond_signal(&pool->notEmpty);
			}

		}


	}



	return NULL;
}


ThreadPool* threadPoolCreate(int min, int max, int queueSize)
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do
	{
	if(pool == NULL){
		printf("malloc threadpool fail...\n");
		break;
	}
	pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t)*max);
	if (pool->threadIDs==NULL) {
		printf("malloc threadIDs fail...\n");
		break;
	}
	memset(pool->threadIDs,0,sizeof(pthread_t)*max);   //初始化threadIDs数组的值，pthread_t是无符号整型。根据是否为0判断该线程ID是否利用的意思
	pool->minNum = min;
	pool->maxNum = max;
	pool->busyNum = 0;
	pool->liveNum = min;     //初始化最小个存活的线程
	pool->exitNum = 0;


	if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
		pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
		pthread_cond_init(&pool->notEmpty,NULL) != 0 ||
		pthread_cond_init(&pool->notFull,NULL != 0) ) 
	{
		printf("mutex or condition init fail...\n");
		break;
	}  

	//任务队列
	pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
	pool->queueCapactity = queueSize;
	pool->queueSize = 0;
	pool->queueFront = 0;
	pool->queueRear = 0;

	//线程池------未销毁状态
	pool->shutdown = 0;

	//创建线程
	pthread_create(&pool->managerID, NULL, manager, pool);
	for (int i = 0; i < min; ++i) {
		pthread_create(&pool->threadIDs[i], NULL, worker, pool);
	}
	return pool;
	} while (0);
	


	//如果申请堆内存失败，释放资源
	if (pool && pool->threadIDs) free(pool->threadIDs);
	if (pool && pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);
	return NULL;

}

int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL) return -1;

	//关闭线程池
	pool->shutdown = 1;

	//阻塞回收管理者线程
	pthread_join(pool->managerID, NULL);
	//唤醒阻塞的消费者线程----因为一些消费者线程可能被阻塞在条件变量里，无法退出
	for (int i = 0; i < pool->liveNum; ++i) {
		pthread_cond_signal(&pool->notEmpty);
	}

	//释放线程池里的堆内存
	if (pool->taskQ) free(pool->taskQ);
	if (pool->threadIDs) free(pool->threadIDs);

	//销毁互斥锁和条件变量
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);


	free(pool);
	pool = NULL;

	return 0;
}


void threadPoolAdd(void* arg)
{
	Args* arg_ = (Args*)arg;
	ThreadPool* pool = arg_->pool;
	void(*func)(void*) = arg_->func;
	int *num =arg_->num;
	while (1)
	{
		// 当添加300个任务时，不再添加任务了。
		if (*num == 300) {  
			break;
		}

	int* num_ = (int*)malloc(sizeof(int));   //为每个任务的arg开辟堆，在任务执行完后会free该堆区
	*num_ = *num;
	//加锁
	pthread_mutex_lock(&pool->mutexPool);
	printf("insert Task:%d\n", *num_);
	while (pool->queueSize == pool->queueCapactity && !pool->shutdown)
	{
		 //阻塞生产者线程
		pthread_cond_wait(&pool->notFull,&pool->mutexPool);
	}
	//线程池是否销毁
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//添加任务
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = num_;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapactity;
	pool->queueSize++;
	
	*num = *num + 1;
	
	//唤醒工作线程消费
	pthread_cond_signal(&pool->notEmpty);
	//解锁
	pthread_mutex_unlock(&pool->mutexPool);

	}
}

int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int aliveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return aliveNum;
}



void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self(); //谁调用该函数threadExit，就是谁的线程ID
	for (int i = 0; i < pool->maxNum;++i) {
		if (tid == pool->threadIDs[i]) {
			pool->threadIDs[i] = 0;
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);

}
