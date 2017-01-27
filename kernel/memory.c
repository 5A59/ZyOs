#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "global.h"
#include "string.h"
#include "debug.h"
#include "sync.h"

#define PG_SIZE 4096 // 页尺寸 4KB

/**
 * 0xc009f000 是内核主线程栈顶
 * 0xc009e000 是内核主线程pcb
 * 一个页框大小的位图可表示128M内存: 一个页框4KB, 
 * 4KB = 2 ^ 12 字节 = 2 ^ (12 + 3) bits
 * 128MB = 4Kb * 4KB
 * 位图位置在0xc009a000,可以支持４个页框大小 512M
 */
#define MEM_BITMAP_BASE 0xc009a000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) 
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

// 快过低端1M内存,使虚拟地址在逻辑上连续
#define K_HEAP_START 0xc0100000

// 内存池结构,生成两个实例用于管理内核内存池和用户内存池
struct pool {
	struct bitmap pool_bitmap; // 位图结构，用于管理物理内存
	uint32_t phy_addr_start; // 物理内存起始地址
	uint32_t pool_size; // 内存池字节容量
	struct lock lock; // 申请内存时互斥
};

struct pool kernel_pool, user_pool;
struct virtual_addr kernel_vaddr; // 给内核分配虚拟地址

// 在pf表示的虚拟内存池中申请pg_cnt个虚拟页　成功返回虚拟页的起始地址，失败返回null
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
	int vaddr_start = 0;
	int bit_idx_start = -1;
	uint32_t cnt = 0;

	if (pf == PF_KERNEL) {
		// 获取空闲位的起始地址
		bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
		if (bit_idx_start == -1) {
			return NULL;
		}
		while (cnt < pg_cnt) {
			// 使用的位图置１
			bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
		}
		// 位图中一位代表一页的内存
		vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
	}else { // 用户内存池
		struct task_struct* cur = running_thread();
		bit_idx_start = bitmap_scan(&cur->userprog_vaddr.vaddr_bitmap, pg_cnt);
		if (bit_idx_start == -1) {
			return NULL;
		}
		while (cnt < pg_cnt) {
			bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
		}
		vaddr_start = cur->userprog_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
		ASSERT((uint32_t)vaddr_start < (0xc0000000 - PG_SIZE));
	}
	return (void*)vaddr_start;
}

// 得到虚拟地址的pte(页表)指针
uint32_t* pte_ptr(uint32_t vaddr) {
	uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
	return pte;
}

// 得到虚拟地址对应的pde指针
uint32_t* pde_ptr(uint32_t vaddr) {
	uint32_t* pde = (uint32_t*)((0xfffff000) + PDE_IDX(vaddr) * 4);
	return pde;
}

static void* palloc(struct pool* m_pool) {
	// 在bitmap中找到一个空闲的位置
	int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
	if (bit_idx == -1) {
		return NULL;
	}
	bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
	uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
	return (void*)page_phyaddr;
}

// 页表中添加虚拟地址与物理地址的映射
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
	uint32_t vaddr = (uint32_t)_vaddr;
	uint32_t page_phyaddr = (uint32_t)_page_phyaddr;
	uint32_t* pde = pde_ptr(vaddr);
	uint32_t* pte = pte_ptr(vaddr);

	if (*pde & 0x00000001) { // 判断页目录项是否存在, 第0位为P
		ASSERT(!(*pte & 0x00000001));

		if (!(*pte & 0x00000001)) { // 判断页表项是否存在 不存在创建页表
			*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // US = 1 RW = 1 P = 1
		}else {
			PANIC("pte repeat");
			*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // US = 1 RW = 1 P = 1
		}
	}else { // 页目录项不存在
		uint32_t pde_phyaddr = (uint32_t) palloc(&kernel_pool); // 申请页目录项内存
		*pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // 创建页目录项

		memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);
		ASSERT(!(*pte & 0x00000001));
		*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // 创建页表项
	}
}

// 分配pg_cnt个页空间，成功返回起始虚拟地址
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
	ASSERT(pg_cnt > 0 && pg_cnt < 3840);
	/**
	 * malloc_page 的三个步骤
	 * 1.通过vaddr_get在虚拟内存池中申请虚拟地址
	 * 2.palloc在物理内存池中申请物理页
	 * 3.page_tablea_add将虚拟地址和物理地址在页表中映射
	 */
	void* vaddr_start = vaddr_get(pf, pg_cnt);
	if (vaddr_start == NULL) {
		return NULL;
	}

	uint32_t vaddr = (uint32_t)vaddr_start;
	uint32_t cnt = pg_cnt;
	struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

	while (cnt-- > 0) {
		void* page_phyaddr = palloc(mem_pool); // 申请物理内存页
		if (page_phyaddr == NULL) {
			return NULL;
		}
		page_table_add((void*)vaddr, page_phyaddr); // 页表中做映射
		vaddr += PG_SIZE; // 指向下一个虚拟页
	}
	return vaddr_start;
}

