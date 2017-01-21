#include "sync.h"
#include "list.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"

// 初始化信号量
void sema_init(struct semaphore* psema, uint8_t value) {
	psema->value = value;
	list_init(&psema->waiters);
}

// 初始化锁
void lock_init(struct lock* plock) {
	plock->holder = NULL;
	plock->holder_repeat_nr = 0;
	sema_init(&plock->semaphore, 1); // 信号量为1
}

// 信号量down操作
void sema_down(struct semaphore* psema) {
	// 关中断保证原子操作
	enum intr_status old_status = intr_disable();
	while (psema->value == 0) { // 信号量为0 表示被别人持有
		ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));
		if (elem_find(&psema->waiters, &running_thread()->general_tag)) {
			PANIC("sema_down : thread blocked has been in waiters_list \n");
		}

		// 添加到等待队列
		list_append(&psema->waiters, &running_thread()->general_tag);
		thread_block(TASK_BLOCKED); // 阻塞线程 就是从就绪队列上换下来
	}

	psema->value --;
	ASSERT(psema->value == 0);
	// 恢复之前的中断状态
	intr_set_status(old_status);
}

// 信号量up操作
void sema_up(struct semaphore* psema) {
	enum intr_status old_status = intr_disable();
	ASSERT(psema->value == 0);
	if (!list_empty(&psema->waiters)) {
		// 从等待队列中取出一个线程pcb
		struct task_struct* thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
		// 唤醒线程
		thread_unblock(thread_blocked);
	}
	psema->value ++;
	ASSERT(psema->value == 1);
	intr_set_status(old_status);
}

// 获取锁
void lock_acquire(struct lock* plock) {
	if (plock->holder != running_thread()) {
		sema_down(&plock->semaphore); // 信号量P操作
		plock->holder = running_thread();
		ASSERT(plock->holder_repeat_nr == 0);
		plock->holder_repeat_nr = 1;
	}else {
		plock->holder_repeat_nr ++;
	}
}

// 释放锁
void lock_release(struct lock* plock) {
	ASSERT(plock->holder == running_thread());
	if (plock->holder_repeat_nr > 1) {
		plock->holder_repeat_nr --;
		return ;
	}
	ASSERT(plock->holder_repeat_nr == 1);

	plock->holder = NULL;
	plock->holder_repeat_nr = 0;
	sema_up(&plock->semaphore); // 信号量V操作
}
