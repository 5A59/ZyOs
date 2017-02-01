#include "print.h"
#include "init.h"
#include "thread.h"
#include "interrupt.h"
#include "console.h"
#include "process.h"
#include "syscall-init.h"
#include "syscall.h"
#include "stdio.h"
#include "memory.h"
#include "dir.h"
#include "fs.h"
#include "assert.h"
#include "shell.h"
#include "file.h"

void init(void);

int main(void) {
   put_str("I am kernel\n");
   init_all();
   //cls_screen();
   console_put_str("################################\n");
   console_put_str("#                              #\n");
   console_put_str("#   input 'y' to start shell   #\n");
   console_put_str("#                              #\n");
   console_put_str("################################\n");
   char pos[1] = {0};
   while (read(stdin_no, pos, 1) != -1) {
	   console_put_char(pos[0]);
	   if (pos[0] == 'y') {
		   cls_screen();
		   process_execute(init, "init");
		   break;
	   }
   }
   while(1);
   return 0;
}

/* init进程 */
void init(void) {
   uint32_t ret_pid = fork();
   if(ret_pid) {
	   while(1);
   } else {
	   my_shell();
   }
   panic("init: should not be here");
}
