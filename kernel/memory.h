#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"

// �ڴ�ر��
enum pool_flags {
	PF_KERNEL = 1, // �ں��ڴ��
	PF_USER = 2 // �û��ڴ��
};

#define PG_P_1 1 // ҳ�����ҳĿ¼���������λ
#define PG_P_0 0 // ҳ�����ҳĿ¼���������λ
#define PG_RW_R 0 // r/w����λ������ִ��
#define PG_RW_W 2 // ��дִ��
#define PG_US_S 0 // u/s����λ ϵͳ��
#define PG_US_U 4 // �û���

// �����ַ����
struct virtual_addr {
	struct bitmap vaddr_bitmap; // ����ʵ�ڴ��ӳ��λͼ
	uint32_t vaddr_start; // �����ַ��ʼ��ַ
};

extern struct pool kernel_pool, user_pool;
void mem_init(void);
void* get_kernel_pages(uint32_t pg_cnt);
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void malloc_init(void);
uint32_t* pte_ptr(uint32_t vaddr);
uint32_t* pde_ptr(uint32_t vaddr);
#endif
