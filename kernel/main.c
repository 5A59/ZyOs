#include "print.h"
int main(void) {
	put_str("I am a kernel \n");
	put_int(0);
	put_char('\n');
	put_int(9);
	put_char('\n');
	put_int(0x12345678);
	put_char('\n');
	put_int(0x00000000);
	while (1);
	return 0;
}
