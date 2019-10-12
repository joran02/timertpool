/*
 * =====================================================================================
 *
 *       Filename:  tpoolH.c
 *
 *        Version:  1.0
 *        Created:  2014年03月10日
 *       Revision:  none
 *       Compiler:  gcc
 *	  Description:  tpoolH实现
 *
 *         Author:  huangws
 *
 * =====================================================================================
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "tpoolH.h"
#include "thread.h"

//#include "malloc.h"

#define MAX_THREAD_NUM		8

// 任务单元
typedef struct worker
{
	void* (*process)(void* arg); 	/* 消息处理回调函数 */
	void* (*destroy)(void* arg); 	/* 内存销毁回调函数 */
	void* arg;						/* 回调函数的参数 */
	struct worker* next;
} thread_worker_t;

// 线程池
typedef struct
{
	pthread_t threadid[MAX_THREAD_NUM];
	pthread_mutex_t queue_lock;
	pthread_cond_t queue_ready;
	int shutdown;     				/*是否销毁线程池*/
	int max_thread_num;    			/*线程池中允许的活动线程数目*/
	int cur_queue_size;     		/*当前等待队列的任务数目*/
	thread_worker_t* queue_head;    /*线程池中所有等待任务*/
} thread_pool_t;


static void* _tpoolH_routine(void* arg);


static thread_pool_t _tpoolH;


int tpoolH_init(int _thread_num)
{
	int i = 0;
	int ret = 0;
	pthread_mutex_init (&(_tpoolH.queue_lock), NULL);
	pthread_cond_init (&(_tpoolH.queue_ready), NULL);
	_tpoolH.queue_head = NULL;
	_tpoolH.max_thread_num = _thread_num > MAX_THREAD_NUM ? MAX_THREAD_NUM :
	                         _thread_num;
	_tpoolH.cur_queue_size = 0;
	_tpoolH.shutdown = 0;

	for (i = 0; i < _tpoolH.max_thread_num; i++)
	{
		if (pthread_create(&_tpoolH.threadid[i], "tpoolH_routine", NULL,
		                   _tpoolH_routine, NULL))
		{
			ret = -1;
			printf("thread_create _tpoolH_routine error!  err:%s\n", strerror(errno));
		}
	}

	return ret;
}

int tpoolH_add_worker(void* (*process)(void* arg), void* arg,
                      void* (*destroy)(void* arg))
{
	assert(process != NULL);
	assert(arg != NULL);
	thread_worker_t* newworker = (thread_worker_t*)malloc(sizeof(thread_worker_t));
	newworker->process = process;
	newworker->destroy = destroy;
	newworker->arg = arg;
	newworker->next = NULL;
	pthread_mutex_lock(&(_tpoolH.queue_lock));
	thread_worker_t* member = _tpoolH.queue_head;

	if (member != NULL)
	{
		while (member->next != NULL)
		{
			member = member->next;
		}

		member->next = newworker;
	}
	else
	{
		_tpoolH.queue_head = newworker;
	}

	assert (_tpoolH.queue_head != NULL);
	_tpoolH.cur_queue_size++;
	pthread_mutex_unlock(&(_tpoolH.queue_lock));
	pthread_cond_signal(&(_tpoolH.queue_ready));
	return 0;
}

int tpoolH_add_worker_2(void* (*process)(void* arg))
{
	assert(process != NULL);
	thread_worker_t* newworker = (thread_worker_t*)malloc(sizeof(thread_worker_t));
	newworker->process = process;
	newworker->destroy = NULL;
	newworker->arg = NULL;
	newworker->next = NULL;
	pthread_mutex_lock(&(_tpoolH.queue_lock));
	thread_worker_t* member = _tpoolH.queue_head;

	if (member != NULL)
	{
		while (member->next != NULL)
		{
			member = member->next;
		}

		member->next = newworker;
	}
	else
	{
		_tpoolH.queue_head = newworker;
	}

	assert (_tpoolH.queue_head != NULL);
	_tpoolH.cur_queue_size++;
	pthread_mutex_unlock(&(_tpoolH.queue_lock));
	pthread_cond_signal(&(_tpoolH.queue_ready));
	return 0;
}

int tpoolH_destroy()
{
	int i;
	thread_worker_t* head = NULL;

	if (_tpoolH.shutdown)
	{
		return -1;
	}

	_tpoolH.shutdown = 1;
	pthread_cond_broadcast(&(_tpoolH.queue_ready));

	for (i = 0; i < _tpoolH.max_thread_num; i++)
	{
		pthread_join(_tpoolH.threadid[i], NULL);
	}

	while (_tpoolH.queue_head != NULL)
	{
		head = _tpoolH.queue_head;
		_tpoolH.queue_head = _tpoolH.queue_head->next;
		free(head);
	}

	pthread_mutex_destroy(&(_tpoolH.queue_lock));
	pthread_cond_destroy(&(_tpoolH.queue_ready));
	return 0;
}

static void* _tpoolH_routine(void* arg)
{
	printf("starting thread 0x%x\n", (unsigned int)pthread_self());

	while (1)
	{
		pthread_mutex_lock(&(_tpoolH.queue_lock));

		while (_tpoolH.cur_queue_size == 0 && !_tpoolH.shutdown)
		{
			//printf("thread 0x%x is waiting\n", (unsigned int)pthread_self());
			pthread_cond_wait(&(_tpoolH.queue_ready), &(_tpoolH.queue_lock));
		}

		if (_tpoolH.shutdown)
		{
			pthread_mutex_unlock(&(_tpoolH.queue_lock));
			printf("thread 0x%x will exit\n", (unsigned int)pthread_self());
			pthread_exit(NULL);
		}

		//printf("thread 0x%x is starting to work\n", (unsigned int)pthread_self());
		assert(_tpoolH.cur_queue_size != 0);
		assert(_tpoolH.queue_head != NULL);
		_tpoolH.cur_queue_size--;
		thread_worker_t* worker = _tpoolH.queue_head;
		_tpoolH.queue_head = worker->next;
		pthread_mutex_unlock(&(_tpoolH.queue_lock));

		if (worker->process)
		{
			(*(worker->process))(worker->arg);
		}

		if (worker->destroy)
		{
			(*(worker->destroy))(worker->arg);
		}

		free(worker);
		worker = NULL;
	}
}


