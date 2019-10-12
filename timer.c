/*
	-----------++++----------
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>

#include "timer.h"
#include "list.h"
#include "thread.h"
//#include "log.h"
#include "tpoolH.h"

#define 	USE_TPOOL					0
#define 	EVENT_PROCESS_NUM			1

#define 	BASE_HANDEL					100
#define		TIMER_LIST_HEAD_ADDR		(timer_set_t *)(&g_timer.timer_list.list)
/*printf("\033[0;32m""+++ timer_name:%s, handle:%d, timer_count:%d""\033[0m\n", p_timer->cb_func_name, p_timer->handle, g_timer.timer_count); \*/


#define TIMER_LIST_ADD(p_timer, g_timer) \
    do{ \
        list_add_tail(&p_timer->list, &g_timer.timer_list.list); \
    }while(0)


#if 0
// 如果回调函数的参数(p_timer->cb_param)是动态分配空间，则此处需要free(p_timer->cb_param)
#define TIMER_LIST_DEL(p_timer, g_timer) \
	do{ \
		g_timer.timer_count--; \
		printf("+++ g_timer.timer_count=%d\n", g_timer.timer_count); \
		if (p_timer->cb_param != NULL) \
			free(p_timer->cb_param); \
		list_del(&p_timer->list); \
 		free(p_timer); \
	}while(0)
#else
/*printf("\033[0;32m""--- timer_name:%s, handle:%d, timer_count=%d""\033[0m\n", p_timer->cb_func_name, p_timer->handle, g_timer.timer_count); \*/


#define TIMER_LIST_DEL(p_timer, g_timer) \
	do{ \
		g_timer.timer_count--; \
		p_timer->handle = 0; \
		list_del(&p_timer->list); \
 		free(p_timer); \
	}while(0)
#endif

// clock_gettime需要链接时钟库: -lrt
#define GET_CLOCK_MONOTONIC_TIME_MS(p_time_ms)	{struct timespec ts = {0, 0};clock_gettime(CLOCK_MONOTONIC, &ts);*p_time_ms = ts.tv_sec*1000 + ts.tv_nsec/(1000*1000);}

typedef struct timer_set_s
{
	struct list_head list;
	int handle;
	int interval;
	int run_time;					// -1:循环运行; N(N>0):运行N次后停止
	timer_cb func;					// 回调函数
	void* cb_param;					// 回调函数参数
	unsigned int start_time;		// ms
	unsigned int end_time;			// ms
	unsigned int cb_start_time;		// ms
	unsigned int cb_end_time;		// ms
	char cb_func_name[64];			//
} timer_set_t;

typedef struct simple_timer_s
{
	timer_set_t timer_list;
	unsigned int timer_count;
	int base_handle;
	int timer_running;
	int curr_task_hdl;
	int timer_living;
	pthread_t loop_tid[EVENT_PROCESS_NUM];
	int monitor_running;
	pthread_t monitor_tid;
	pthread_mutex_t timer_lock;
} simple_timer_t;


static simple_timer_t g_timer;

static int _loop_event_process();
static int _monitor_process();
int _timer_stop(timer_set_t* p_timer);


int timer_init()
{
	int i = 0;
	memset(&g_timer, 0, sizeof(g_timer));
	pthread_mutex_init(&g_timer.timer_lock, NULL);
	INIT_LIST_HEAD(&(g_timer.timer_list.list));
	g_timer.base_handle = BASE_HANDEL;
	g_timer.timer_running = 1;

	for (i = 0; i < EVENT_PROCESS_NUM; i++)
	{
		if (pthread_create(&g_timer.loop_tid[i], "timer-loop", NULL,
		                   (void*)_loop_event_process, NULL) < 0)
		{
			printf("thread_create error!\n");
			return -1;
		}
	}

	g_timer.monitor_running = 1;

	if (pthread_create(&g_timer.monitor_tid, "timer_monitor", NULL,
	                   (void*)_monitor_process, NULL) < 0)
	{
		printf("thread_create error!\n");
		return -1;
	}

	// init tpool
#if USE_TPOOL
	return tpoolH_init(2);
#endif
	return 0;
}

static int _monitor_process()
{
	int printf_flag = 1;

	while (g_timer.monitor_running)
	{
		sleep(1);

		if (++g_timer.timer_living > 2 && printf_flag > 0)
		{
			printf_flag = 0;
			g_timer.timer_living = 0;
			char syscall_str[128] = {0};
			timer_set_t* p_timer = NULL;
			timer_set_t* p_next  = NULL;
			list_for_each_entry_safe(p_timer, p_next, &g_timer.timer_list.list, list)
		{
				if (g_timer.curr_task_hdl == p_timer->handle)
				{
					printf("ERROR! timer block...... handl:%d, cb_func_name:%s\n", p_timer->handle,
					       p_timer->cb_func_name);
					sprintf(syscall_str, "touch /app/%s", p_timer->cb_func_name);

					if (strlen(syscall_str))
					{
						printf("syscall_str:%s\n", syscall_str);
						system(syscall_str);
					}

					break;
				}
			}
		}
	}

	return 0;
}

