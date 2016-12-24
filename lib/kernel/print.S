; 定义显存段选择子
TI_GDT equ 0
RPL0 equ 0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

section .data
put_int_buffer dq 0 ; 定义8字节的缓冲区用于数字到字符的转换

[bits 32]
section .text
; ------ put_str ------
global put_str
put_str:
  ; 备份寄存器 
  push ebx
  push ecx
  xor ecx, ecx
  mov ebx, [esp + 12]
.goon:
  mov cl, [ebx]
  cmp cl, 0 ; 处理到字符串尾部，结束
  jz .str_over
  push ecx ; 为put_char准备参数
  call put_char
  add esp, 4 ; 回收参数占用的栈空间
  inc ebx ; 指向下一个字符
  jmp .goon
.str_over:
  pop ecx
  pop ebx
  ret

; ------ put_char ------
; 把一个字符写到光标所在处
global put_char ; put_char 导出为全局符号，外部文件可见
put_char:
  pushad ; 备份32位寄存器环境,8个32位寄存器都备份了
  mov ax, SELECTOR_VIDEO 
  mov gs, ax ; gs中为视频段选择子

  ;--- 获取当前光标位置 ---
  ; 访问CRT controller寄存器的时候需要先往0x03d4端口写入寄存器索引,再从0x03d5端口读取数据
  ; 获取高8位
  mov dx, 0x03d4 ; 索引寄存器
  mov al, 0x0e ;
  out dx, al
  mov dx, 0x03d5 ; 通过读写0x03d5获取设置光标位置
  in al, dx ; in 指令目的操作数必须是 al(源操作寄存器是8位) 或者 ax(源操作寄存器是16位)
  mov ah, al

  ; 获取低8位
  mov dx, 0x03d4
  mov al, 0x0f
  out dx, al
  mov dx, 0x03d5
  in al, dx

  ; 光标存入bx
  ; 16位实模式下基址寄存器必需是bx或bp,变址必需是si或di
  mov bx, ax
  mov ecx, [esp + 36] ; 获取栈中待打印的字符 36 = pushad压入的8个寄存器(4*8=32字节) + 主调函数的返回地址4字节
  cmp cl, 0xd ; 判断是否是回车
  jz .is_carriage_return
  cmp cl, 0xa ; 判断是否是换行
  jz .is_line_feed

  cmp cl, 0x8 ; 判断是否是退格 backspace
  jz .is_backspace
  jmp .put_other ; 其他字符直接输出

; 处理退格
.is_backspace:
  ; bx 存的是光标位置,指的是第几个字符,从0开始
  ; 字符在显存中是2个字节 高字节是属性，低字节是ascii字符
  dec bx ; bx 减一指向前一个字符
  shl bx, 1 ; bx 左移一位乘2 
  mov byte [gs:bx], 0x20 ; 待删除的字符置为空格或0
  inc bx ; bx 加一,指向属性位置
  mov byte [gs:bx], 0x07
  shr bx, 1 ; bx 复位
  jmp .set_cursor

; 输出其他字符
.put_other:
  shl bx, 1 ; 光标乘2
  mov [gs:bx], cl ; 字符
  inc bx ; 指向字符属性
  mov byte [gs:bx], 0x07
  shr bx, 1 ; 光标复位
  inc bx ; 指向下一个光标位置
  cmp bx, 2000
  jl .set_cursor ; 光标值小于2000,说明没有写满屏幕，设置新的光标值就可以
  ; 如果超出屏幕字符数2000就换行，换行里面有处理滚屏

.is_line_feed: ; 换行符 \n
.is_carriage_return: ; 回车符 \r
  xor dx, dx
  mov ax, bx
  mov si, 80
  div si
  sub bx, dx ; dx中存的是余数 dx - dx % 8 得到的就是行首位置
  ; 以上处理\r

.is_carriage_return_end:  ; 处理 \n
  add bx, 80 ; 上一行行首位置加上80就是下一行行首，实现了回车换行
  cmp bx, 2000
.is_line_feed_end:
  jl .set_cursor

; 屏幕是80*25
; 屏幕行范围是0-24,将1-24搬运到0-23，24行用空格填充实现滚屏
.roll_screen: ; 滚屏
  cld ; 清楚方向位
  mov eax, 960 ; 一共有2000-80=1920个字符,一共1920*2=3840字节,一次搬4字节，一共3840/4=960次
  mov esi, 0xb80a0 ; 第一行行首
  mov edi, 0xb8000 ; 第0行行首
  rep movsd ; 复制数据
  ; 最后一行空白填充
  mov ebx, 3840 ; 最后一行首字符 1920 * 2
  mov ecx, 80

.cls:
  mov word [gs:ebx], 0x0720 ; 黑底白字的空格
  add ebx, 2
  loop .cls
  mov bx, 1920 ; 光标重置为最后一行行首

.set_cursor: ; 设置光标为dx值
  ; 先设置高8位
  mov dx, 0x03d4
  mov al, 0x0e
  out dx, al
  mov dx, 0x03d5
  mov al, bh
  out dx, al

  ; 设置低8位
  mov dx, 0x03d4
  mov al, 0x0f
  out dx, al
  mov dx, 0x03d5
  mov al, bl
  out dx, al

.put_char_done:
  popad ; 恢复寄存器
  ret

; ------ put_int ------
global put_int
put_int:
  pushad
  mov ebp, esp
  mov eax, [ebp + 4 * 9] ; call 返回地址4字节加上pushad的8个寄存器4字节
  mov edx, eax
  mov edi, 7 ; 指定在put_int_buffer中初始的偏移量
  mov ecx, 8 ; 32位数字中，16进制数字的位数是8个
  mov ebx, put_int_buffer

; 将32位数字按照16进制的形式从低位到高位逐个处理,一共处理8个16进制数字
.16based_4bits:
  and edx, 0x0000000F ; 解析16进制数字的每一位，and操作之后，edx只有低4位有效
  cmp edx, 9 ; 数字0-9 a-f处理方式不同
  jg .is_A2F
  add edx, '0' ; ascii码8位有效，add操作以后edx低8位有效
  jmp .store

.is_A2F:
  sub edx, 10 ; A-F减去10得到的差，加上字符A的ascii码，就是A-F对应的ascii
  add edx, 'A'

.store:
  ; dl 中是数字对应的字符的ascii码
  mov [ebx + edi], dl
  dec edi
  shr eax, 4
  mov edx, eax
  loop .16based_4bits

.ready_to_print:
  inc edi ; 此时edi 减成了-1,加一成为0
.skip_prefix_0:
  cmp edi, 8 ; 已经比较第9个字符，表示待打印的字符串全为0
  je .full0
; 找出连续的0字符,edi作为非0的最高位字符的偏移
.go_on_skip:
  mov cl, [put_int_buffer + edi]
  inc edi
  cmp cl, '0'
  je .skip_prefix_0
  dec edi
  jmp .put_each_num

.full0:
  mov cl, '0'
.put_each_num:
  push ecx
  call put_char
  add esp, 4
  inc edi ; edi 指向下一个字符
  mov cl, [put_int_buffer + edi]
  cmp edi, 8
  jl .put_each_num
  popad
  ret
