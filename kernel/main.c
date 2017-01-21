#include "print.h"
#include "init.h"
#include "debug.h"
#include "thread.h"
#include "console.h"

void k_thread_a(void*);
void k_thread_b(void*);

int main(void) {
	put_str("I am kernel \n");
	init_all();

	thread_start("k_thread_a", 31, k_thread_a, "argA ");
	thread_start("k_thread_b", 8, k_thread_b, "argB ");

	intr_enable(); // 打开中断，时钟中断起作用
	while (1) {
		console_put_str("Main ");
	}

	return 0;
}

void k_thread_a(void* arg) {
	char* para = arg;
	while(1) {
		console_put_str(para);
	}
}

void k_thread_b(void* arg) {
	char* para = arg;
	while(1) {
		console_put_str(para);
	}
}
