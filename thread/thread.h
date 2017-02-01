#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "memory.h"

#define MAX_FILES_OPEN_PER_PROC 8 // 每个任务可打开的文件数

// 自定义函数类型 作为线程调用的函数
typedef void thread_func(void*);
typedef int16_t pid_t;

// 线程或进程状态
enum task_status {
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_WAITING,
	TASK_HANGING,
	TASK_DIED
};

// 中断保存上下文,其实就是保存寄存器的值
struct intr_stack {
	uint32_t vec_no; // kernel.S VECTOR中push %1 压入的中断号
	uint32_t edi;
	uint32_t esi;
	uint32_t ebp;
	uint32_t esp_dummy;
	uint32_t ebx;
	uint32_t edx;
	uint32_t ecx;
	uint32_t eax;
	uint32_t gs;
	uint32_t fs;
	uint32_t es;
	uint32_t ds;

	// 以下是cpu从低特权级进入高特权级的时候压入
	uint32_t err_code;
	void (*eip) (void);
	uint32_t cs;
	uint32_t eflags;
	void* esp;
	uint32_t ss;
};

// 线程自己的栈
// 用于存储线程中执行的函数
// 在switch_to时保存线程环境
struct thread_stack {
	uint32_t ebp;
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;

	void (*eip) (thread_func* func, void* func_arg);

	void (*unused_retaddr); // 占位 充当返回地址
	thread_func* function; // kernel_thread调用的函数
	void* func_arg; // function函数参数
};

// 线程或进程pcb 程序控制块
struct task_struct {
	uint32_t* self_kstack; // 内核线程自己的内核栈地址
	pid_t pid;
	enum task_status status; // 线程状态
	char name[16]; // 线程名称
	uint8_t priority; // 线程优先级
	uint8_t ticks; // 时间片

	uint32_t elapsed_ticks; // 任务执行多长时间

	int32_t fd_table[MAX_FILES_OPEN_PER_PROC]; // 文件描述符数组

	struct list_elem general_tag; // 线程在一般队列中的节点
	struct list_elem all_list_tag; // 线程在线程队列thread_all_list中的节点

	uint32_t* pgdir; // 进程自己页表的虚拟地址
	struct virtual_addr userprog_vaddr; // 用户进程的虚拟地址
	struct mem_block_desc u_block_desc[DESC_CNT]; // 用户进程内存块描述符
	uint32_t cwd_inode_nr; // 进程所在工作目录的inode编号
	int16_t parent_pid; // 父进程pid
	uint32_t stack_magic; // 用于栈的边界标记 检测栈的溢出
};

extern struct list thread_ready_list;
extern struct list thread_all_list;

void thread_create(struct task_struct* pthread, thread_func function, void* func_arg);
void init_thread(struct task_struct* pthread, char* name, int prio);
struct task_struct* thread_start(char* name, int prio, thread_func function, void* func_arg);
struct task_struct* running_thread(void);
void schedule(void);
void thread_init(void);
void thread_block(enum task_status stat);
void thread_unblock(struct task_struct* pthread);
void thread_yield(void);
pid_t fork_pid(void);
void sys_ps(void);
#endif