static int _loop_event_process()
{
	unsigned int curr_time_ms = 0;
	struct timespec ts = {0, 0};

	while (g_timer.timer_running)
	{
		//GET_CLOCK_MONOTONIC_TIME_MS(&curr_times_ms);
		memset(&ts, 0, sizeof(ts));
		clock_gettime(CLOCK_MONOTONIC, &ts);
		curr_time_ms = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
		pthread_mutex_lock(&g_timer.timer_lock);
		timer_set_t* p_timer = NULL;
		timer_set_t* p_next  = NULL;
		list_for_each_entry_safe(p_timer, p_next, &g_timer.timer_list.list, list)
		{
			if (p_timer->run_time > 0)  					// 运行N次(N>0)的定时器
			{
				if (curr_time_ms >= p_timer->end_time)
				{
					p_timer->run_time--;
					g_timer.curr_task_hdl = p_timer->handle;
					// 更新开始时间和结束时间
					p_timer->start_time = curr_time_ms;
					p_timer->end_time = p_timer->start_time + p_timer->interval;
					memset(&ts, 0, sizeof(ts));
					clock_gettime(CLOCK_MONOTONIC, &ts);
					p_timer->cb_start_time = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);

					//GET_CLOCK_MONOTONIC_TIME_MS(&p_timer->cb_start_time);
					if (p_timer->func != NULL)
					{
						p_timer->func(p_timer->cb_param);
					}

					//GET_CLOCK_MONOTONIC_TIME_MS(&p_timer->cb_end_time);
					memset(&ts, 0, sizeof(ts));
					clock_gettime(CLOCK_MONOTONIC, &ts);
					p_timer->cb_end_time = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);

					if ((p_timer->cb_end_time - p_timer->cb_start_time) > 1)
					{
						printf("\033[0;32m""======111====== Tid %ld Callback task %s use %u ms""\033[0m\n",
						       syscall(SYS_gettid), p_timer->cb_func_name,
						       (p_timer->cb_end_time - p_timer->cb_start_time));
					}

					if (0 == p_timer->run_time)
					{
						_timer_stop(p_timer);
					}
				}
			}
			else if(p_timer->run_time == -1)  			// 循环运行
			{
				if ((curr_time_ms - p_timer->start_time) >= p_timer->interval)
				{
					// 更新开始时间和结束时间
					p_timer->start_time = curr_time_ms;
					p_timer->end_time = p_timer->start_time + p_timer->interval;
					g_timer.curr_task_hdl = p_timer->handle;
					memset(&ts, 0, sizeof(ts));
					clock_gettime(CLOCK_MONOTONIC, &ts);
					p_timer->cb_start_time = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);

					//GET_CLOCK_MONOTONIC_TIME_MS(&p_timer->cb_start_time);
					if (p_timer->func != NULL)
					{
						p_timer->func(p_timer->cb_param);
					}

					//GET_CLOCK_MONOTONIC_TIME_MS(&p_timer->cb_end_time);
					memset(&ts, 0, sizeof(ts));
					clock_gettime(CLOCK_MONOTONIC, &ts);
					p_timer->cb_end_time = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);

					if ((p_timer->cb_end_time - p_timer->cb_start_time) > 1)
					{
						printf("\033[0;32m""======222====== Tid %ld Callback task %s use %u ms""\033[0m\n",
						       syscall(SYS_gettid), p_timer->cb_func_name,
						       (p_timer->cb_end_time - p_timer->cb_start_time));
					}
				}
			}
		}
		pthread_mutex_unlock(&g_timer.timer_lock);
		usleep(1 * 1000);	// 1ms
		g_timer.timer_living = 0;
	}

	return 0;
}

int timer_start(int run_time	/*-1:循环运行; N(N>0):运行N次后停止*/
                , int interval	/*ms*/
                , char* cb_func_name
                , timer_cb func
                , void* param)
{
	unsigned int curr_time_ms;
	struct timespec ts = {0, 0};

	if (g_timer.timer_running != 1)
	{
		printf("timer not init!\n");
		return -1;
	}

	timer_set_t* p_timer = (timer_set_t*)malloc(sizeof(timer_set_t));

	if (NULL == p_timer)
	{
		printf("malloc error!\n");
		return -1;
	}

	//GET_CLOCK_MONOTONIC_TIME_MS(&curr_time_ms);
	memset(&ts, 0, sizeof(ts));
	clock_gettime(CLOCK_MONOTONIC, &ts);
	curr_time_ms = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
	pthread_mutex_lock(&g_timer.timer_lock);
	p_timer->handle = g_timer.base_handle++;
	g_timer.timer_count++;
	p_timer->cb_param = param;
	p_timer->start_time = curr_time_ms;
	p_timer->interval = interval;
	p_timer->end_time = p_timer->start_time + p_timer->interval;
	p_timer->func = func;
	p_timer->run_time = run_time;
	memset(p_timer->cb_func_name, 0, sizeof(p_timer->cb_func_name));
	strncpy(p_timer->cb_func_name
	        , cb_func_name
	        , strlen(cb_func_name) > sizeof(p_timer->cb_func_name) ? sizeof(
	            p_timer->cb_func_name) : strlen(cb_func_name));
	TIMER_LIST_ADD(p_timer, g_timer);
	pthread_mutex_unlock(&g_timer.timer_lock);
	return p_timer->handle;
}

