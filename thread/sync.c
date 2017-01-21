#include "sync.h"
#include "list.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"

// ��ʼ���ź���
void sema_init(struct semaphore* psema, uint8_t value) {
	psema->value = value;
	list_init(&psema->waiters);
}

// ��ʼ����
void lock_init(struct lock* plock) {
	plock->holder = NULL;
	plock->holder_repeat_nr = 0;
	sema_init(&plock->semaphore, 1); // �ź���Ϊ1
}

// �ź���down����
void sema_down(struct semaphore* psema) {
	// ���жϱ�֤ԭ�Ӳ���
	enum intr_status old_status = intr_disable();
	while (psema->value == 0) { // �ź���Ϊ0 ��ʾ�����˳���
		ASSERT(!elem_find(&psema->waiters, &running_thread()->general_tag));
		if (elem_find(&psema->waiters, &running_thread()->general_tag)) {
			PANIC("sema_down : thread blocked has been in waiters_list \n");
		}

		// ��ӵ��ȴ�����
		list_append(&psema->waiters, &running_thread()->general_tag);
		thread_block(TASK_BLOCKED); // �����߳� ���ǴӾ��������ϻ�����
	}

	psema->value --;
	ASSERT(psema->value == 0);
	// �ָ�֮ǰ���ж�״̬
	intr_set_status(old_status);
}

// �ź���up����
void sema_up(struct semaphore* psema) {
	enum intr_status old_status = intr_disable();
	ASSERT(psema->value == 0);
	if (!list_empty(&psema->waiters)) {
		// �ӵȴ�������ȡ��һ���߳�pcb
		struct task_struct* thread_blocked = elem2entry(struct task_struct, general_tag, list_pop(&psema->waiters));
		// �����߳�
		thread_unblock(thread_blocked);
	}
	psema->value ++;
	ASSERT(psema->value == 1);
	intr_set_status(old_status);
}

// ��ȡ��
void lock_acquire(struct lock* plock) {
	if (plock->holder != running_thread()) {
		sema_down(&plock->semaphore); // �ź���P����
		plock->holder = running_thread();
		ASSERT(plock->holder_repeat_nr == 0);
		plock->holder_repeat_nr = 1;
	}else {
		plock->holder_repeat_nr ++;
	}
}

// �ͷ���
void lock_release(struct lock* plock) {
	ASSERT(plock->holder == running_thread());
	if (plock->holder_repeat_nr > 1) {
		plock->holder_repeat_nr --;
		return ;
	}
	ASSERT(plock->holder_repeat_nr == 1);

	plock->holder = NULL;
	plock->holder_repeat_nr = 0;
	sema_up(&plock->semaphore); // �ź���V����
}
