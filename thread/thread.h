#ifndef __THREAD_THREAD_H
#define __THREAD_THREAD_H
#include "stdint.h"
#include "list.h"
#include "bitmap.h"
#include "memory.h"

#define MAX_FILES_OPEN_PER_PROC 8 // ÿ������ɴ򿪵��ļ���

// �Զ��庯������ ��Ϊ�̵߳��õĺ���
typedef void thread_func(void*);
typedef int16_t pid_t;

// �̻߳����״̬
enum task_status {
	TASK_RUNNING,
	TASK_READY,
	TASK_BLOCKED,
	TASK_WAITING,
	TASK_HANGING,
	TASK_DIED
};

// �жϱ���������,��ʵ���Ǳ���Ĵ�����ֵ
struct intr_stack {
	uint32_t vec_no; // kernel.S VECTOR��push %1 ѹ����жϺ�
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

	// ������cpu�ӵ���Ȩ���������Ȩ����ʱ��ѹ��
	uint32_t err_code;
	void (*eip) (void);
	uint32_t cs;
	uint32_t eflags;
	void* esp;
	uint32_t ss;
};

// �߳��Լ���ջ
// ���ڴ洢�߳���ִ�еĺ���
// ��switch_toʱ�����̻߳���
struct thread_stack {
	uint32_t ebp;
	uint32_t ebx;
	uint32_t edi;
	uint32_t esi;

	void (*eip) (thread_func* func, void* func_arg);

	void (*unused_retaddr); // ռλ �䵱���ص�ַ
	thread_func* function; // kernel_thread���õĺ���
	void* func_arg; // function��������
};

// �̻߳����pcb ������ƿ�
struct task_struct {
	uint32_t* self_kstack; // �ں��߳��Լ����ں�ջ��ַ
	pid_t pid;
	enum task_status status; // �߳�״̬
	char name[16]; // �߳�����
	uint8_t priority; // �߳����ȼ�
	uint8_t ticks; // ʱ��Ƭ

	uint32_t elapsed_ticks; // ����ִ�ж೤ʱ��

	int32_t fd_table[MAX_FILES_OPEN_PER_PROC]; // �ļ�����������

	struct list_elem general_tag; // �߳���һ������еĽڵ�
	struct list_elem all_list_tag; // �߳����̶߳���thread_all_list�еĽڵ�

	uint32_t* pgdir; // �����Լ�ҳ��������ַ
	struct virtual_addr userprog_vaddr; // �û����̵������ַ
	struct mem_block_desc u_block_desc[DESC_CNT]; // �û������ڴ��������
	uint32_t cwd_inode_nr; // �������ڹ���Ŀ¼��inode���
	int16_t parent_pid; // ������pid
	uint32_t stack_magic; // ����ջ�ı߽��� ���ջ�����
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

