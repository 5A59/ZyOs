#include "thread.h"
#include "stdint.h"
#include "string.h"
#include "global.h"
#include "debug.h"
#include "interrupt.h"
#include "print.h"
#include "memory.h"
#include "process.h"
#include "sync.h"

#define PG_SIZE 4096

struct task_struct* main_thread; // ���߳�pcb
struct task_struct* idle_thread; // ϵͳ���е�ʱ�����е��߳�
struct list thread_ready_list; // ��������
struct list thread_all_list; // �������е��߳�
static struct list_elem* thread_tag; // ��������е��߳̽ڵ�
struct lock pid_lock; // ����pid��

extern void switch_to(struct task_struct* cur, struct task_struct* next); // �л��߳�

// ϵͳ���е�ʱ�����е��߳�
static void idle(void* arg UNUSED) {
	while(1) {
		thread_block(TASK_BLOCKED);
		// ִ��hlt���账�ڿ��ж����
		asm volatile ("sti; hlt" : : : "memory");
	}
}

struct task_struct* running_thread() {
	uint32_t esp;
	asm("mov %%esp, %0" : "=g" (esp));
	// esp �������־���pcb����ʼ��ַ
	return (struct task_struct*)(esp & 0xfffff000);
}

// kernel_threadִ��function(func_arg)�̺߳���
static void kernel_thread(thread_func* function, void* func_arg) {
	// ���жϣ�����ʱ���ж� �Ӷ����������߳�
	intr_enable();
	function(func_arg);
}

// ����pid
static pid_t allocate_pid(void) {
	static pid_t next_pid = 0;
	lock_acquire(&pid_lock);
	next_pid ++;
	lock_release(&pid_lock);
	return next_pid;
}

// �����߳�pcb,��ʼ��task_struct
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
	// Ԥ���ж�ʹ��ջ�Ŀռ�
	pthread->self_kstack -= sizeof(struct intr_stack);

	// Ԥ���߳�ջ�ռ�
	pthread->self_kstack -= sizeof(struct thread_stack);
	struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

// ��ʼ���̻߳�����Ϣ
// prio : �߳����ȼ�
void init_thread(struct task_struct* pthread, char* name, int prio) {
	memset(pthread, 0, sizeof(*pthread));
	pthread->pid = allocate_pid();
	strcpy(pthread->name, name);

	if (pthread == main_thread) {
		pthread->status = TASK_RUNNING;
	}else {
		pthread->status = TASK_READY;
	}

	pthread->self_kstack = (uint32_t*)((uint32_t)pthread + PG_SIZE);
	pthread->priority = prio;
	pthread->ticks = prio;
	pthread->elapsed_ticks = 0;
	pthread->pgdir = NULL;
	pthread->stack_magic = 0x19870916; // ����һ��������У��ж�ջ�߽�
}

// �����߳�pcb�������ڴ� ���뵽������
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
	// ����һҳ�ڴ�ҳ pcbλ���ں˿ռ�
	struct task_struct* thread = get_kernel_pages(1);

	init_thread(thread, name, prio);
	thread_create(thread, function, func_arg);

	// ��֤֮ǰ���ڶ�����
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	// ���뵽�����̶߳�����
	list_append(&thread_ready_list, &thread->general_tag);

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	// ���뵽ȫ���̶߳�����
	list_append(&thread_all_list, &thread->all_list_tag);

	return thread;
}

// main��������Ϊ���߳�
static void make_main_thread(void) {
	// main�߳��Ѿ����У���loader.S��Ԥ����tcb ���Բ���Ҫ�ٷ����ڴ�ҳ
	main_thread = running_thread();
	init_thread(main_thread, "main", 31);

	// ���������̲߳���Ҫ���뵽����������
	

	ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
	// ���뵽ȫ���̶߳�����
	list_append(&thread_all_list, &main_thread->all_list_tag);
}

// ʵ���������
void schedule() {
	ASSERT(intr_get_status() == INTR_OFF);

	struct task_struct* cur = running_thread();
	if (cur->status == TASK_RUNNING) { // ʱ��Ƭ���ˣ����뵽����������
		ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
		list_append(&thread_ready_list, &cur->general_tag);
		cur->ticks = cur->priority;
		cur->status = TASK_READY;
	}else {
		//��ʱ�߳���Ҫĳ�¼���������ܼ������У�����Ҫ�����������
	}

	// ����������û�п����е��߳�,����idle
	if (list_empty(&thread_ready_list)) {
		thread_unblock(idle_thread);
	}

	// ��ȡ����ͷ������
	ASSERT(!list_empty(&thread_ready_list));
	thread_tag = NULL;
	thread_tag = list_pop(&thread_ready_list);
	struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
	next->status = TASK_RUNNING;

	// ��������ҳ��
	process_activate(next);

	// �������������������Ϣ
	// �л��߳�
	switch_to(cur, next);
}

// ��ǰ�߳������Լ�
void thread_block(enum task_status stat) {
	ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));
	enum intr_status old_status = intr_disable();
	struct task_struct* cur_thread = running_thread();
	cur_thread->status = stat;
	schedule(); // ���µ�ǰ�߳�

	// ��ǰ�߳�����״̬�����Ż�ִ������
	intr_set_status(old_status);
}

// ���pthread����״̬
void thread_unblock(struct task_struct* pthread) {
	enum intr_status old_status = intr_disable();
	ASSERT(((pthread->status == TASK_BLOCKED) || (pthread->status == TASK_WAITING) || (pthread->status == TASK_HANGING)));
	if (pthread->status != TASK_READY) {
		ASSERT(!elem_find(&thread_ready_list, &pthread->general_tag));
		if (elem_find(&thread_ready_list, &pthread->general_tag)) {
			PANIC("thread_unblock: blocked thread in ready_list\n");
		}
		list_push(&thread_ready_list, &pthread->general_tag);
		pthread->status = TASK_READY;
	}
	intr_set_status(old_status);
}

// �����ó�cpu
void thread_yield(void) {
	struct task_struct* cur = running_thread();
	enum intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
	list_append(&thread_ready_list, &cur->general_tag);
	cur->status = TASK_READY;
	schedule();
	intr_set_status(old_status);
}

// ��ʼ���̻߳���
void thread_init(void) {
	put_str("thread_init start \n");
	list_init(&thread_ready_list);
	list_init(&thread_all_list);
	lock_init(&pid_lock);
	make_main_thread();
	idle_thread = thread_start("idle", 10, idle, NULL);
	put_str("thread_init done \n");
}


