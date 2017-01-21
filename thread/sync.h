#ifndef __THREAD_SYNC_H
#define __THREAD_SYNC_H
#include "list.h"
#include "stdint.h"
#include "thread.h"

// �ź���
struct semaphore {
	uint8_t value; // �ź���
	struct list waiters; // �ȴ�����
};

// ��
struct lock {
	struct task_struct* holder; // ���ĳ�����
	struct semaphore semaphore; // ��Ԫ�ź���ʵ����
	uint32_t holder_repeat_nr; // ���������ظ��������Ĵ���
};

void sema_init(struct semaphore* psema, uint8_t value);
void sema_down(struct semaphore* psema);
void sema_up(struct semaphore* psema);
void lock_init(struct lock* plock);
void lock_acquire(struct lock* plock);
void lock_release(struct lock* plock);
#endif
