#include "file.h"
#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "stdio-kernel.h"
#include "memory.h"
#include "debug.h"
#include "interrupt.h"
#include "string.h"
#include "thread.h"
#include "global.h"

#define DEFAULT_SECS 1

// �ļ���
struct file file_table[MAX_FILE_OPEN];

// ���ļ���file_table�л�ȡһ������λ�������±�
int32_t get_free_slot_in_global(void) {
	uint32_t fd_idx = 3;
	// ǰ3��������׼�����������
	while (fd_idx < MAX_FILE_OPEN) {
		if (file_table[fd_idx].fd_inode == NULL) {
			break;
		}
		fd_idx ++;
	}
	if (fd_idx == MAX_FILE_OPEN) {
		printk("exceed max open files \n");
		return -1;
	}
	return fd_idx;
}

//��ȫ���������±갲װ�����̻��߳��Լ����ļ�����������fd_table��
int32_t pcb_fd_install(int32_t globa_fd_idx) {
	struct task_struct* cur = running_thread();
	uint8_t local_fd_idx = 3;
	while (local_fd_idx < MAX_FILES_OPEN_PER_PROC) {
		if (cur->fd_table[local_fd_idx] == -1) { // -1 ��ʾ����
			cur->fd_table[local_fd_idx] = globa_fd_idx;
			break;
		}
		local_fd_idx ++;
	}
	if (local_fd_idx == MAX_FILES_OPEN_PER_PROC) {
		printk("exceed max open files_per_proc\n");
		return -1;
	}
	return local_fd_idx;
}

// ����һ��inode ����inode���
int32_t inode_bitmap_alloc(struct partition* part) {
	int32_t bit_idx = bitmap_scan(&part->inode_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->inode_bitmap, bit_idx, 1);
	return bit_idx;
}

// ����һ���������ص�ַ
int32_t block_bitmap_alloc(struct partition* part) {
	int32_t bit_idx = bitmap_scan(&part->block_bitmap, 1);
	if (bit_idx == -1) {
		return -1;
	}
	bitmap_set(&part->block_bitmap, bit_idx, 1);
	return (part->sb->data_start_lba + bit_idx);
}

// ���ڴ���bitmap��bit_idxλ���ڵ�512�ֽ�ͬ����Ӳ��
void bitmap_sync(struct partition* part, uint32_t bit_idx, uint8_t btmp_type) {
	uint32_t off_sec = bit_idx / 4096;
	uint32_t off_size = off_sec * BLOCK_SIZE;
	uint32_t sec_lba;
	uint8_t* bitmap_off;

	switch(btmp_type) {
		case INODE_BITMAP:
			sec_lba = part->sb->inode_bitmap_lba + off_sec;
			bitmap_off = part->inode_bitmap.bits + off_size;
			break;
		case BLOCK_BITMAP:
			sec_lba = part->sb->block_bitmap_lba + off_sec;
			bitmap_off = part->block_bitmap.bits + off_size;
			break;
	}
	ide_write(part->my_disk, sec_lba, bitmap_off, 1);
}

// �����ļ�
int32_t file_create(struct dir* parent_dir, char* filename, uint8_t flag) {
	void* io_buf = sys_malloc(1024);
	if (io_buf == NULL) {
		printk("in file_create: sys_malloc for io_buf failed \n");
		return -1;
	}

	uint8_t rollback_step = 0;

	int32_t inode_no = inode_bitmap_alloc(cur_part);
	if (inode_no == -1) {
		printk("in file_create: allocate inode failed\n");
		return -1;
	}

	struct inode* new_file_inode = (struct inode*)sys_malloc(sizeof(struct inode));
	if (new_file_inode == NULL) {
		printk("file_create: sys_malloc for inode failed");
		rollback_step = 1;
		goto rollback;
	}

	inode_init(inode_no, new_file_inode);

	int fd_idx = get_free_slot_in_global();
	if (fd_idx == -1) {
		printk("exceed max open files \n");
		rollback_step = 2;
		goto rollback;
	}

	file_table[fd_idx].fd_inode = new_file_inode;
	file_table[fd_idx].fd_pos = 0;
	file_table[fd_idx].fd_flag = flag;
	file_table[fd_idx].fd_inode->write_deny = false;

	struct dir_entry new_dir_entry;
	memset(&new_dir_entry, 0, sizeof(struct dir_entry));

	create_dir_entry(filename, inode_no, FT_REGULAR, &new_dir_entry);

	// ͬ���ڴ����ݵ�Ӳ��

	// 1.��parent_dir�°�װĿ¼��new_dir_entry
	if (!sync_dir_entry(parent_dir, &new_dir_entry, io_buf)) {
		printk("sync dir_entry to disk failed\n");
		rollback_step = 3;
		goto rollback;
	}

	memset(io_buf, 0, 1024);
	// 2.����Ŀ¼inodeͬ����Ӳ��
	inode_sync(cur_part, parent_dir->inode, io_buf);
	memset(io_buf, 0, 1024);
	// 3.���´������ļ�inodeͬ����Ӳ��
	inode_sync(cur_part, new_file_inode, io_buf);
	// 4.��inode_bitmapλͼͬ����Ӳ��
	bitmap_sync(cur_part, inode_no, INODE_BITMAP);
	// 5.���������ļ�inode�ڵ���ӵ�open_inodes������
	list_push(&cur_part->open_inodes, &new_file_inode->inode_tag);
	new_file_inode->i_open_cnts = 1;

	sys_free(io_buf);
	return pcb_fd_install(fd_idx);

rollback:
	switch(rollback_step) {
		case 3:
			memset(&file_table[fd_idx], 0, sizeof(struct file));
		case 2:
			sys_free(new_file_inode);
		case 1:
			bitmap_set(&cur_part->inode_bitmap, inode_no, 0);
			break;
	}
	sys_free(io_buf);
	return -1;
}

