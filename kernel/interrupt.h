#ifndef __KERNEL_INTERRUPT_H
#define __KERNEL_INTERRUPT_H
#include "stdint.h"
typedef void* intr_handler;
void idt_init(void);

/**
 * 定义中断的两种状态
 */
enum intr_status {
	INTR_OFF, // 中断关闭
	INTR_ON   // 中断打开
};

enum intr_status intr_get_status(void);
enum intr_status intr_set_status(enum intr_status);
enum intr_status intr_enable(void);
enum intr_status intr_disable(void);
void register_handler(uint8_t vector_no, intr_handler function);
#endif