int timer_ioctrl(int handle, int cmd, int channel, void* param,
                 int size_of_param)
{
	/*printf("cmd=%d, handle=%d, size_of_param=%d\n",
			cmd,
			handle,
			size_of_param);*/
	timer_set_t* p_timer = NULL;
	timer_set_t* p_next  = NULL;

	if (-1 == handle)
	{
		// 如果handle等于-1，则不对handle进行校验
	}
	else
	{
		list_for_each_entry_safe(p_timer, p_next, &g_timer.timer_list.list, list)
		{
			if (handle == p_timer->handle)
			{
				break;
			}
		}

		if (TIMER_LIST_HEAD_ADDR == p_timer)
		{
			printf("_______Find error!\n");
			return -1;
		}
	}

	switch (cmd)
	{
		case TIMER_CMD_GETTMCOUNT:
			{
				memcpy(param, &g_timer.timer_count, size_of_param);
				break;
			}

		case TIMER_CMD_GETTMPASST:
			{
				unsigned int curr_time_ms;
				unsigned int pass_time;
				struct timespec ts = {0, 0};
				clock_gettime(CLOCK_MONOTONIC, &ts);
				curr_time_ms = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
				pass_time = curr_time_ms - p_timer->start_time;
				memcpy(param, &pass_time, size_of_param);
				break;
			}

		case TIMER_CMD_GETTMREACH:
			{
				unsigned int curr_time_ms;
				unsigned int reach_time;
				struct timespec ts = {0, 0};
				clock_gettime(CLOCK_MONOTONIC, &ts);
				curr_time_ms = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
				reach_time = p_timer->end_time - curr_time_ms;
				memcpy(param, &reach_time, size_of_param);
				break;
			}

		case TIMER_CMD_RESET_STARTTIME:
			{
				unsigned int reset_time = 0;
				//memcpy(&reset_time, param, size_of_param);
				unsigned int curr_time_ms;
				struct timespec ts = {0, 0};
				clock_gettime(CLOCK_MONOTONIC, &ts);
				curr_time_ms = ts.tv_sec * 1000 + ts.tv_nsec / (1000 * 1000);
				reset_time = curr_time_ms;

				if (reset_time != 0)
				{
					p_timer->start_time = reset_time;
					p_timer->end_time = p_timer->start_time + p_timer->interval;
				}

				break;
			}

		default:
			{
				printf("error! No command!\n");
				return -1;
			}
	}

	return 0;
}

int _timer_stop(timer_set_t* p_timer)
{
	TIMER_LIST_DEL(p_timer, g_timer);
	return 0;
}

int timer_stop(int handle)
{
	pthread_mutex_lock(&g_timer.timer_lock);
	timer_set_t* p_timer = NULL;
	timer_set_t* p_next  = NULL;
	list_for_each_entry_safe(p_timer, p_next, &g_timer.timer_list.list, list)
	{
		if (handle == p_timer->handle)
		{
			TIMER_LIST_DEL(p_timer, g_timer);
			pthread_mutex_unlock(&g_timer.timer_lock);
			return 0;
		}
	}
	pthread_mutex_unlock(&g_timer.timer_lock);

	if (TIMER_LIST_HEAD_ADDR == p_timer)
	{
		printf("Handle error!\n");
	}

	return -1;
}

int timer_deinit()
{
	int i = 0;
	pthread_mutex_lock(&g_timer.timer_lock);
	g_timer.timer_running = 0;
	g_timer.monitor_running = 0;
	pthread_mutex_unlock(&g_timer.timer_lock);

	for (i = 0; i < EVENT_PROCESS_NUM; i++)
	{
		pthread_join(g_timer.loop_tid[i], NULL);
	}

	pthread_join(g_timer.monitor_tid, NULL);
	timer_set_t* p_timer = NULL;
	timer_set_t* p_next  = NULL;
	list_for_each_entry_safe(p_timer, p_next, &g_timer.timer_list.list, list)
	{
		if (p_timer->handle)
		{
			TIMER_LIST_DEL(p_timer, g_timer);
		}
	}
	pthread_mutex_destroy(&g_timer.timer_lock);
	memset(&g_timer, 0, sizeof(g_timer));
	// deinit tpool
#if	USE_TPOOL
	return tpoolH_destroy();
#endif
	return 0;
}

