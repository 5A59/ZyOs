%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR

; 构建 gdt 和其内部描述符
; gdt 是一片内存区域 每隔8字节是一个表项 即段描述符
GDT_BASE: dd 0x00000000 ; dd define double-word 定义双字变量 即4字节数据
          dd 0x00000000

CODE_DESC: dd 0x0000FFFF
           dd DESC_CODE_HIGH4

DATA_STACK_DESC: dd 0x0000FFFF
                 dd DESC_DATA_HIGH4

VIDEO_DESC: dd 0x80000007
            dd DESC_VIDEO_HIGH4

GDT_SIZE equ $ - GDT_BASE
GDT_LIMIT equ GDT_SIZE - 1
times 60 dq 0 ; 此处预留60个描述符的 slot

SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002 << 3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

; total mem bytes 用于保存内存容量 字节为单位
; 4 个段描述符定义 + 60个预留的slot = 512 = 0x200 
; loader.bin 加载地址是0x900
; totalmembytes的地址就是0xb00
total_mem_bytes dd 0

; 定义 gdt 的指针 前两字节是 gdt 界限 后4字节是gdt起始地址
gdt_ptr dw GDT_LIMIT
      dd GDT_BASE
;loadermsg db '2 loader in real.' ; db 声明字节

; 对齐 没什么用
; ards_buf 用于缓存0x15中断返回的ards结构
ards_buf times 244 db 0
ards_nr dw 0 ; 记录ards的数量

loader_start:

; int 15h eax = 0000E820h, edx=0x534d4150 获取内存布局
  xor ebx, ebx ; ebx置零
  mov edx, 0x534d4150
  mov di, ards_buf
.e820_mem_get_loop: ; 循环获取ards内存范围描述结构
  mov eax, 0x0000e820 ; 执行int 15h 以后，eax的值变成0x534d4150('SMAP'的ascii,所以要重新赋值)
  mov ecx, 20 ; ards的大小是20字节
  int 0x15 ; 调用中断
  jc .e820_failed_so_try_e801 ; cf位是1就是有错误发生,尝试e801h子功能
  add di, cx ; 指向缓存区的下一个位置
  inc word [ards_nr] ; 记录ards数量 inc指令是循环累加
  cmp ebx, 0 ; ebx为0且cf不为1,说明ards全部返回
  jnz .e820_mem_get_loop

  mov cx, [ards_nr]
  mov ebx, ards_buf ; 
  xor edx, edx ; 清零

.find_max_mem_area: ; 最大的内存块一定是可以使用的
  mov eax, [ebx] ; base addr
  add eax, [ebx+8] ; length
  add ebx, 20 ; 指向下一个adrs
  cmp edx, eax ; edx 是最大内存容量
  jge .next_ards ; edx > eax
  mov edx, eax ; edx < eax -> edx=eax
.next_ards:
  loop .find_max_mem_area
  jmp .mem_get_ok

.e820_failed_so_try_e801:
  mov ax, 0xe801
  int 0x15
  jc .e801_failed_so_try88
  mov cx, 0x400
  mul cx
  shl edx, 16
  and eax, 0x0000FFFF
  or edx, eax
  add edx, 0x100000
  mov esi, edx ; 低15mb存入esi备份

  ; 计算16mb以上的
  xor eax, eax
  mov ax, bx
  mov ecx, 0x10000
  mul ecx
  add esi, eax
  mov edx, esi
  jmp .mem_get_ok

.e801_failed_so_try88:
  mov ah, 0x88
  int 0x15
  jc .error_hlt
  and eax, 0x0000FFFF

  mov cx, 0x400
  mul cx
  shl edx, 16
  or edx, eax
  add edx, 0x100000

.mem_get_ok:
  mov [total_mem_bytes], edx ;存入total_mem_bytes

  ; 准备进入保护模式
  ; 1. 打开 A20
  ; 2. 加载 gdt
  ; 3. 将 cr0 的 pe 置 1

  ; 打开 A20
  in al, 0x92
  or al, 0000_0010B
  out 0x92, al

  ; 加载 GDT
  lgdt [gdt_ptr]

  ; cr0 第0位置1
  mov eax, cr0
  or eax, 0x00000001
  mov cr0, eax

  jmp dword SELECTOR_CODE: p_mode_start ; 刷新流水线，避免分之预测的影响

.error_hlt: ; 查找内存出错就挂起
  hlt

[bits 32]
p_mode_start:
  mov ax, SELECTOR_DATA
  mov ds, ax
  mov es, ax
  mov ss, ax
  mov esp, LOADER_STACK_TOP
  mov ax, SELECTOR_VIDEO
  mov gs, ax

  mov byte [gs:160], 'P'

  jmp $
