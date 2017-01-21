[bits 32]
section .next
global switch_to
switch_to:
  push esi
  push edi 
  push ebx
  push ebp

  mov eax, [esp + 20] ; 得到栈中的参数cur cur = [esp+20]
  mov [eax], esp ; 得到栈顶指针esp.task_struct的self_kstack字段

  ;;;;;;;;;;;;;;;;;;;以上备份当前线程环境 下面回复下一个线程的环境;;;;;;;;;;;;;;;;;;;
  mov eax, [esp + 24] ; 得到栈中的参数next, 
  mov esp, [eax] 
  pop ebp
  pop ebx
  pop edi
  pop esi
  ret
