#ifndef __FS_SUPER_BLOCK_H
#define __FS_SUPER_BLOCK_H
#include "stdint.h"

struct super_block {
	uint32_t magic; // ������ʶ�ļ�ϵͳ
	uint32_t sec_cnt; // �������ܹ���������
	uint32_t inode_cnt; // �������ܹ���inode����
	uint32_t part_lba_base; // ����������ʼlba��ַ

	uint32_t block_bitmap_lba; // ��λͼ������ʼ������ַ
	uint32_t block_bitmap_sects; // ����λͼ����ռ�õ���������

	uint32_t inode_bitmap_lba; // inodeλͼ��ʼ����lba��ַ
	uint32_t inode_bitmap_sects; // inodeλͼռ�õ���������

	uint32_t inode_table_lba; // inode����ʼ����lba��ַ
	uint32_t inode_table_sects; // inode��ռ�õ���������

	uint32_t data_start_lba; // ��������ʼ�ĵ�һ��������
	uint32_t root_inode_no; // ��Ŀ¼���ڵ�inode
	uint32_t dir_entry_size; // Ŀ¼���С

	uint8_t pad[460]; // �չ�512�ֽ�1������С
} __attribute__ ((packed));
#endif
