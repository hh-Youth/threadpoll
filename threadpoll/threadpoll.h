#ifndef _THREADPOLL_H
#define _THREADPOLL_H

typedef struct ThreadPool ThreadPool;
typedef struct Args {
    ThreadPool* pool;
    void(*func)(void*);
    void* num;
}Args;
//�����̳߳ز���ʼ��

ThreadPool* threadPoolCreate(int min,int max,int queueSize);

//�����̳߳�
int threadPoolDestroy(ThreadPool* pool);

//���̳߳��������
void threadPoolAdd(void * arg);

//��ȡ�̳߳��й������̵߳ĸ���
int threadPoolBusyNum(ThreadPool* pool);

//��ȡ�̳߳��л��ŵ��̵߳ĸ���
int threadPoolAliveNum(ThreadPool* pool);




//////////////////////////
void* worker(void* arg);

void* manager(void* arg);

void threadExit(ThreadPool* pool);


#endif // !_THREADPOLL_H

