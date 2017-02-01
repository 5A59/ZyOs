#ifndef __FS_FS_H
#define __FS_FS_H

#include "stdint.h"

#define MAX_FILES_PER_PART 4096 // ÿ������֧�ֵ���󴴽��ļ���
#define BITS_PER_SECTOR 4096 // ÿ������λ��
#define SECTOR_SIZE 512 // �����ֽڴ�С
#define BLOCK_SIZE SECTOR_SIZE // ���ֽڴ�С

#define MAX_PATH_LEN 512 // ·����󳤶�

enum file_types {
	FT_UNKNOWN, // ��֧�ֵ�����
	FT_REGULAR, // ��ͨ�ļ�
	FT_DIRECTORY // Ŀ¼
};

// ���ļ�ѡ��
enum oflags {
	O_RDONLY, // ֻ��
	O_WRONLY, // ֻд
	O_RDWR, // ��д
	O_CREAT = 4 // ����
};

// �ļ���дλ��ƫ��
enum whence {
	SEEK_SET = 1,
	SEEK_CUR,
	SEEK_END
};

// ������¼�����ļ��������Ѿ��ҵ����ϼ�·��
struct path_search_record {
	char searched_path[MAX_PATH_LEN]; // ���ҹ����еĸ�·��
	struct dir* parent_dir; // ֱ�Ӹ�Ŀ¼
	enum file_types file_type;
};

// �ļ�����
struct stat {
	uint32_t st_ino; // inode���
	uint32_t st_size; // �ļ���С
	enum file_types st_filetype; // �ļ�����
};

extern struct partition* cur_part;
void filesys_init(void);
int32_t path_depth_cnt(char* pathname);
int32_t sys_open(const char* pathname, uint8_t flags);
int32_t sys_close(int32_t fd);
int32_t sys_write(int32_t fd, const void* buf, uint32_t count);
int32_t sys_lseek(int32_t fd, int32_t offset, uint8_t whence);
int32_t sys_ulink(const char* pathname);
int32_t sys_mkdir(const char* pathname);
struct dir* sys_opendir(const char* pathname);
int32_t sys_closedir(struct dir* dir);
struct dir_entry* sys_readddir(struct dir* dir);
void sys_rewinddir(struct dir* dir);
int32_t sys_rmdir(const char* pathname);
char* sys_getcwd(char* buf, uint32_t size);
int32_t sys_chdir(const char* path);
int32_t sys_stat(const char* path, struct stat* buf);
#endif
