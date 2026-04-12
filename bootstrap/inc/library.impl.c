
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

void fn__hexout_(int x) {
	printf("D %08x\n", x);
}

void fn_write(int fd, t$str s, unsigned off, unsigned len) {
	write(fd, (void*) (s + off), len);
}

void fn_writes(int fd, t$str s) {
	write(fd, (void*)s, strlen((void*) s));
}
void fn_writex(int fd, int n) {
	char tmp[64];
	sprintf(tmp, "0x%x", n);
	write(fd, tmp, strlen(tmp));
}
void fn_writei(int fd, int n) {
	char tmp[64];
	sprintf(tmp, "%d", n);
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
	os_argc = argc - 1;
	os_argv = argv + 1;
	int x = fn_start();
#if !QUIET
	printf("X %08x\n", x);
#endif
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

int fn_fd_open(t$str s) {
	return open((void*)s, O_RDONLY, 0644);
}
int fn_fd_create(t$str s) {
	return open((void*)s, O_RDWR | O_CREAT | O_TRUNC, 0644);
}
void fn_fd_close(int fd) {
	close(fd);
}
int fn_fd_set_pos(int fd, unsigned pos) {
	if (lseek(fd, pos, SEEK_SET) == ((off_t) -1)) {
		return -1;
	} else {
		return 0;
	}
}
unsigned fn_fd_get_pos(int fd) {
	return lseek(fd, 0, SEEK_CUR);
}

void fn_abort(void) {
	abort();
}

static uint32_t heap_alloc_count = 0;
static uint32_t heap_alloc_bytes = 0;

void* fn_sys_alloc(t$i32 n) {
	heap_alloc_count++;
	heap_alloc_bytes += n;
	return calloc(1, n);
}
t$i32 fn_sys_get_alloc_count(void) {
	return heap_alloc_count;
}
t$i32 fn_sys_get_alloc_bytes(void) {
	return heap_alloc_bytes;
}
void* fn___new(t$i32 n) {
	return fn_sys_alloc(n);
}
