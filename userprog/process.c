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

// �����û����̳�ʼ��������Ϣ
void start_process(void* filename_) {
	void* function = filename_;
	struct task_struct* cur = running_thread();
	// ֮ǰ��ʼ����ʱ����
	// cur->self_kstack -= sizeof(intr_stack);
	// cur->self_kstack -= sizeof(thread_stack);
	cur->self_kstack += sizeof(struct thread_stack);
	struct intr_stack* proc_stack = (struct intr_stack*)cur->self_kstack;
	proc_stack->edi = proc_stack->esi = proc_stack->ebp = proc_stack->esp_dummy = 0;
	proc_stack->ebx = proc_stack->edx = proc_stack->ecx = proc_stack->eax = 0;
	proc_stack->gs = 0;
	proc_stack->ds = proc_stack->es = proc_stack->fs = SELECTOR_U_DATA;
	proc_stack->eip = function; // ��ִ�е��û������ַ
	proc_stack->cs = SELECTOR_U_CODE;
	proc_stack->eflags = (EFLAGS_IOPL_0 | EFLAGS_MBS | EFLAGS_IF_1);
	proc_stack->esp = (void*)((uint32_t)get_a_page(PF_USER, USER_STACK3_VADDR) +  PG_SIZE);
	proc_stack->ss = SELECTOR_U_DATA;
	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (proc_stack) : "memory");
}

// ����ҳ�� cr3�Ĵ���
void page_dir_activate(struct task_struct* p_thread) {
	// ÿ�����̶�ӵ�ж����������ַ�ռ�,�����Ͼ��Ǹ������̶��е�����ҳ��,ҳ���Ǵ洢
	// ��ҳ��Ĵ��� CR3 �е�, CR3 �Ĵ���ֻ�� 1 ��,���,��ͬ�Ľ�����ִ��ǰ,����Ҫ�� CR3 �Ĵ�����Ϊ
	// �任����֮���׵�ҳ��,�Ӷ�ʵ���������ַ�ռ�ĸ��롣
	//
	uint32_t pagedir_phy_addr = 0x100000; // Ĭ�����ں˵�ҳĿ¼�����ַ��Ҳ�����ں��߳��õ�ҳĿ¼��
	// �û��������Լ���ҳĿ¼��
	if (p_thread->pgdir != NULL) {
		pagedir_phy_addr = addr_v2p((uint32_t)p_thread->pgdir);
	}
	// ����ҳĿ¼�Ĵ���cr3 ʹ��ҳ����Ч
	asm volatile("movl %0, %%cr3" : : "r" (pagedir_phy_addr) : "memory");
}

// ����ҳ��(cr3�Ĵ���) + ����tss�е�esp0Ϊ������Ȩ��0��ջ
void process_activate(struct task_struct* p_thread) {
	ASSERT(p_thread != NULL);
	page_dir_activate(p_thread);

	// �ں��߳���Ȩ��������0������Ҫ����
	if (p_thread->pgdir) {
		// ���½��̵�esp0,���ڴ˽��̱��ж�ʱ����������
		// esp0 ָ����������ı���Ľṹ��
		update_tss_esp(p_thread);
	}
}

// ����ҳĿ¼��������ǰҳ���ʾ�ں˿ռ��pde����
uint32_t* create_page_dir(void) {
	uint32_t* page_dir_vaddr = get_kernel_pages(1);
	if (page_dir_vaddr == NULL) {
		console_put_str("create_page_dir: get_kernel_page failed!");
		return NULL;
	}

	// 1.����ҳ��
	// page_dir_vaddr + 0x300 * 4 ���ں�ҳĿ¼�ĵ�768��
	memcpy((uint32_t*)((uint32_t)page_dir_vaddr + 0x300 * 4), (uint32_t*)(0xfffff000 + 0x300 * 4), 1024);

	// 2.����ҳĿ¼��ַ
	uint32_t new_page_dir_phy_addr = addr_v2p((uint32_t)page_dir_vaddr);
	// ҳĿ¼��ַ�Ǵ���ҳĿ¼�����һ�����ҳĿ¼��ַΪ��ҳĿ¼�������ַ
	page_dir_vaddr[1023] = new_page_dir_phy_addr | PG_US_U | PG_RW_W | PG_P_1;
	return page_dir_vaddr;
}

// �����û����������ַλͼ
void create_user_vaddr_bitmap(struct task_struct* user_prog) {
	user_prog->userprog_vaddr.vaddr_start = USER_VADDR_START;
	uint32_t bitmap_pg_cnt = DIV_ROUND_UP((0xc0000000 - USER_VADDR_START) / PG_SIZE / 8, PG_SIZE);
	user_prog->userprog_vaddr.vaddr_bitmap.bits = get_kernel_pages(bitmap_pg_cnt);
	user_prog->userprog_vaddr.vaddr_bitmap.btmp_bytes_len = (0xc0000000 - USER_VADDR_START) / PG_SIZE / 8;
	bitmap_init(&user_prog->userprog_vaddr.vaddr_bitmap);
}

// �����û�����
void process_execute(void* filename, char* name) {
	// pcb �ں˵����ݽṹ�����ں���ά��������Ϣ������Ҫ���ں��ڴ��������
	struct task_struct* thread = get_kernel_pages(1);
	init_thread(thread, name, default_prio);
	create_user_vaddr_bitmap(thread);
	// start_process ���߳�ִ�еĺ�����filename�Ǻ����Ĳ���
	thread_create(thread, start_process, filename);
	thread->pgdir = create_page_dir();

	// �ѽ�����ӵ������б���
	enum intr_status old_status = intr_disable();
	ASSERT(!elem_find(&thread_ready_list, &thread->general_tag));
	list_append(&thread_ready_list, &thread->general_tag);

	ASSERT(!elem_find(&thread_all_list, &thread->all_list_tag));
	list_append(&thread_all_list, &thread->all_list_tag);
	intr_set_status(old_status);
}