// �򿪱��Ϊinode_no��inode��Ӧ���ļ� �����ļ�������
int32_t file_open(uint32_t inode_no, uint8_t flag) {
	int fd_idx = get_free_slot_in_global();
	if (fd_idx == -1) {
		printk("exceed max open files\n");
		return -1;
	}

	file_table[fd_idx].fd_inode = inode_open(cur_part, inode_no); // inode_open ��ʼ���ǰ�Ӳ���ϵ�inode���ݶ����ڴ棬�޸Ķ�Ӧ������
	file_table[fd_idx].fd_pos = 0; // �ļ���ָ��ָ��ͷ
	file_table[fd_idx].fd_flag = flag;
	bool* write_deny = &file_table[fd_idx].fd_inode->write_deny;

	if (flag & O_WRONLY || flag & O_RDWR) {
		// ����д�ļ����ж��Ƿ���������������д
		enum intr_status old_status = intr_disable();
		if (!(*write_deny)) {
			*write_deny = true;
			intr_set_status(old_status);
		}else {
			intr_set_status(old_status);
			printk("file can not be write now , try again later \n");
			return -1;
		}
	}
	return pcb_fd_install(fd_idx);
}

// �ر��ļ�
int32_t file_close(struct file* file) {
	if (file == NULL) {
		return -1;
	}
	file->fd_inode->write_deny = false;
	inode_close(file->fd_inode);
	file->fd_inode = NULL;
	return 0;
}

/* ��buf�е�count���ֽ�д��file,�ɹ��򷵻�д����ֽ���,ʧ���򷵻�-1 */
int32_t file_write(struct file* file, const void* buf, uint32_t count) {
   if ((file->fd_inode->i_size + count) > (BLOCK_SIZE * 140))	{   // �ļ�Ŀǰ���ֻ֧��512*140=71680�ֽ�
      printk("exceed max file_size 71680 bytes, write file failed\n");
      return -1;
   }
   uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
   if (io_buf == NULL) {
      printk("file_write: sys_malloc for io_buf failed\n");
      return -1;
   }
   uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);	  // ������¼�ļ����еĿ��ַ
   if (all_blocks == NULL) {
      printk("file_write: sys_malloc for all_blocks failed\n");
      return -1;
   }

   const uint8_t* src = buf;	    // ��srcָ��buf�д�д������� 
   uint32_t bytes_written = 0;	    // ������¼��д�����ݴ�С
   uint32_t size_left = count;	    // ������¼δд�����ݴ�С
   int32_t block_lba = -1;	    // ���ַ
   uint32_t block_bitmap_idx = 0;   // ������¼block��Ӧ��block_bitmap�е�����,��Ϊ��������bitmap_sync
   uint32_t sec_idx;	      // ������������
   uint32_t sec_lba;	      // ������ַ
   uint32_t sec_off_bytes;    // �������ֽ�ƫ����
   uint32_t sec_left_bytes;   // ������ʣ���ֽ���
   uint32_t chunk_size;	      // ÿ��д��Ӳ�̵����ݿ��С
   int32_t indirect_block_table;      // ������ȡһ����ӱ��ַ
   uint32_t block_idx;		      // ������

   /* �ж��ļ��Ƿ��ǵ�һ��д,�����,��Ϊ�����һ���� */
   if (file->fd_inode->i_sectors[0] == 0) {
      block_lba = block_bitmap_alloc(cur_part);
      if (block_lba == -1) {
	      printk("file_write: block_bitmap_alloc failed\n");
	      return -1;
      }
      file->fd_inode->i_sectors[0] = block_lba;

      /* ÿ����һ����ͽ�λͼͬ����Ӳ�� */
      block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
      ASSERT(block_bitmap_idx != 0);
      bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
   }

   /* д��count���ֽ�ǰ,���ļ��Ѿ�ռ�õĿ��� */
   uint32_t file_has_used_blocks = file->fd_inode->i_size / BLOCK_SIZE + 1;

   /* �洢count�ֽں���ļ���ռ�õĿ��� */
   uint32_t file_will_use_blocks = (file->fd_inode->i_size + count) / BLOCK_SIZE + 1;
   ASSERT(file_will_use_blocks <= 140);

   /* ͨ���������ж��Ƿ���Ҫ��������,������Ϊ0,��ʾԭ�������� */
   uint32_t add_blocks = file_will_use_blocks - file_has_used_blocks;

