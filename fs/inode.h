#ifndef __FS_INODE_H
#define __FS_INODE_H
#include "stdint.h"
#include "list.h"
#include "ide.h"

struct inode {
	uint32_t i_no; // inode编号

	// inode是文件时: 表示文件大小
	// inode是目录时: 表示目录下所有目录项大小之和
	uint32_t i_size;

	uint32_t i_open_cnts; // 记录此文件被打开的次数
	bool write_deny; // 写文件前检查此标识，防止并行写文件

	// 0-11 是直接块 12用来存储一级间接块指针
	uint32_t i_sectors[13];
	// 保存已经打开的inode列表,用于缓存
	struct list_elem inode_tag;
};

struct inode* inode_open(struct partition* part, uint32_t inode_no);
void inode_sync(struct partition* part, struct inode* inode, void* io_buf);
void inode_init(uint32_t inode_no, struct inode* new_inode);
void inode_close(struct inode* inode);
void inode_release(struct partition* part, uint32_t inode_no);
void inode_delete(struct partition* part, uint32_t inode_no, void* io_buf);
#endif
