#include "init.h"
#include "print.h"
#include "interrupt.h"
#include "timer.h"
#include "memory.h"
#include "thread.h"
#include "console.h"
//#include "../device/timer.h"		  // 用相对路径演示头文件包含

/*负责初始化所有模块 */
void init_all() {
   put_str("init_all\n");
   idt_init();    // 初始化中断
   mem_init(); // 初始化内存管理系统
   thread_init(); // 初始化线程相关
   timer_init();  // 初始化PIT
   console_init(); // 
}
