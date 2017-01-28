#ifndef __KERNEL_MEMORY_H
#define __KERNEL_MEMORY_H
#include "stdint.h"
#include "bitmap.h"
#include "list.h"

// 内存池标记
enum pool_flags {
	PF_KERNEL = 1, // 内核内存池
	PF_USER = 2 // 用户内存池
};

#define PG_P_1 1 // 页表项或页目录项存在属性位
#define PG_P_0 0 // 页表项或页目录项存在属性位
#define PG_RW_R 0 // r/w属性位　读／执行
#define PG_RW_W 2 // 读写执行
#define PG_US_S 0 // u/s属性位 系统级
#define PG_US_U 4 // 用户级

// 虚拟地址管理
struct virtual_addr {
	struct bitmap vaddr_bitmap; // 与真实内存的映射位图
	uint32_t vaddr_start; // 虚拟地址起始地址
};

// 内存块，用于存储小内存块
struct mem_block {
	struct list_elem free_elem;
};

// 内存块描述符
struct mem_block_desc {
	uint32_t block_size; // 内存块大小
	uint32_t blocks_per_arena; // 可容纳mem_block的数量
	struct list free_list; // 目前可用的mem_block链表
};

#define DESC_CNT 7 // 内存块描述符个数

extern struct pool kernel_pool, user_pool;
void mem_init(void);
void* get_kernel_pages(uint32_t pg_cnt);
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt);
void malloc_init(void);
uint32_t* pte_ptr(uint32_t vaddr);
uint32_t* pde_ptr(uint32_t vaddr);
uint32_t addr_v2p(uint32_t vaddr);
void* get_a_page(enum pool_flags pf, uint32_t vaddr);
void* get_user_pages(uint32_t pg_cnt);
void block_desc_init(struct mem_block_desc* desc_array);
void* sys_malloc(uint32_t size);
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt);
void pfree(uint32_t pg_phy_addr);
void sys_free(void* ptr);
#endif
