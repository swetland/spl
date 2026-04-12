// Copyright 2026, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "emulator-sr32.h"

extern void *mem;

void exit_emu(int status);
void *mem_dma(uint32_t addr, uint32_t len);

void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit_emu(1);
}

#define SYS_MAX_FDS 64

static int guest_fdmap[SYS_MAX_FDS];
static uint32_t guest_argc;
static uint32_t guest_argv;

static uint32_t safer_heap = 0;

void sys_init(uint32_t _argc, uint32_t _argv, uint32_t guard_pages) {
	guest_argc = _argc;
	guest_argv = _argv;
	uint32_t n = 0;
	for (n = 0; n < SYS_MAX_FDS; n++) {
		guest_fdmap[n] = -1;
	}
	guest_fdmap[0] = 0;
	guest_fdmap[1] = 1;
	guest_fdmap[2] = 2;

	safer_heap = guard_pages;
	if (safer_heap) {
		// make heap inaccessible
		mem_protect(mem, 4 * 1024 * 1024, 4 * 1024 * 1024, 0);
	}
}

static uint32_t new_fd(int fd) {
	if (fd < 0) {
		return fd;
	}
	uint32_t n = 0;
	while (n < SYS_MAX_FDS) {
		if (guest_fdmap[n] < 0) {
			guest_fdmap[n] = fd;
			return n;
		}
		n++;
	}
	die("system: out of file descriptors\n");
	return -1;
}

static int map_fd(uint32_t n) {
	if ((n >= SYS_MAX_FDS) || (guest_fdmap[n] < 0)) {
		die("system: invalid file descriptor: %d\n", n);
	}
	return guest_fdmap[n];
}

static const char* get_cstring(uint32_t addr) {
	const char* x = mem_dma(addr, 256);
	if (memchr(x, 0, 256) == NULL) {
		die("system: cstring larger than 255 bytes\n");
	}
	return x;
}

int sys_fd_open(uint32_t _path) {
	int fd = open(get_cstring(_path), O_RDONLY, 0644);
	return new_fd(fd);
}

int sys_fd_create(uint32_t _path) {
	int fd = open(get_cstring(_path), O_RDWR | O_CREAT | O_TRUNC, 0644);
	return new_fd(fd);
}

int sys_fd_close(uint32_t _fd) {
	close(map_fd(_fd));
	guest_fdmap[_fd] = -1;
	return 0;
}

int sys_fd_readc(uint32_t _fd) {
	uint8_t x;
	if (read(map_fd(_fd), &x, 1) == 1) {
		return x;
	} else {
		return -1;
	}
}

int sys_fd_write(uint32_t _fd, uint32_t _ptr, uint32_t off, uint32_t len) {
	void *data = mem_dma(_ptr + off, len);
	if (data == NULL) {
		die("system: write(): invalid memory address\n");
	}
	if (write(map_fd(_fd), data, len) == len) {
		return 0;
	} else {
		return 1;
	}
}

int sys_fd_writeu32(uint32_t _fd, uint32_t n) {
	if (write(map_fd(_fd), &n, 4) == 4) {
		return 0;
	} else {
		return -1;
	}
}

int sys_fd_writec(uint32_t _fd, uint32_t _c) {
	uint8_t c = _c;
	if (write(map_fd(_fd), &c, 1) == 1) {
		return 0;
	} else {
		return -1;
	}
}

int sys_fd_writes(uint32_t _fd, uint32_t _ptr) {
	const char *s = get_cstring(_ptr);
	size_t len = strlen(s);
	if (write(map_fd(_fd), s, len) == len) {
		return 0;
	} else {
		return -1;
	}
}

int sys_fd_writei(uint32_t _fd, int32_t n) {
	char tmp[64];
	int len = sprintf(tmp, "%d", n);
	if (write(map_fd(_fd), tmp, len) == len) {
		return 0;
	} else {
		return -1;
	}
}

int sys_fd_writex(uint32_t _fd, uint32_t n) {
	char tmp[64];
	int len = sprintf(tmp, "0x%x", n);
	if (write(map_fd(_fd), tmp, len) == len) {
		return 0;
	} else {
		return -1;
	}
}

static uint32_t heap_base = 4 * 1024 * 1024;
static uint32_t heap_alloc_count = 0;
static uint32_t heap_alloc_bytes = 0;

int sys_alloc(uint32_t sz) {
	uint32_t addr;
	if (safer_heap) {
		sz = (sz + 3) & (~3);
		// number of pages needed in bytes
		uint32_t need = (sz + 4095) & (~4095);

		// align allocation so next word past the
		// requested space will fault
		addr = heap_base + need - sz;

		// make those pages accessible and
		// leave guard page(s) after
		mem_protect(mem, heap_base, need, MEM_RW);
		heap_base = heap_base + need + safer_heap * 4096;
	} else {
		addr = heap_base;
		sz = (sz + 15) & (~15);
		heap_base += sz;
	}
	heap_alloc_count++;
	heap_alloc_bytes += sz;
	return addr;
}

#define R_SP 2
#define R_RV 5

#define ARG(n) mem_rd32(mem, sp + (n) * 4)

static inline uint32_t _do_syscall(uint32_t sp, uint32_t n) {
	//fprintf(stderr, "SYSCALL #0x%x 0x%x 0x%x 0x%x 0x%x\n", n, ARG(0), ARG(1), ARG(2), ARG(3));
	switch (n) {
	case 0x100: exit_emu(ARG(0));
	case 0x101: return mem_rd32(mem, guest_argv + 4 * ARG(0));
	case 0x102: return guest_argc;
	case 0x200: return sys_fd_open(ARG(0));
	case 0x201: return sys_fd_create(ARG(0));
	case 0x202: return sys_fd_close(ARG(0));
	case 0x203: return sys_fd_readc(ARG(0));
	case 0x204: return sys_fd_writec(ARG(0), ARG(1));
	case 0x205: return sys_fd_writes(ARG(0), ARG(1));
	case 0x206: return sys_fd_writei(ARG(0), ARG(1));
	case 0x207: return sys_fd_writex(ARG(0), ARG(1));
	case 0x208: return sys_fd_write(ARG(0), ARG(1), ARG(2), ARG(3));
	case 0x209: return sys_fd_writeu32(ARG(0), ARG(1));
	case 0x300: return sys_alloc(ARG(0));
	case 0x301: return heap_alloc_count;
	case 0x302: return heap_alloc_bytes;
	}
	return -1;
}

void do_syscall(CpuState *s, uint32_t n) {
	s->r[R_RV] = _do_syscall(s->r[R_SP], n);
	//fprintf(stderr,"RETURN: 0x%x\n", s->r[R_RV]);
}

