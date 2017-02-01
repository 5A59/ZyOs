#include "syscall.h"
#include "thread.h"

// �޲���ϵͳ����
#define _syscall0(NUMBER) ({  \
	int retval; \
	asm volatile ( \
		"int $0x80" \
		: "=a" (retval) \
		: "a" (NUMBER) \
		: "memory"  \
		);  \
	retval;  \
})

// һ������ϵͳ����
#define _syscall1(NUMBER, ARG1) ({  \
	int retval; \
	asm volatile (\
		"int $0x80" \
		: "=a" (retval) \
		: "a" (NUMBER), "b" (ARG1) \
		: "memory" \
		);  \
	retval; \
})

// 2������ϵͳ����
#define _syscall2(NUMBER, ARG1, ARG2) ({  \
	int retval; \
	asm volatile (\
		"int $0x80" \
		: "=a" (retval) \
		: "a" (NUMBER), "b" (ARG1), "c" (ARG2) \
		: "memory" \
		);  \
	retval; \
})

// 3������ϵͳ����
#define _syscall3(NUMBER, ARG1, ARG2, ARG3) ({  \
	int retval; \
	asm volatile (\
		"int $0x80" \
		: "=a" (retval) \
		: "a" (NUMBER), "b" (ARG1), "c" (ARG2), "d"(ARG3) \
		: "memory" \
		);  \
	retval; \
})

/* ���ص�ǰ����pid */
uint32_t getpid() {
	   return _syscall0(SYS_GETPID);
}

/* ��buf��count���ַ�д���ļ�������fd */
uint32_t write(int32_t fd, const void* buf, uint32_t count) {
	   return _syscall3(SYS_WRITE, fd, buf, count);
}

/* ����size�ֽڴ�С���ڴ�,�����ؽ�� */
void* malloc(uint32_t size) {
	   return (void*)_syscall1(SYS_MALLOC, size);
}

/* �ͷ�ptrָ����ڴ� */
void free(void* ptr) {
	   _syscall1(SYS_FREE, ptr);
}

/* �����ӽ���,�����ӽ���pid */
pid_t fork(void){
	   return _syscall0(SYS_FORK);
}

/* ���ļ�������fd�ж�ȡcount���ֽڵ�buf */
int32_t read(int32_t fd, void* buf, uint32_t count) {
	   return _syscall3(SYS_READ, fd, buf, count);
}

/* ���һ���ַ� */
void putchar(char char_asci) {
	   _syscall1(SYS_PUTCHAR, char_asci);
}

/* �����Ļ */
void clear(void) {
	   _syscall0(SYS_CLEAR);
}

/* ��ȡ��ǰ����Ŀ¼ */
char* getcwd(char* buf, uint32_t size) {
	   return (char*)_syscall2(SYS_GETCWD, buf, size);
}

/* ��flag��ʽ���ļ�pathname */
int32_t open(char* pathname, uint8_t flag) {
	   return _syscall2(SYS_OPEN, pathname, flag);
}

/* �ر��ļ�fd */
int32_t close(int32_t fd) {
	   return _syscall1(SYS_CLOSE, fd);
}

/* �����ļ�ƫ���� */
int32_t lseek(int32_t fd, int32_t offset, uint8_t whence) {
	   return _syscall3(SYS_LSEEK, fd, offset, whence);
}

/* ɾ���ļ�pathname */
int32_t unlink(const char* pathname) {
	   return _syscall1(SYS_UNLINK, pathname);
}

/* ����Ŀ¼pathname */
int32_t mkdir(const char* pathname) {
	   return _syscall1(SYS_MKDIR, pathname);
}

/* ��Ŀ¼name */
struct dir* opendir(const char* name) {
	   return (struct dir*)_syscall1(SYS_OPENDIR, name);
}

/* �ر�Ŀ¼dir */
int32_t closedir(struct dir* dir) {
	   return _syscall1(SYS_CLOSEDIR, dir);
}

/* ɾ��Ŀ¼pathname */
int32_t rmdir(const char* pathname) {
	   return _syscall1(SYS_RMDIR, pathname);
}

/* ��ȡĿ¼dir */
struct dir_entry* readdir(struct dir* dir) {
	   return (struct dir_entry*)_syscall1(SYS_READDIR, dir);
}

/* �ع�Ŀ¼ָ�� */
void rewinddir(struct dir* dir) {
	   _syscall1(SYS_REWINDDIR, dir);
}

/* ��ȡpath���Ե�buf�� */
int32_t stat(const char* path, struct stat* buf) {
	   return _syscall2(SYS_STAT, path, buf);
}

/* �ı乤��Ŀ¼Ϊpath */
int32_t chdir(const char* path) {
	   return _syscall1(SYS_CHDIR, path);
}

/* ��ʾ�����б� */
void ps(void) {
	   _syscall0(SYS_PS);
}
