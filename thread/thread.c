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

struct task_struct* main_thread; // 主线程pcb
struct task_struct* idle_thread; // 系统空闲的时候运行的线程
struct list thread_ready_list; // 就绪队列
struct list thread_all_list; // 保存所有的线程
static struct list_elem* thread_tag; // 保存队列中的线程节点
struct lock pid_lock; // 分配pid锁

extern void switch_to(struct task_struct* cur, struct task_struct* next); // 切换线程

// 系统空闲的时候运行的线程
static void idle(void* arg UNUSED) {
	while(1) {
		thread_block(TASK_BLOCKED);
		// 执行hlt必需处在开中断情况
		asm volatile ("sti; hlt" : : : "memory");
	}
}

struct task_struct* running_thread() {
	uint32_t esp;
	asm("mov %%esp, %0" : "=g" (esp));
	// esp 整数部分就是pcb的起始地址
	return (struct task_struct*)(esp & 0xfffff000);
}

// kernel_thread执行function(func_arg)线程函数
static void kernel_thread(thread_func* function, void* func_arg) {
	// 开中断，接受时钟中断 从而调度其他线程
	intr_enable();
	function(func_arg);
}

// 分配pid
static pid_t allocate_pid(void) {
	static pid_t next_pid = 0;
	lock_acquire(&pid_lock);
	next_pid ++;
	lock_release(&pid_lock);
	return next_pid;
}

// 创建线程pcb,初始化task_struct
void thread_create(struct task_struct* pthread, thread_func function, void* func_arg) {
	// 预留中断使用栈的空间
	pthread->self_kstack -= sizeof(struct intr_stack);

	// 预留线程栈空间
	pthread->self_kstack -= sizeof(struct thread_stack);
	struct thread_stack* kthread_stack = (struct thread_stack*)pthread->self_kstack;
	kthread_stack->eip = kernel_thread;
	kthread_stack->function = function;
	kthread_stack->func_arg = func_arg;
	kthread_stack->ebp = kthread_stack->ebx = kthread_stack->esi = kthread_stack->edi = 0;
}

// 初始化线程基本信息
// prio : 线程优先级
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
	pthread->stack_magic = 0x19870916; // 特殊一点的数就行，判断栈边界
}

// 创建线程pcb并分配内存 加入到队列中
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg) {
	// 分配一页内存页 pcb位于内核空间
	struct task_struct* thread = get_kernel_pages(1);

	init_thread(thread, name, prio);
	thread_create(thread, function, func_arg);

	// 保证之前不在队列中
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	// 加入到就绪线程队列中
	list_append(&thread_ready_list, &thread->general_tag);

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	// 加入到全部线程队列中
	list_append(&thread_all_list, &thread->all_list_tag);

	return thread;
}

// main函数设置为主线程
static void make_main_thread(void) {
	// main线程已经运行，在loader.S中预留了tcb 所以不需要再分配内存页
	main_thread = running_thread();
	init_thread(main_thread, "main", 31);

	// 正在运行线程不需要加入到就绪队列中
	

	ASSERT(!elem_find(&thread_all_list, &main_thread->all_list_tag));
	// 加入到全部线程队列中
	list_append(&thread_all_list, &main_thread->all_list_tag);
}

// 实现任务调度
void schedule() {
	ASSERT(intr_get_status() == INTR_OFF);

	struct task_struct* cur = running_thread();
	if (cur->status == TASK_RUNNING) { // 时间片到了，加入到就绪队列中
		ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
		list_append(&thread_ready_list, &cur->general_tag);
		cur->ticks = cur->priority;
		cur->status = TASK_READY;
	}else {
		//此时线程需要某事件发生后才能继续运行，不需要加入就绪队列
	}

	// 就绪队列中没有可运行的线程,唤醒idle
	if (list_empty(&thread_ready_list)) {
		thread_unblock(idle_thread);
	}

	// 获取队列头的任务
	ASSERT(!list_empty(&thread_ready_list));
	thread_tag = NULL;
	thread_tag = list_pop(&thread_ready_list);
	struct task_struct* next = elem2entry(struct task_struct, general_tag, thread_tag);
	next->status = TASK_RUNNING;

	// 更新任务页表
	process_activate(next);

	// 以上是设置新任务的信息
	// 切换线程
	switch_to(cur, next);
}

// 当前线程阻塞自己
void thread_block(enum task_status stat) {
	ASSERT(((stat == TASK_BLOCKED) || (stat == TASK_WAITING) || (stat == TASK_HANGING)));
	enum intr_status old_status = intr_disable();
	struct task_struct* cur_thread = running_thread();
	cur_thread->status = stat;
	schedule(); // 换下当前线程

	// 当前线程阻塞状态解除后才会执行以下
	intr_set_status(old_status);
}

// 解除pthread阻塞状态
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

// 主动让出cpu
void thread_yield(void) {
	struct task_struct* cur = running_thread();
	enum intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list, &cur->general_tag));
	list_append(&thread_ready_list, &cur->general_tag);
	cur->status = TASK_READY;
	schedule();
	intr_set_status(old_status);
}

// 初始化线程环境
void thread_init(void) {
	put_str("thread_init start \n");
	list_init(&thread_ready_list);
	list_init(&thread_all_list);
	lock_init(&pid_lock);
	make_main_thread();
	idle_thread = thread_start("idle", 10, idle, NULL);
	put_str("thread_init done \n");
}


