#include "threadpoll.h"
#include <pthread.h>
#include<stdlib.h>
#include <string.h>
#include <unistd.h>
#include<stdio.h>

const int NUMBER = 2;

//����ṹ��
typedef struct Task
{
	void (*function)(void * arg);
	void* arg;
}Task;

//�̳߳ؽṹ��
struct ThreadPool
{
	//�������
	Task* taskQ;
	int queueCapactity;    //����
	int queueSize;         //��ǰ�������
	int queueFront;        //��ͷ -> ȡ����
	int queueRear;         //��β -> ������


	pthread_t managerID;      //�������߳�ID
	pthread_t *threadIDs;     //�������߳�ID
	int minNum;               //��С�߳�����
	int maxNum;               //����߳�����
	int busyNum;              //æ���̵߳ĸ���
	int liveNum;              //�����̵߳ĸ���  
	int exitNum;              //Ҫ���ٵ��̵߳ĸ���

	pthread_mutex_t mutexPool;     //���������̳߳�
	pthread_mutex_t mutexBusy;     //��busyNum����
	pthread_cond_t notFull;        //��������ǲ������˵���������
	pthread_cond_t notEmpty;       //��������ǲ��ǿ��˵���������

	int shutdown;          // �ǲ���Ҫ�����̳߳أ�����Ϊ1��������Ϊ0 

};



void* worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (1)
	{
		pthread_mutex_lock(&pool->mutexPool);
		//��ǰ��������Ƿ�Ϊ�� && �̳߳��Ƿ�����
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//���������߳�(��������)
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//�ж��ǲ���Ҫ�����߳�
			if (pool->exitNum > 0) {
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);   //�߳��˳�
				}
			}
		}


		//�ж��̳߳��Ƿ�����
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);   //�߳��˳�
		}

		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
	
		//�ƶ�ͷָ��(˳��ѭ������)
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapactity;
		pool->queueSize--;


		//����������
		pthread_cond_signal(&pool->notFull);
		//����
		pthread_mutex_unlock(&pool->mutexPool);



		//��æ���߳���������Ҫ�����ͽ���
		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		//����������е�������
		task.function(task.arg);
		free(task.arg);     //��������ʱΪÿ�������arg�����˶�����������ִ�����Ӧ��free�ö���
		task.arg = NULL;

		//����ִ�����
		//��æ���߳���������Ҫ�����ͽ���
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
		//ÿ��������һ��
		sleep(1);
		printf("Now livePthread Num is:%ld\n", threadPoolAliveNum(pool));
		printf("Now busyPthread Num is:%ld\n", threadPoolBusyNum(pool));
		//ȡ���̳߳�������������͵�ǰ�̵߳�����
		pthread_mutex_lock(&pool->mutexPool);
		
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//ȡ��æ���̵߳�����
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//����߳�
		//�����������>�����̸߳��� && �����̸߳���<����߳���
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

		//�����߳�
		//����æ���߳�*2 < �����߳��� && �����߳��� > ��С�߳���
		if (busyNum * 2 < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//�ù������߳���ɱ����
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
	memset(pool->threadIDs,0,sizeof(pthread_t)*max);   //��ʼ��threadIDs�����ֵ��pthread_t���޷������͡������Ƿ�Ϊ0�жϸ��߳�ID�Ƿ����õ���˼
	pool->minNum = min;
	pool->maxNum = max;
	pool->busyNum = 0;
	pool->liveNum = min;     //��ʼ����С�������߳�
	pool->exitNum = 0;


	if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
		pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
		pthread_cond_init(&pool->notEmpty,NULL) != 0 ||
		pthread_cond_init(&pool->notFull,NULL != 0) ) 
	{
		printf("mutex or condition init fail...\n");
		break;
	}  

	//�������
	pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
	pool->queueCapactity = queueSize;
	pool->queueSize = 0;
	pool->queueFront = 0;
	pool->queueRear = 0;

	//�̳߳�------δ����״̬
	pool->shutdown = 0;

	//�����߳�
	pthread_create(&pool->managerID, NULL, manager, pool);
	for (int i = 0; i < min; ++i) {
		pthread_create(&pool->threadIDs[i], NULL, worker, pool);
	}
	return pool;
	} while (0);
	


	//���������ڴ�ʧ�ܣ��ͷ���Դ
	if (pool && pool->threadIDs) free(pool->threadIDs);
	if (pool && pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);
	return NULL;

}

int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL) return -1;

	//�ر��̳߳�
	pool->shutdown = 1;

	//�������չ������߳�
	pthread_join(pool->managerID, NULL);
	//�����������������߳�----��ΪһЩ�������߳̿��ܱ�����������������޷��˳�
	for (int i = 0; i < pool->liveNum; ++i) {
		pthread_cond_signal(&pool->notEmpty);
	}

	//�ͷ��̳߳���Ķ��ڴ�
	if (pool->taskQ) free(pool->taskQ);
	if (pool->threadIDs) free(pool->threadIDs);

	//���ٻ���������������
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
		// �����300������ʱ��������������ˡ�
		if (*num == 300) {  
			break;
		}

	int* num_ = (int*)malloc(sizeof(int));   //Ϊÿ�������arg���ٶѣ�������ִ������free�ö���
	*num_ = *num;
	//����
	pthread_mutex_lock(&pool->mutexPool);
	printf("insert Task:%d\n", *num_);
	while (pool->queueSize == pool->queueCapactity && !pool->shutdown)
	{
		 //�����������߳�
		pthread_cond_wait(&pool->notFull,&pool->mutexPool);
	}
	//�̳߳��Ƿ�����
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//�������
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = num_;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapactity;
	pool->queueSize++;
	
	*num = *num + 1;
	
	//���ѹ����߳�����
	pthread_cond_signal(&pool->notEmpty);
	//����
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
	pthread_t tid = pthread_self(); //˭���øú���threadExit������˭���߳�ID
	for (int i = 0; i < pool->maxNum;++i) {
		if (tid == pool->threadIDs[i]) {
			pool->threadIDs[i] = 0;
			printf("threadExit() called, %ld exiting...\n", tid);
			break;
		}
	}
	pthread_exit(NULL);

}
