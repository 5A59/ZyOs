#include "syscall.h"

// 无参数系统调用
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

// 一个参数系统调用
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

// 2个参数系统调用
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

// 3个参数系统调用
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

uint32_t getpid() {
	return _syscall0(SYS_GETPID);
}

uint32_t write(char* str) {
	return _syscall1(SYS_WRITE, str);
}
