#include "process.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "thread.h"
#include "list.h"
#include "tss.h"
#include "interrupt.h"
#include "string.h"
#include "console.h"

extern void intr_exit(void);

// 构建用户进程初始上下文信息
void start_process(void* filename_) {
	void* function = filename_;
	struct task_struct* cur = running_thread();
	// 之前初始化的时候是
	// cur->self_kstack -= sizeof(intr_stack);
	// cur->self_kstack -= sizeof(thread_stack);
	cur->self_kstack += sizeof(struct thread_stack);
	struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;
	proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
	proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
	proc_stack->gs = 0;
	proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
	proc_stack->eip = function; // 待执行的用户程序地址
	proc_stack->cs = SELECTOR_U_CODE;
	proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
	proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) +  PG_SIZE);
	proc_stack->ss = SELECTOR_U_DATA;
	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}

// 更新页表 cr3寄存器
void page_dir_activate(struct task_struct* p_thread) {
	// 每个进程都拥有独立的虚拟地址空间,本质上就是各个进程都有单独的页表,页表是存储
	// 在页表寄存器 CR3 中的, CR3 寄存器只有 1 个,因此,不同的进程在执行前,我们要在 CR3 寄存器中为
	// 其换上与之配套的页表,从而实现了虚拟地址空间的隔离。
	//
	uint32_t pagedir_phy_addr = 0x100000; // 默认是内核的页目录物理地址，也就是内核线程用的页目录表
	// 用户进程有自己的页目录表
	if (p_thread->pgdir != NULL) {
		pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
	}
	// 更新页目录寄存器cr3 使新页表生效
	asm volatile("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

// 更新页表(cr3寄存器) + 更新tss中的esp0为进程特权级0的栈
void process_activate(struct task_struct* p_thread) {
	ASSERT(p_thread != NULL);
	page_dir_activate(p_thread);

	// 内核线程特权级本身是0，不需要更新
	if (p_thread->pgdir) {
		// 更新进程的esp0,用于此进程被中断时保留上下文
		// esp0 指向进程上下文保存的结构体
		update_tss_esp(p_thread);
	}
}

// 创建页目录表，并将当前页表表示内核空间的pde复制
uint32_t* create_page_dir(void) {
	uint32_t* page_dir_vaddr = get_kernel_pages(1);
	if (page_dir_vaddr == NULL) {
		console_put_str("create_page_dir: get_kernel_page failed!");
		return NULL;
	}

	// 1.复制页表
	// page_dir_vaddr + 0x300 * 4 是内核页目录的第768项
	memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300 * 4), (uint32_t*)(0xfffff000 + 0x300 * 4), 1024);

	// 2.更新页目录地址
	uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
	// 页目录地址是存在页目录的最后一项，更新页目录地址为新页目录的物理地址
	page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;
	return page_dir_vaddr;
}

// 创建用户进程虚拟地址位图
void create_user_vaddr_bitmap(struct task_struct* user_prog) {
	user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
	uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
	user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
	user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
	bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

// 创建用户进程
void process_execute(void* filename, char* name) {
	// pcb 内核的数据结构，由内核来维护进程信息，所以要在内核内存池中申请
	struct task_struct* thread = get_kernel_pages(1);
	init_thread(thread, name, default_prio);
	create_user_vaddr_bitmap(thread);
	// start_process 是线程执行的函数，filename是函数的参数
	thread_create(thread, start_process, filename);
	thread->pgdir = create_page_dir();

	// 把进程添加到任务列表中
	enum intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	list_append(&thread_ready_list, &thread->general_tag);

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	list_append(&thread_all_list, &thread->all_list_tag);
	intr_set_status(old_status);
}
