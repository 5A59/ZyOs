#include "memory.h"
#include "stdint.h"
#include "print.h"
#include "bitmap.h"
#include "global.h"
#include "string.h"
#include "debug.h"
#include "sync.h"
#include "interrupt.h"

#define PG_SIZE 4096 // ҳ�ߴ� 4KB

/**
 * 0xc009f000 ���ں����߳�ջ��
 * 0xc009e000 ���ں����߳�pcb
 * һ��ҳ���С��λͼ�ɱ�ʾ128M�ڴ�: һ��ҳ��4KB, 
 * 4KB = 2 ^ 12 �ֽ� = 2 ^ (12 + 3) bits
 * 128MB = 4Kb * 4KB
 * λͼλ����0xc009a000,����֧�֣���ҳ���С 512M
 */
#define MEM_BITMAP_BASE 0xc009a000

#define PDE_IDX(addr) ((addr & 0xffc00000) >> 22) 
#define PTE_IDX(addr) ((addr & 0x003ff000) >> 12)

// ����Ͷ�1M�ڴ�,ʹ�����ַ���߼�������
#define K_HEAP_START 0xc0100000

// �ڴ�ؽṹ,��������ʵ�����ڹ����ں��ڴ�غ��û��ڴ��
struct pool {
	struct bitmap pool_bitmap; // λͼ�ṹ�����ڹ��������ڴ�
	uint32_t phy_addr_start; // �����ڴ���ʼ��ַ
	uint32_t pool_size; // �ڴ���ֽ�����
	struct lock lock; // �����ڴ�ʱ����
};

// �ڴ�ֿ���Ϣ
struct arena {
	struct mem_block_desc* desc; // arena������mem_block_desc
	// largeΪtrueʱ��cnt��ʾ����ҳ������falseʱcnt��ʾ���е�mem_block����
	uint32_t cnt; 
	bool large;
};

struct mem_block_desc k_block_descs[DESC_CNT]; // �ں��ڴ������������
struct pool kernel_pool; // �ں��ڴ��
struct pool user_pool; // �û��ڴ��
struct virtual_addr kernel_vaddr; // ���ں˷��������ַ

