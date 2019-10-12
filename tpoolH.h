#ifndef __tpoolH_h__
#define __tpoolH_h__

int tpoolH_init(int _thread_num);
int tpoolH_destroy();
int tpoolH_add_worker(void* (*process)(void* arg), void* arg,
                      void* (*destroy)(void* arg));
int tpoolH_add_worker_2(void* (*process)(void* arg));

#endif /* __tpoolH_h__ */

