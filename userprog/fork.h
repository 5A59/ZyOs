#ifndef __USERPROG_FORK_H
#define __USERPROG_FORK_H
#include "thread.h"
// fork子进程　用户进程通过系统调用fork调用　内核线程不能调用
pid_t sys_fork(void);
#endif
