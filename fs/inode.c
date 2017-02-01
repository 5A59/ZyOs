#include "inode.h"
#include "fs.h"
#include "file.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "interrupt.h"
#include "list.h"
#include "stdio-kernel.h"
#include "string.h"
#include "super_block.h"

// �洢inodeλ��
struct inode_position {
	bool two_sec; // inode�Ƿ������
	uint32_t sec_lba; // inode����������
	uint32_t off_size; // inode�������ڵ��ֽ�ƫ����
};

// ��ȡinodeλ��
static void inode_locate(struct partition* part, uint32_t inode_no, struct inode_position* inode_pos) {
	ASSERT(inode_no < 4096);
	uint32_t inode_table_lba = part->sb->inode_table_lba;

	uint32_t inode_size = sizeof(struct inode);
	uint32_t off_size = inode_no * inode_size; // inode�����inode_table_lba���ֽ�ƫ����
	uint32_t off_sec = off_size / 512; // inode�����inode_table_lba������ƫ����
	uint32_t off_size_in_sec = off_size % 512; // �����ҵ�inode�������е���ʼ��ַ

	// �ж�inode�Ƿ��Խ2������
	uint32_t left_in_sec = 512 - off_size_in_sec;
	if (left_in_sec < inode_size) {
		inode_pos->two_sec = true;
	}else {
		inode_pos->two_sec = false;
	}
	inode_pos->sec_lba = inode_table_lba + off_sec;
	inode_pos->off_size=  off_size_in_sec;
}

// ��inodeд�����part
void inode_sync(struct partition* part, struct inode* inode, void* io_buf) {
	uint8_t inode_no = inode->i_no;
	struct inode_position inode_pos;
	inode_locate(part, inode_no, &inode_pos);
	ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));

	struct inode pure_inode;
	memcpy(&pure_inode, inode, sizeof(struct inode));

	// ����inode��������Աֻ�����ڴ��У����ڽ�inodeͬ����Ӳ�̣����������
	pure_inode.i_open_cnts = 0;
	pure_inode.write_deny = false;
	pure_inode.inode_tag.prev = pure_inode.inode_tag.next = NULL;

	char* inode_buf = (char*)io_buf;
	if (inode_pos.two_sec) {
		// ���������������Ҫ���벢д����������
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);

		// ��inodeƴ��2��������Ӧ��λ��
		memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));

		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	}else {
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
		memcpy((inode_buf + inode_pos.off_size), &pure_inode, sizeof(struct inode));
		ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
}

// ����inode�ŷ�����Ӧ��inode
struct inode* inode_open(struct partition* part, uint32_t inode_no) {
	// �����Ѵ򿪵�inode�����в���,�ӿ��ٶ�
	struct list_elem* elem = part->open_inodes.head.next;
	struct inode* inode_found;
	while (elem != &part->open_inodes.tail) {
		inode_found = elem2entry(struct inode, inode_tag, elem);
		if (inode_found->i_no == inode_no) {
			inode_found->i_open_cnts ++;
			return inode_found;
		}
		elem = elem->next;
	}

	// ������û���ҵ�����Ӳ���ж���
	struct inode_position inode_pos;
	inode_locate(part, inode_no, &inode_pos);

	// Ϊ��ͨ��sys_malloc������inode��������������Ҫ��inode�����ں˿ռ䣬��ʱ��cur_pbc->pgdir��ΪNULL
	// ��sys_malloc��ʵ���й�
	struct task_struct* cur = running_thread();
	uint32_t* cur_pagedir_bak = cur->pgdir;
	cur->pgdir = NULL;
	inode_found = (struct inode*)sys_malloc(sizeof(struct inode));
	cur->pgdir = cur_pagedir_bak;

	char* inode_buf;
	if (inode_pos.two_sec) {
		inode_buf = (char*)sys_malloc(1024); // ����������С
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
	}else {
		inode_buf = (char*)sys_malloc(512);
		ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
	}
	memcpy(inode_found, inode_buf + inode_pos.off_size, sizeof(struct inode));

	list_push(&part->open_inodes, &inode_found->inode_tag);
	inode_found->i_open_cnts = 1;

	sys_free(inode_buf);
	return inode_found;
}

