#ifndef __FS_INODE_H
#define __FS_INODE_H
#include "stdint.h"
#include "list.h"
#include "ide.h"

struct inode {
	uint32_t i_no; // inode���

	// inode���ļ�ʱ: ��ʾ�ļ���С
	// inode��Ŀ¼ʱ: ��ʾĿ¼������Ŀ¼���С֮��
	uint32_t i_size;

	uint32_t i_open_cnts; // ��¼���ļ����򿪵Ĵ���
	bool write_deny; // д�ļ�ǰ���˱�ʶ����ֹ����д�ļ�

	// 0-11 ��ֱ�ӿ� 12�����洢һ����ӿ�ָ��
	uint32_t i_sectors[13];
	// �����Ѿ��򿪵�inode�б�,���ڻ���
	struct list_elem inode_tag;
};

struct inode* inode_open(struct partition* part, uint32_t inode_no);
void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
void inode_init(uint32_t inode_no, struct inode* new_inode);
void inode_close(struct inode* inode);
void inode_release(struct partition* part, uint32_t inode_no);
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf);
#endif
