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

; ----- 加载kernel -----
  mov eax, KERNEL_START_SECTOR ; kernel.bin所在的扇区号
  mov ebx, KERNEL_BIN_BASE_ADDR ; 从磁盘读出，写入到ebx指定的地址
  mov ecx, 200 ; 读入扇区数

  call rd_disk_m_32

  ; 创建页目录及页表并初始化页内存位图
  call setup_page
  ; 将描述符表地址及偏移量写入内存gdt_ptr,一会用新地址重新加载
  sgdt [gdt_ptr] ; sgdt 指令用于设置gdtr寄存器

  ; 修改显存段的段描述符段基址
  mov ebx, [gdt_ptr + 2]
  or dword [ebx + 0x18 + 4], 0xc0000000 ; 显存段是第三个段描述符,每个描述符8字节,所以加3*8 = 0x18 , 段描述符的最高4字节的最高位是段基址的31-24位
  ; gdt基址加上0xc0000000成为内核所在的高地址 
  add dword [gdt_ptr + 2], 0xc0000000 

  add esp, 0xc0000000  ; 栈指针映射到内核地址

  ; 页目录地址赋值给cr3
  mov eax, PAGE_DIR_TABLE_POS
  mov cr3, eax

  ; 启动分页机制
  ; 打开cr0的pg位(第31位)
  mov eax, cr0
  or eax, 0x80000000 
  mov cr0, eax

  ; 开启分页后，用gdt的新地址重新加载
  lgdt [gdt_ptr]

  jmp SELECTOR_CODE: enter_kernel ; 强制刷新流水线 更新gdt

enter_kernel:
  call kernel_init
  mov esp, 0xc009f000
  jmp KERNEL_ENTRY_POINT ; 

; ----- 将kernel.bin中的segment拷贝到编译的地址 -----
kernel_init:
  xor eax, eax
  xor ebx, ebx ; ebx记录程序头表地址
  xor ecx, ecx ; cx记录程序头表中的program header数量
  xor edx, edx ; dx 记录program header尺寸,即 e_phentsize

  mov dx, [KERNEL_BIN_BASE_ADDR + 42] ; 偏移文件42字节处的属性是e_phentsize,表示program header大小
  mov ebx, [KERNEL_BIN_BASE_ADDR + 28] ; 偏移文件开始部分28字节的地方是e_phoff，表示1个program header在文件中的偏移量
  add ebx, KERNEL_BIN_BASE_ADDR
  mov cx, [KERNEL_BIN_BASE_ADDR + 44] ; 偏移文件开始部分44字节的地方是e_phnum,表示有几个program header
.each_segment:
  cmp byte [ebx + 0], PT_NULL ; p_type == PT_NULL，说明program header未使用
  je .PTNULL

  ; 为函数memcpy压入参数，参数从右往左依次压入
  push dword [ebx + 16]
  mov eax, [ebx + 4] ; 据程序头偏移量为4字节的是p_offset
  add eax, KERNEL_BIN_BASE_ADDR ; 加上kernel.bin被加载到的物理地址,eax为该段的物理地址
  push eax
  push dword [ebx + 8]
  call mem_cpy
  add esp, 12 ; 清理栈中压入的参数
.PTNULL:
  add ebx, edx ; edx 是program header大小,此时ebx 指向下一个program header
  loop .each_segment
  ret

; ----- 逐字节拷贝 mem_cpy(dst, src, size) -----
mem_cpy:
  cld ; 用于指定 edi esi增长的方向，即增加或者减少，对应的有std，movsb每次执行后会自动给edi esi增加拷贝的字节数
  push ebp
  mov ebp, esp
  push ecx ; 备份ecx 外层还有用
  mov edi, [ebp + 8] ; dst
  mov esi, [ebp + 12] ; src
  mov ecx, [ebp + 16] ; size
  rep movsb ; 逐字节拷贝指令 rep 重复执行ecx中的次数

  ; 恢复环境
  pop ecx
  pop ebp
  ret


; ----- 创建页目录及页表 ----- ;
setup_page:
  ; 页目录空间清零
  mov ecx, 4096
  mov esi, 0
.clear_page_dir:
  mov byte [PAGE_DIR_TABLE_POS + esi], 0
  inc esi
  loop .clear_page_dir

; 开始创建页目录项(PDE)
.create_pde:
  mov eax, PAGE_DIR_TABLE_POS
  add eax, 0x1000 ; 此时eax为第一个页表的位置和属性
  mov ebx, eax

  ; 页目录项0和0xc00都存为第一个页表的地址
  ; 一个页表表示4MB的内存，0xc03fffff以下的地址和0x003fffff以下的地址都指向相同的页表，为地址映射到内核地址做准备
  or eax, PG_US_U | PG_RW_W | PG_P ; 页目录项属性RW和P为１,US为１，表示用户属性，所有特权级别都可以访问
  mov [PAGE_DIR_TABLE_POS + 0x0], eax
  mov [PAGE_DIR_TABLE_POS + 0xc00], eax
  sub eax, 0x1000
  mov [PAGE_DIR_TABLE_POS + 4092], eax ; 最后一个目录项指向页目录表自己的地址

; 创建页表项(PTE)
  mov ecx, 256 ; 1M / 每页4k = 256
  mov esi, 0
  mov edx, PG_US_U | PG_RW_W | PG_P
.create_pte:
  mov [ebx + esi * 4], edx ; ebx 在上面赋值为 0x101000，第一个页表地址
  add edx, 4096
  inc esi
  loop .create_pte

; 创建内核其他页表的PDE
  mov eax, PAGE_DIR_TABLE_POS
  add eax, 0x2000
  or eax, PG_US_U | PG_RW_W | PG_P
  mov ebx, PAGE_DIR_TABLE_POS
  mov ecx, 254
  mov esi, 769
.create_kernel_pde:
  mov [ebx + esi * 4], eax
  inc esi
  add eax, 0x1000
  loop .create_kernel_pde
  ret

; ----- 读取硬盘n个扇区 -----
rd_disk_m_32:
  mov esi, eax
  mov di, cx
  ;---读写硬盘---
  ; 1. 设置要读取的扇区数
  mov dx, 0x1f2
  mov al, cl
  out dx, al

  mov eax, esi ; 回复ax

  ; 2. 将LBA地址存入-x1f3-0x1f6
  ; LBA地址7-0位写入端口0x1f3
  mov dx, 0x1f3
  out dx, al

  ; LBA 8-15写入0x1f4
  mov cl, 8
  shr eax, cl
  mov dx, 0x1f4
  out dx, al

  ; LBA 16-32写入0x1f5
  shr eax, cl
  mov dx, 0x1f5
  out dx, al

  shr eax, cl
  and al, 0x0f ; lba 24-27
  or al, 0xe0 ; 4-7位为1110，表示lba模式
  mov dx, 0x1f6
  out dx, al

  ; 3. 向0x1f7写入读命令 0x20
  mov dx, 0x1f7
  mov al, 0x20
  out dx, al

  ; 4. 检测硬盘状态
  .not_ready: ; 测试0x1f7的bsy位
    nop
	in al, dx
	and al, 0x88
	cmp al, 0x08
	jnz .not_ready

  ; 5. 从0x1f0端口读取数据
    mov ax, di
	mov dx, 256
	mul dx
	mov cx, ax
	mov dx, 0x1f0
  .go_on_read:
    in ax, dx
	mov [ebx], ax
	add ebx, 2

	loop .go_on_read
	ret