/* ��ʼ���ļ����п��ַ�ռ���all_blocks,(ϵͳ�п��С����������С)
 * ���涼ͳһ��all_blocks�л�ȡд��������ַ */
   if (add_blocks == 0) { 
   /* ��ͬһ������д������,���漰������������ */
      if (file_has_used_blocks <= 12 ) {	// �ļ�����������12��֮��
		 block_idx = file_has_used_blocks - 1;  // ָ�����һ���������ݵ�����
		 all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
      } else { 
      /* δд��������֮ǰ�Ѿ�ռ���˼�ӿ�,��Ҫ����ӿ��ַ������ */
		 ASSERT(file->fd_inode->i_sectors[12] != 0);
         indirect_block_table = file->fd_inode->i_sectors[12];
		 ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
      }
   } else {
   /* ��������,���漰���������������Ƿ����һ����ӿ��,����Ҫ������������� */
   /* ��һ�����:12��ֱ�ӿ鹻��*/
      if (file_will_use_blocks <= 12 ) {
      /* �Ƚ���ʣ��ռ�Ŀɼ����õ�������ַд��all_blocks */
		 block_idx = file_has_used_blocks - 1;
		 ASSERT(file->fd_inode->i_sectors[block_idx] != 0);
		 all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

		  /* �ٽ�δ��Ҫ�õ���������ú�д��all_blocks */
		 block_idx = file_has_used_blocks;      // ָ���һ��Ҫ�����������
		 while (block_idx < file_will_use_blocks) {
			block_lba = block_bitmap_alloc(cur_part);
			if (block_lba == -1) {
			   printk("file_write: block_bitmap_alloc for situation 1 failed\n");
			   return -1;
			}

		  /* д�ļ�ʱ,��Ӧ�ô��ڿ�δʹ�õ��Ѿ��������������,���ļ�ɾ��ʱ,�ͻ�ѿ��ַ��0 */
			ASSERT(file->fd_inode->i_sectors[block_idx] == 0);     // ȷ����δ����������ַ
			file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;

			/* ÿ����һ����ͽ�λͼͬ����Ӳ�� */
			block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

			block_idx++;   // ��һ�������������
		 }
      } else if (file_has_used_blocks <= 12 && file_will_use_blocks > 12) { 
	 /* �ڶ������: ��������12��ֱ�ӿ���,�����ݽ�ʹ�ü�ӿ�*/

		  /* �Ƚ���ʣ��ռ�Ŀɼ����õ�������ַ�ռ���all_blocks */
		 block_idx = file_has_used_blocks - 1;      // ָ����������ڵ����һ������
		 all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];

		 /* ����һ����ӿ�� */
		 block_lba = block_bitmap_alloc(cur_part);
		 if (block_lba == -1) {
			printk("file_write: block_bitmap_alloc for situation 2 failed\n");
			return -1;
		 }

		 ASSERT(file->fd_inode->i_sectors[12] == 0);  // ȷ��һ����ӿ��δ����
		 /* ����һ����ӿ������� */
		 indirect_block_table = file->fd_inode->i_sectors[12] = block_lba;

		 block_idx = file_has_used_blocks;	// ��һ��δʹ�õĿ�,�����ļ����һ���Ѿ�ʹ�õ�ֱ�ӿ����һ��
		 while (block_idx < file_will_use_blocks) {
			block_lba = block_bitmap_alloc(cur_part);
			if (block_lba == -1) {
			   printk("file_write: block_bitmap_alloc for situation 2 failed\n");
			   return -1;
			}

			if (block_idx < 12) {      // �´�����0~11��ֱ�Ӵ���all_blocks����
			   ASSERT(file->fd_inode->i_sectors[block_idx] == 0);      // ȷ����δ����������ַ
			   file->fd_inode->i_sectors[block_idx] = all_blocks[block_idx] = block_lba;
			} else {     // ��ӿ�ֻд�뵽all_block������,��ȫ��������ɺ�һ����ͬ����Ӳ��
			   all_blocks[block_idx] = block_lba;
			}

			/* ÿ����һ����ͽ�λͼͬ����Ӳ�� */
			block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);

			block_idx++;   // ��һ��������
		 }
		 ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);      // ͬ��һ����ӿ��Ӳ��
      } else if (file_has_used_blocks > 12) {
		 /* ���������:������ռ�ݼ�ӿ�*/
		 ASSERT(file->fd_inode->i_sectors[12] != 0); // �Ѿ��߱���һ����ӿ��
		 indirect_block_table = file->fd_inode->i_sectors[12];	 // ��ȡһ����ӱ��ַ

		 /* ��ʹ�õļ�ӿ�Ҳ��������all_blocks,���뵥����¼ */
		 ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1); // ��ȡ���м�ӿ��ַ

		 block_idx = file_has_used_blocks;	  // ��һ��δʹ�õļ�ӿ�,���Ѿ�ʹ�õļ�ӿ����һ��
		 while (block_idx < file_will_use_blocks) {
			block_lba = block_bitmap_alloc(cur_part);
			if (block_lba == -1) {
			   printk("file_write: block_bitmap_alloc for situation 3 failed\n");
			   return -1;
			}
			all_blocks[block_idx++] = block_lba;

			/* ÿ����һ����ͽ�λͼͬ����Ӳ�� */
			block_bitmap_idx = block_lba - cur_part->sb->data_start_lba;
			bitmap_sync(cur_part, block_bitmap_idx, BLOCK_BITMAP);
		 }
		 ide_write(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);   // ͬ��һ����ӿ��Ӳ��
      } 
   }

   bool first_write_block = true;      // ����ʣ��ռ��������ʶ
   /* ���ַ�Ѿ��ռ���all_blocks��,���濪ʼд���� */
   file->fd_pos = file->fd_inode->i_size - 1;   // ��fd_posΪ�ļ���С-1,������д����ʱ��ʱ����
   while (bytes_written < count) {      // ֱ��д����������
      memset(io_buf, 0, BLOCK_SIZE);
      sec_idx = file->fd_inode->i_size / BLOCK_SIZE;
      sec_lba = all_blocks[sec_idx];
      sec_off_bytes = file->fd_inode->i_size % BLOCK_SIZE;
      sec_left_bytes = BLOCK_SIZE - sec_off_bytes;

      /* �жϴ˴�д��Ӳ�̵����ݴ�С */
      chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;
      if (first_write_block) {
		 ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
		 first_write_block = false;
      }
      memcpy(io_buf + sec_off_bytes, src, chunk_size);
      ide_write(cur_part->my_disk, sec_lba, io_buf, 1);
      printk("file write at lba 0x%x\n", sec_lba);    //����,��ɺ�ȥ��

      src += chunk_size;   // ��ָ�����Ƶ��¸�������
      file->fd_inode->i_size += chunk_size;  // �����ļ���С
      file->fd_pos += chunk_size;   
      bytes_written += chunk_size;
      size_left -= chunk_size;
   }
   inode_sync(cur_part, file->fd_inode, io_buf);
   sys_free(all_blocks);
   sys_free(io_buf);
   return bytes_written;
}