// ��pf��ʾ�������ڴ��������pg_cnt������ҳ���ɹ���������ҳ����ʼ��ַ��ʧ�ܷ���null
static void* vaddr_get(enum pool_flags pf, uint32_t pg_cnt) {
	int vaddr_start = 0;
	int bit_idx_start = -1;
	uint32_t cnt = 0;

	if (pf == PF_KERNEL) {
		// ��ȡ����λ����ʼ��ַ
		bit_idx_start = bitmap_scan(&kernel_vaddr.vaddr_bitmap, pg_cnt);
		if (bit_idx_start == -1) {
			return NULL;
		}
		while (cnt < pg_cnt) {
			// ʹ�õ�λͼ�ã�
			bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 1);
		}
		// λͼ��һλ����һҳ���ڴ�
		vaddr_start = kernel_vaddr.vaddr_start + bit_idx_start * PG_SIZE;
	}else { // �û��ڴ��
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

// �õ������ַ��pte(ҳ��)ָ��
uint32_t* pte_ptr(uint32_t vaddr) {
	uint32_t* pte = (uint32_t*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + PTE_IDX(vaddr) * 4);
	return pte;
}

// �õ������ַ��Ӧ��pdeָ��
uint32_t* pde_ptr(uint32_t vaddr) {
	uint32_t* pde = (uint32_t*)((0xfffff000) + PDE_IDX(vaddr) * 4);
	return pde;
}

static void* palloc(struct pool* m_pool) {
	// ��bitmap���ҵ�һ�����е�λ��
	int bit_idx = bitmap_scan(&m_pool->pool_bitmap, 1);
	if (bit_idx == -1) {
		return NULL;
	}
	bitmap_set(&m_pool->pool_bitmap, bit_idx, 1);
	uint32_t page_phyaddr = ((bit_idx * PG_SIZE) + m_pool->phy_addr_start);
	return (void*)page_phyaddr;
}

// ҳ������������ַ�������ַ��ӳ��
static void page_table_add(void* _vaddr, void* _page_phyaddr) {
	uint32_t vaddr = (uint32_t)_vaddr;
	uint32_t page_phyaddr = (uint32_t)_page_phyaddr;
	uint32_t* pde = pde_ptr(vaddr);
	uint32_t* pte = pte_ptr(vaddr);

	if (*pde & 0x00000001) { // �ж�ҳĿ¼���Ƿ����, ��0λΪP
		ASSERT(!(*pte & 0x00000001));

		if (!(*pte & 0x00000001)) { // �ж�ҳ�����Ƿ���� �����ڴ���ҳ��
			*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // US = 1 RW = 1 P = 1
		}else {
			PANIC("pte repeat");
			*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // US = 1 RW = 1 P = 1
		}
	}else { // ҳĿ¼�����
		uint32_t pde_phyaddr = (uint32_t) palloc(&kernel_pool); // ����ҳĿ¼���ڴ�
		*pde = (pde_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // ����ҳĿ¼��

		memset((void*)((int)pte & 0xfffff000), 0, PG_SIZE);
		ASSERT(!(*pte & 0x00000001));
		*pte = (page_phyaddr | PG_US_U | PG_RW_W | PG_P_1); // ����ҳ����
	}
}

// ����pg_cnt��ҳ�ռ䣬�ɹ�������ʼ�����ַ
void* malloc_page(enum pool_flags pf, uint32_t pg_cnt) {
	ASSERT(pg_cnt > 0 && pg_cnt < 3840);
	/**
	 * malloc_page ����������
	 * 1.ͨ��vaddr_get�������ڴ�������������ַ
	 * 2.palloc�������ڴ������������ҳ
	 * 3.page_tablea_add�������ַ�������ַ��ҳ����ӳ��
	 */
	void* vaddr_start = vaddr_get(pf, pg_cnt);
	if (vaddr_start == NULL) {
		return NULL;
	}

	uint32_t vaddr = (uint32_t)vaddr_start;
	uint32_t cnt = pg_cnt;
	struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;

	while (cnt-- > 0) {
		void* page_phyaddr = palloc(mem_pool); // ���������ڴ�ҳ
		if (page_phyaddr == NULL) {
			return NULL;
		}
		page_table_add((void*)vaddr, page_phyaddr); // ҳ������ӳ��
		vaddr += PG_SIZE; // ָ����һ������ҳ
	}
	return vaddr_start;
}

// ���ں������ڴ���������ڴ� malloc_page �ķ�װ
void* get_kernel_pages(uint32_t pg_cnt) {
	void* vaddr = malloc_page(PF_KERNEL, pg_cnt);
	if (vaddr != NULL) {
		memset(vaddr, 0, pg_cnt * PG_SIZE);
	}
	return vaddr;
}

// ���û��ؼ�����4k�ڴ� ���������ַ
void* get_user_pages(uint32_t pg_cnt) {
	lock_acquire(&user_pool.lock);
	void* vaddr = malloc_page(PF_USER, pg_cnt);
	memset(vaddr, 0, pg_cnt * PG_SIZE);
	lock_release(&user_pool.lock);
	return vaddr;
}

// ����ַvaddr�ͳ��е������ַ����
void* get_a_page(enum pool_flags pf, uint32_t vaddr) {
	struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
	lock_acquire(&mem_pool->lock);

	struct task_struct* cur = running_thread();
	int32_t bit_idx = -1;

	// ��ǰ���û����������û��ڴ棬�޸��û������Լ��������ַλͼ
	if (cur->pgdir != NULL && pf == PF_USER) {
		bit_idx = (vaddr - cur->userprog_vaddr.vaddr_start) / PG_SIZE;
		ASSERT(bit_idx > 0);
		bitmap_set(&cur->userprog_vaddr.vaddr_bitmap, bit_idx, 1);
	}else if (cur->pgdir == NULL && pf == PF_KERNEL) {
		// �ں��߳������ں��ڴ棬�޸�kernel_vaddr
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

// ��װ1ҳ��С��vaddr,ר�����forkʱ�����ַλͼ������������
void* get_a_page_without_opvaddrbitmap(enum pool_flags pf, uint32_t vaddr) {
	struct pool* mem_pool = pf & PF_KERNEL ? &kernel_pool : &user_pool;
	lock_acquire(&mem_pool->lock);
	void* page_phyaddr = palloc(mem_pool);
	if (page_phyaddr == NULL) {
		lock_release(&mem_pool->lock);
		return NULL;
	}
	page_table_add((void*)vaddr, page_phyaddr);
	lock_release(&mem_pool->lock);
	return (void*)vaddr;
}

// �õ������ַӳ�䵽�������ַ
uint32_t addr_v2p(uint32_t vaddr) {
	uint32_t* pte = pte_ptr(vaddr);
	// pte ��ҳ�����ڵ�����ҳ���ַ
	// ȥ�����12λҳ�������� + �����ַvaddr�ĵ�12λ
	return ((*pte & 0xfffff000) + (vaddr & 0x00000fff));
}

// ��ʼ���ڴ��
// ��Ҫ��Ϊ kernel_pool user_pool kernel_vaddr ��ʼ��
static void mem_pool_init(uint32_t all_mem) {
	put_str("mem_pool_init start \n");
	uint32_t page_table_size = PG_SIZE * 256; // ҳ���С����ҳ��ҳĿ¼�� + �ڣ��͵�768��ҳĿ¼��ָ��ͬһ��ҳ�� + ��769-1022��ҳĿ¼��һ��ָ��254��ҳ��һ��256��ҳ��
	uint32_t used_mem = page_table_size + 0x100000; // 0x100000 �ǵͶˣ����ڴ�  ��ʹ�õ��ڴ�
	uint32_t free_mem = all_mem - used_mem;
	uint16_t all_free_pages = free_mem / PG_SIZE;
	uint16_t kernel_free_pages = all_free_pages / 2;
	uint16_t user_free_pages = all_free_pages - kernel_free_pages;

	uint32_t kbm_length = kernel_free_pages / 8; // kernel bitmap �ĳ��ȣ�λͼ�е�һλ��ʾһҳ��λͼ��ʾ���ֽ�Ϊ��λ
	uint32_t ubm_length = user_free_pages / 8; // user bitmap �ĳ���

	uint32_t kp_start = used_mem; // �ں��ڴ����ʼ��ַ
	uint32_t up_start = kp_start + kernel_free_pages * PG_SIZE; // �û��ڴ����ʼ��ַ

	kernel_pool.phy_addr_start = kp_start;
	user_pool.phy_addr_start = up_start;

	kernel_pool.pool_size = kernel_free_pages * PG_SIZE; // �ֽ�Ϊ��λ
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

	// λͼ����
	bitmap_init(&kernel_pool.pool_bitmap);
	bitmap_init(&user_pool.pool_bitmap);

	lock_init(&kernel_pool.lock);
	lock_init(&user_pool.lock);

	// �����ַ��ֵ
	kernel_vaddr.vaddr_bitmap.btmp_bytes_len = kbm_length; // ά���ں˵������ַ ���ں��ڴ�ش�Сһ��
	kernel_vaddr.vaddr_bitmap.bits = (void*)(MEM_BITMAP_BASE + kbm_length + ubm_length);

	kernel_vaddr.vaddr_start = K_HEAP_START;
	bitmap_init(&kernel_vaddr.vaddr_bitmap);
	put_str("mem_pool_init done \n");
}

// ����arena�е�idx���ڴ���ַ
static struct mem_block* arena2block(struct arena* a, uint32_t idx) {
	return (struct mem_block*)((uint32_t)a + sizeof(struct arena) + idx * a->desc->block_size);
}

// �����ڴ��b���ڵ�arena��ַ
static struct arena* block2arena(struct mem_block* b) {
	return (struct arena*)((uint32_t)b & 0xfffff000);
}

// �ڶ�������size�ֽ��ڴ�
void* sys_malloc(uint32_t size) {
	enum pool_flags PF;
	struct pool* mem_pool;
	uint32_t pool_size;
	struct mem_block_desc* descs;
	struct task_struct* cur_thread = running_thread();

	if (cur_thread->pgdir == NULL) {
		PF = PF_KERNEL;
		pool_size = kernel_pool.pool_size;
		mem_pool = &kernel_pool;
		descs = k_block_descs;
	}else {
		PF = PF_USER;
		pool_size = user_pool.pool_size;
		mem_pool = &user_pool;
		descs = cur_thread->u_block_desc;
	}

	// �����ڴ�̫�� ����NULL
	if (!(size > 0 && size < pool_size)) {
		return NULL;
	}
	struct arena* a;
	struct mem_block* b;
	lock_acquire(&mem_pool->lock);

	// ����1024����ҳ��
	if (size > 1024) {
		uint32_t page_cnt = DIV_ROUND_UP(size + sizeof(struct arena), PG_SIZE);
		a = malloc_page(PF, page_cnt);
		if (a != NULL) {
			memset(a, 0, page_cnt * PG_SIZE);
			a->desc = NULL;
			a->cnt = page_cnt;
			a->large = true;
			lock_release(&mem_pool->lock);
			return (void*)(a + 1); // ���arena��С�������ڴ�
		}else {
			lock_release(&mem_pool->lock);
			return NULL;
		}
	}else { // С��1024 ��mem_block_desc������
		uint8_t desc_idx;
		for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx ++) {
			if (size <= descs[desc_idx].block_size) {
				break;
			}
		}
		// mem_block_desc��free_list��û�п��õ�mem_block,�����µ�arena�ṩmem_block
		if (list_empty(&descs[desc_idx].free_list)) {
			a = malloc_page(PF, 1); // ����1ҳ��
			if (a == NULL) {
				lock_release(&mem_pool->lock);
				return NULL;
			}
			memset(a, 0, PG_SIZE);

			a->desc = &descs[desc_idx];
			a->large = false;
			a->cnt = descs[desc_idx].blocks_per_arena;
			uint32_t block_idx;

			enum intr_status old_status = intr_disable();
			for (block_idx = 0; block_idx < descs[desc_idx].blocks_per_arena; block_idx ++) {
				b = arena2block(a, block_idx);
				ASSERT(!elem_find(&a->desc->free_list, &b->free_elem));
				list_append(&a->desc->free_list, &b->free_elem);
			}
			intr_set_status(old_status);
		}

		// ��ʼ�����ڴ��
		b = elem2entry(struct mem_block, free_elem, list_pop(&(descs[desc_idx].free_list)));
		memset(b, 0, descs[desc_idx].block_size);

		a = block2arena(b);
		a->cnt --; // �����ڴ���1
		lock_release(&mem_pool->lock);
		return (void*)b;
	}
}

// �����ַ���յ������ڴ��
void pfree(uint32_t pg_phy_addr) {
	struct pool* mem_pool;
	uint32_t bit_idx = 0;
	if (pg_phy_addr >= user_pool.phy_addr_start) { // �û������ڴ��
		mem_pool = &user_pool;
		bit_idx = (pg_phy_addr - user_pool.phy_addr_start) / PG_SIZE;
	}else {
		mem_pool = &kernel_pool;
		bit_idx = (pg_phy_addr - kernel_pool.phy_addr_start) / PG_SIZE;
	}
	bitmap_set(&mem_pool->pool_bitmap, bit_idx, 0);
}

// ȥ��ҳ���������ַvaddr��ӳ�䣬ֻ��Ҫȥ��vaddr��Ӧ��pte
static void page_table_pte_remove(uint32_t vaddr) {
	uint32_t* pte = pte_ptr(vaddr);
	*pte &= ~PG_P_1; // ҳ����pte��Pλ��0
	asm volatile ("invlpg %0" : : "m" (vaddr) : "memory"); // ����tlb
}

// �������ַ�����ͷ���_vaddr��ʵ������pg_cnt������ҳ��ַ
static void vaddr_remove(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
	uint32_t bit_idx_start = 0;
	uint32_t vaddr = (uint32_t)_vaddr;
	uint32_t cnt = 0;

	if (pf == PF_KERNEL) {
		bit_idx_start = (vaddr - kernel_vaddr.vaddr_start) / PG_SIZE;
		while (cnt < pg_cnt) {
			bitmap_set(&kernel_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
		}
	}else {
		struct task_struct* cur_thread = running_thread();
		bit_idx_start = (vaddr - cur_thread->userprog_vaddr.vaddr_start) / PG_SIZE;
		while (cnt < pg_cnt) {
			bitmap_set(&cur_thread->userprog_vaddr.vaddr_bitmap, bit_idx_start + cnt++, 0);
		}
	}
}

// �ͷ��������ַvaddrΪ��ʼ��cnt������ҳ��
void mfree_page(enum pool_flags pf, void* _vaddr, uint32_t pg_cnt) {
	uint32_t pg_phy_addr;
	uint32_t vaddr = (int32_t)_vaddr;
	uint32_t page_cnt = 0;
	ASSERT(pg_cnt >= 1 && vaddr % PG_SIZE == 0);
	pg_phy_addr = addr_v2p(vaddr); // ��ȡ�����ַ��Ӧ�������ַ

	// ȷ���ͷŵ������ַ�ڵͶ�1M+1k��СҳĿ¼+1k��Сҳ���ַ��Χ��
	ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= 0x102000);

	if (pg_phy_addr >= user_pool.phy_addr_start) { // λ���û��ڴ��
		vaddr -= PG_SIZE;
		while (page_cnt < pg_cnt) {
			vaddr += PG_SIZE;
			pg_phy_addr = addr_v2p(vaddr);
			// ȷ�������ַ�����û������ڴ��
			ASSERT((pg_phy_addr % PG_SIZE) == 0 && pg_phy_addr >= user_pool.phy_addr_start);
			// ��Ӧ������ҳ�򻹵��ڴ��
			pfree(pg_phy_addr);
			// ��ҳ��������������ַ���ڵ�ҳ����pte
			page_table_pte_remove(vaddr);
			page_cnt ++;
		}
		// ��������ַλͼ����Ӧ��λ
		vaddr_remove(pf, _vaddr, pg_cnt);
	}else {
		vaddr -= PG_SIZE;
		while (page_cnt < pg_cnt) {
			vaddr += PG_SIZE;
			pg_phy_addr = addr_v2p(vaddr);
			// ȷ���ͷŵ������ڴ�λ���ں������ڴ��
			ASSERT((pg_phy_addr % PG_SIZE) == 0 && \
					 pg_phy_addr >= kernel_pool.phy_addr_start && \
					 pg_phy_addr < user_pool.phy_addr_start);

			pfree(pg_phy_addr);
			page_table_pte_remove(vaddr);
			page_cnt ++;
		}
		vaddr_remove(pf, _vaddr, pg_cnt);
	}
}

// �����ڴ�ptr
void sys_free(void* ptr) {
	ASSERT(ptr != NULL);
	if (ptr != NULL) {
		enum pool_flags PF;
		struct pool* mem_pool;

		if (running_thread()->pgdir == NULL) { // ��ǰ���̣߳�λ���ں�
			ASSERT((uint32_t)ptr >= K_HEAP_START);
			PF = PF_KERNEL;
			mem_pool = &kernel_pool;
		}else {
			PF = PF_USER;
			mem_pool = &user_pool;
		}

		lock_acquire(&mem_pool->lock);
		struct mem_block* b = ptr;
		struct arena* a = block2arena(b);
		ASSERT(a->large == 0 || a->large == 1); // ???
		if (a->desc == NULL && a->large == true) { // ����1024�ڴ棬�������ҳ��
			mfree_page(PF, a, a->cnt);
		}else { // С��1024��С�ڴ�
			// ��ӵ������ڴ���б���
			list_append(&a->desc->free_list, &b->free_elem);
			// ��ǰarena�����ڴ�鶼�ǿ��е�
			if (++a->cnt == a->desc->blocks_per_arena) {
				uint32_t block_idx;
				for (block_idx = 0; block_idx < a->desc->blocks_per_arena; block_idx ++) {
					struct mem_block* b = arena2block(a, block_idx);
					ASSERT(elem_find(&a->desc->free_list, &b->free_elem));
					list_remove(&b->free_elem);
				}
				mfree_page(PF, a, 1);
			}
		}
		lock_release(&mem_pool->lock);
	}
}

// ��ʼ��arean�ڴ�ֿ�
void block_desc_init(struct mem_block_desc* desc_array) {
	uint16_t desc_idx, block_size = 16;
	for (desc_idx = 0; desc_idx < DESC_CNT; desc_idx ++) {
		// �ڴ���С
		desc_array[desc_idx].block_size = block_size;
		// �ڴ������
		desc_array[desc_idx].blocks_per_arena = (PG_SIZE - sizeof(struct arena)) / block_size;
		list_init(&desc_array[desc_idx].free_list);
		// ��һ���ڴ���С
		block_size *= 2;
	}
}

void mem_init() {
	put_str("mem_init start \n");
	uint32_t mem_bytes_total = (*(uint32_t*)(0xb00)); // loader.S �л�ȡ�������ڴ��С���ڵ�ַ0xb00
	mem_pool_init(mem_bytes_total);
	block_desc_init(k_block_descs);
	put_str("mem_init done \n");
}