// 从内核物理内存池中申请内存 malloc_page 的封装
void* get_kernel_pages(uint32_t pg_cnt) {
	void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
	if (vaddr != NULL) {
		memset(vaddr, 0, pg_cnt * PG_SIZE);
	}
	return vaddr;
}

// 在用户控件申请4k内存 返回虚拟地址
void* get_user_pages(uint32_t pg_cnt) {
	lock_acquire(&user_pool.lock);
	void* vaddr = malloc_page(PF_USER, pg_cnt);
	memset(vaddr, 0, pg_cnt * PG_SIZE);
	lock_release(&user_pool.lock);
	return vaddr;
}

// 将地址vaddr和池中的物理地址关联
void* get_a_page(enum pool_flags pf, uint32_t vaddr) {
	struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
	lock_acquire(&mem_pool->lock);

	struct task_struct* cur = running_thread();
	int32_t bit_idx = -1;

	// 当前是用户进程申请用户内存，修改用户进程自己的虚拟地址位图
	if (cur->pgdir != NULL && pf == PF_USER) {
		bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
		ASSERT(bit_idx > 0);
		bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
	}else if (cur->pgdir == NULL && pf == PF_KERNEL) {
		// 内核线程申请内核内存，修改kernel_vaddr
		bit_idx = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
		ASSERT(bit_idx > 0);
		bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx, 1);
	}else {
		PANIC("get_a_page: not allow kernel alloc userpace or user alloc kernelspace by get_a_page");
	}

	void* page_phyaddr = palloc(mem_pool);
	if (page_phyaddr == NULL) {
		return NULL;
	}
	page_table_add((void*)vaddr, page_phyaddr);
	lock_release(&mem_pool->lock);
	return (void*)vaddr;
}

// 得到虚拟地址映射到的物理地址
uint32_t addr_v2p(uint32_t vaddr) {
	uint32_t* pte = pte_ptr(vaddr);
	// pte 是页表所在的物理页框地址
	// 去掉其低12位页表项属性 + 虚拟地址vaddr的低12位
	return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

// 初始化内存池
// 主要是为 kernel_pool user_pool kernel_vaddr 初始化
static void mem_pool_init(uint32_t all_mem) {
	put_str("mem_pool_init start \n");
	uint32_t page_table_size = PG_SIZE * 256; // 页表大小　１页的页目录表 + 第０和第768个页目录项指向同一个页表 + 第769-1022个页目录项一共指向254个页表，一共256个页框
	uint32_t used_mem = page_table_size + 0x100000; // 0x100000 是低端１Ｍ内存  已使用的内存
	uint32_t free_mem = all_mem - used_mem;
	uint16_t all_free_pages = free_mem / PG_SIZE;
	uint16_t kernel_free_pages = all_free_pages / 2;
	uint16_t user_free_pages = all_free_pages - kernel_free_pages;

	uint32_t kbm_length = kernel_free_pages / 8; // kernel bitmap 的长度，位图中的一位表示一页，位图表示以字节为单位
	uint32_t ubm_length = user_free_pages / 8; // user bitmap 的长度

	uint32_t kp_start = used_mem; // 内核内存池起始地址
	uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // 用户内存池起始地址

	kernel_pool.phy_addr_start = kp_start;
	user_pool.phy_addr_start = up_start;

	kernel_pool.pool_size = kernel_free_pages * PG_SIZE; // 字节为单位
	user_pool.pool_size = user_free_pages * PG_SIZE;

	kernel_pool.pool_bitmap.btmp_bytes_len = kbm_length;
	user_pool.pool_bitmap.btmp_bytes_len = ubm_length;

	kernel_pool.pool_bitmap.bits = (void*)MEM_BITMAP_BASE;
	user_pool.pool_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length);

	put_str("kernel_pool_bitmap_start : ");
	put_int((int)kernel_pool.pool_bitmap.bits);
	put_str("\n");
	put_str("kernel_pool_phy_addr_start : ");
	put_int((int)kernel_pool.phy_addr_start);
	put_str("\n");
	put_str("user_pool_bitmap_start : ");
	put_int((int)user_pool.pool_bitmap.bits);
	put_str("\n");
	put_str("user_pool_phy_addr_start : ");
	put_int((int)user_pool.phy_addr_start);
	put_str("\n");

	// 位图清零
	bitmap_init(&kernel_pool.pool_bitmap);
	bitmap_init(&user_pool.pool_bitmap);

	lock_init(&kernel_pool.lock);
	lock_init(&user_pool.lock);

	// 虚拟地址赋值
	kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length; // 维护内核的虚拟地址 和内核内存池大小一致
	kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);

	kernel_vaddr.vaddr_start = K_HEAP_START;
	bitmap_init(&kernel_vaddr.vaddr_bitmap);
	put_str("mem_pool_init done \n");
}

void mem_init() {
	put_str("mem_init start \n");
	uint32_t mem_bytes_total = (*(uint32_t*)(0xb00)); // loader.S 中获取到物理内存大小存在地址0xb00
	mem_pool_init(mem_bytes_total);
	put_str("mem_init done \n");
}