/* ���ļ�file�ж�ȡcount���ֽ�д��buf, ���ض������ֽ���,�����ļ�β�򷵻�-1 */
int32_t file_read(struct file* file, void* buf, uint32_t count) {
   uint8_t* buf_dst = (uint8_t*)buf;
   uint32_t size = count, size_left = size;

   /* ��Ҫ��ȡ���ֽ����������ļ��ɶ���ʣ����, ����ʣ������Ϊ����ȡ���ֽ��� */
   if ((file->fd_pos + count) > file->fd_inode->i_size)	{
      size = file->fd_inode->i_size - file->fd_pos;
      size_left = size;
      if (size == 0) {	   // �����ļ�β�򷵻�-1
	 return -1;
      }
   }

   uint8_t* io_buf = sys_malloc(BLOCK_SIZE);
   if (io_buf == NULL) {
      printk("file_read: sys_malloc for io_buf failed\n");
   }
   uint32_t* all_blocks = (uint32_t*)sys_malloc(BLOCK_SIZE + 48);	  // ������¼�ļ����еĿ��ַ
   if (all_blocks == NULL) {
      printk("file_read: sys_malloc for all_blocks failed\n");
      return -1;
   }

   uint32_t block_read_start_idx = file->fd_pos / BLOCK_SIZE;		       // �������ڿ����ʼ��ַ
   uint32_t block_read_end_idx = (file->fd_pos + size) / BLOCK_SIZE;	       // �������ڿ����ֹ��ַ
   uint32_t read_blocks = block_read_start_idx - block_read_end_idx;	       // ������Ϊ0,��ʾ������ͬһ����
   ASSERT(block_read_start_idx < 139 && block_read_end_idx < 139);

   int32_t indirect_block_table;       // ������ȡһ����ӱ��ַ
   uint32_t block_idx;		       // ��ȡ�����Ŀ��ַ 

/* ���¿�ʼ����all_blocks���ַ����,ר�Ŵ洢�õ��Ŀ��ַ(�������п��Сͬ������С) */
   if (read_blocks == 0) {       // ��ͬһ�����ڶ�����,���漰����������ȡ
      ASSERT(block_read_end_idx == block_read_start_idx);
      if (block_read_end_idx < 12 ) {	   // ������������12��ֱ�ӿ�֮��
	 block_idx = block_read_end_idx;
	 all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
      } else {		// ���õ���һ����ӿ��,��Ҫ�����м�ӿ������
	 indirect_block_table = file->fd_inode->i_sectors[12];
	 ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);
      }
   } else {      // ��Ҫ�������
   /* ��һ�����: ��ʼ�����ֹ������ֱ�ӿ�*/
      if (block_read_end_idx < 12 ) {	  // ���ݽ������ڵĿ�����ֱ�ӿ�
	 block_idx = block_read_start_idx; 
	 while (block_idx <= block_read_end_idx) {
	    all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx]; 
	    block_idx++;
	 }
      } else if (block_read_start_idx < 12 && block_read_end_idx >= 12) {
   /* �ڶ������: ����������ݿ�Խֱ�ӿ�ͼ�ӿ�����*/
       /* �Ƚ�ֱ�ӿ��ַд��all_blocks */
	 block_idx = block_read_start_idx;
	 while (block_idx < 12) {
	    all_blocks[block_idx] = file->fd_inode->i_sectors[block_idx];
	    block_idx++;
	 }
	 ASSERT(file->fd_inode->i_sectors[12] != 0);	    // ȷ���Ѿ�������һ����ӿ��

      /* �ٽ���ӿ��ַд��all_blocks */
	 indirect_block_table = file->fd_inode->i_sectors[12];
	 ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);	      // ��һ����ӿ�������д�뵽��13�����λ��֮��
      } else {	
   /* ���������: �����ڼ�ӿ���*/
	 ASSERT(file->fd_inode->i_sectors[12] != 0);	    // ȷ���Ѿ�������һ����ӿ��
	 indirect_block_table = file->fd_inode->i_sectors[12];	      // ��ȡһ����ӱ��ַ
	 ide_read(cur_part->my_disk, indirect_block_table, all_blocks + 12, 1);	      // ��һ����ӿ�������д�뵽��13�����λ��֮��
      } 
   }

   /* �õ��Ŀ��ַ�Ѿ��ռ���all_blocks��,���濪ʼ������ */
   uint32_t sec_idx, sec_lba, sec_off_bytes, sec_left_bytes, chunk_size;
   uint32_t bytes_read = 0;
   while (bytes_read < size) {	      // ֱ������Ϊֹ
      sec_idx = file->fd_pos / BLOCK_SIZE;
      sec_lba = all_blocks[sec_idx];
      sec_off_bytes = file->fd_pos % BLOCK_SIZE;
      sec_left_bytes = BLOCK_SIZE - sec_off_bytes;
      chunk_size = size_left < sec_left_bytes ? size_left : sec_left_bytes;	     // ����������ݴ�С

      memset(io_buf, 0, BLOCK_SIZE);
      ide_read(cur_part->my_disk, sec_lba, io_buf, 1);
      memcpy(buf_dst, io_buf + sec_off_bytes, chunk_size);

      buf_dst += chunk_size;
      file->fd_pos += chunk_size;
      bytes_read += chunk_size;
      size_left -= chunk_size;
   }
   sys_free(all_blocks);
   sys_free(io_buf);
   return bytes_read;
}