/* ��Ӳ�̷���part�ϵ�inode��� */
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf) {
   ASSERT(inode_no < 4096);
   struct inode_position inode_pos;
   inode_locate(part, inode_no, &inode_pos);     // inodeλ����Ϣ�����inode_pos
   ASSERT(inode_pos.sec_lba <= (part->start_lba + part->sec_cnt));
   
   char* inode_buf = (char*)io_buf;
   if (inode_pos.two_sec) {   // inode������,����2������
      /* ��ԭӲ���ϵ������ȶ����� */
      ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
      /* ��inode_buf��0 */
      memset((inode_buf + inode_pos.off_size), 0, sizeof(struct inode));
      /* ����0���ڴ����ݸ��Ǵ��� */
      ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 2);
   } else {    // δ������,ֻ����1�������ͺ�
      /* ��ԭӲ���ϵ������ȶ����� */
      ide_read(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
      /* ��inode_buf��0 */
      memset((inode_buf + inode_pos.off_size), 0, sizeof(struct inode));
      /* ����0���ڴ����ݸ��Ǵ��� */
      ide_write(part->my_disk, inode_pos.sec_lba, inode_buf, 1);
   }
}

/* ����inode�����ݿ��inode���� */
void inode_release(struct partition* part, uint32_t inode_no) {
   struct inode* inode_to_del = inode_open(part, inode_no);
   ASSERT(inode_to_del->i_no == inode_no);

/* 1 ����inodeռ�õ����п� */
   uint8_t block_idx = 0, block_cnt = 12;
   uint32_t block_bitmap_idx;
   uint32_t all_blocks[140] = {0};	  //12��ֱ�ӿ�+128����ӿ�

   /* a �Ƚ�ǰ12��ֱ�ӿ����all_blocks */
   while (block_idx < 12) {
      all_blocks[block_idx] = inode_to_del->i_sectors[block_idx];
      block_idx++;
   }

   /* b ���һ����ӿ�����,����128����ӿ����all_blocks[12~], ���ͷ�һ����ӿ����ռ������ */
   if (inode_to_del->i_sectors[12] != 0) {
      ide_read(part->my_disk, inode_to_del->i_sectors[12], all_blocks + 12, 1);
      block_cnt = 140;

      /* ����һ����ӿ��ռ�õ����� */
      block_bitmap_idx = inode_to_del->i_sectors[12] - part->sb->data_start_lba;
      ASSERT(block_bitmap_idx > 0);
      bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
      bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
   }
   
   /* c inode���еĿ��ַ�Ѿ��ռ���all_blocks��,����������� */
   block_idx = 0;
   while (block_idx < block_cnt) {
      if (all_blocks[block_idx] != 0) {
	 block_bitmap_idx = 0;
	 block_bitmap_idx = all_blocks[block_idx] - part->sb->data_start_lba;
	 ASSERT(block_bitmap_idx > 0);
	 bitmap_set(&part->block_bitmap, block_bitmap_idx, 0);
	 bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
      }
      block_idx++; 
   }

/*2 ���ո�inode��ռ�õ�inode */
   bitmap_set(&part->inode_bitmap, inode_no, 0);  
   bitmap_sync(cur_part, inode_no, INODE_BITMAP);

   /******     ����inode_delete�ǵ����õ�    ******
   * �˺�������inode_table�н���inode��0,
   * ��ʵ�����ǲ���Ҫ��,inode��������inodeλͼ���Ƶ�,
   * Ӳ���ϵ����ݲ���Ҫ��0,����ֱ�Ӹ���*/
   void* io_buf = sys_malloc(1024);
   inode_delete(part, inode_no, io_buf);
   sys_free(io_buf);
   /***********************************************/
    
   inode_close(inode_to_del);
}


// �ر�inode���߼���inode����
void inode_close(struct inode* inode) {
	enum intr_status old_status = intr_disable();
	inode->i_open_cnts --;
	if (inode->i_open_cnts == 0) {
		// û�н����ٴ򿪴��ļ���ȥ����inode���ͷſռ�
		list_remove(&inode->inode_tag);
		struct task_struct* cur = running_thread();
		uint32_t* cur_pagedir_bak = cur->pgdir;
		cur->pgdir = NULL;
		sys_free(inode);
		cur->pgdir = cur_pagedir_bak;
	}
	intr_set_status(old_status);
}

void inode_init(uint32_t inode_no, struct inode* new_inode) {
	new_inode->i_no = inode_no;
	new_inode->i_size = 0;
	new_inode->i_open_cnts = 0;
	new_inode->write_deny = false;

	uint8_t sec_idx = 0;
	while (sec_idx < 13) {
		new_inode->i_sectors[sec_idx] = 0;
		sec_idx ++;
	}
}
