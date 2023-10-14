
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void fn__hexout_(int x) {
	printf("D %08x\n", x);
}

void fn_writes(int fd, t$str s) {
	write(fd, (void*)s, strlen((void*) s));
}
void fn_writex(int fd, int n) {
	char tmp[64];
	sprintf(tmp, "0x%x", n);
	write(fd, tmp, strlen(tmp));
}
void fn_writec(int fd, int n) {
	t$u8 x = n;
	if (write(fd, &x, 1) != 1) {}
}

int fn_readc(int fd) {
	t$u8 x;
	if (read(fd, &x, 1) == 1) {
		return x;
	} else {
		return -1;
	}
}

int main(int argc, char** argv) {
	int x = fn_start();
	printf("X %08x\n", x);
	return 0;
}

