
#include <stdio.h>
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

static int os_argc;
static char **os_argv;

int main(int argc, char** argv) {
	os_argc = argc;
	os_argv = argv;
	int x = fn_start();
	printf("X %08x\n", x);
	return 0;
}

t$u8* fn_os_arg(int n) {
	if ((n < 0) || (n >= os_argc)) {
		return (void*) "";
	}
	return (void*) os_argv[n];
}

t$i32 fn_os_arg_count(void) {
	return os_argc;
}

void fn_os_exit(int n) {
	exit(n);
}
