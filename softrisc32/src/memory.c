// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <memory.h>

#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <stdio.h>

typedef struct MemTrap MemTrap;

struct MemTrap {
	jmp_buf env;
	MemTrap *next;
	uint32_t addr;
	uint32_t flags;
};

static void *mem_base = NULL;
static MemTrap *mem_traps = NULL;

#define SZ_4GB   0x100000000ULL
#define MASK_4GB 0x0FFFFFFFFULL

static void mem_segv_handler(int signal, siginfo_t *si, void *arg) {
	uint64_t addr = (uint64_t) si->si_addr;
	uint64_t base = (uint64_t) mem_base;

	if ((addr >= base) && (addr < (base + SZ_4GB))) {
		// within the arena
		if (mem_traps == NULL) {
			fprintf(stderr, "MEM FAULT @ %08x\n", (uint32_t) (addr - base));
		} else {
			mem_traps->addr = (addr - base);
			mem_traps->flags = 0;
			longjmp(mem_traps->env, 1);
		}
	} else {
		fprintf(stderr, "OOPS: SEGV @ %p\n", si->si_addr);
	}
	exit(0);
}

void *mem_init() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = mem_segv_handler;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);

	// need a full 4GB region with 4GB in front and behind
	// it and 4GB extra so we can align it to a 4GB boundry
        uint8_t *ptr = mmap(0, SZ_4GB * 4ULL, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

	// align to 4GB
	uint8_t *pt2 = (void*) ((((uint64_t) ptr) + MASK_4GB) & (~MASK_4GB));

	if (pt2 - ptr) {
		munmap(ptr, pt2 - ptr);
	}

	mem_base = pt2 + SZ_4GB;

	return mem_base;
}

void mem_protect(void *mem, uint32_t addr, uint32_t length, uint32_t _flags) {
	uint32_t flags = 0;
	if (_flags & MEM_RD) {
		flags |= PROT_READ;
	}
	if (_flags & MEM_WR) {
		flags |= PROT_WRITE;
	}
	mprotect(mem + addr, length, flags);
}

int mem_rd32_safe(void *mem, uint32_t addr, uint32_t *value) {
	MemTrap trap;
	trap.next = mem_traps;
	mem_traps = &trap;
	if (setjmp(trap.env)) {
		*value = 0xEEEEEEEE;
		mem_traps = trap.next;
		return -1;
	} else {
		*value = mem_rd32(mem, addr);
		mem_traps = trap.next;
		return 0;
	}
}

int mem_try(mem_try_fn fn, void *cookie, uint32_t *fault_addr) {
	MemTrap trap;
	trap.next = mem_traps;
	mem_traps = &trap;
	if (setjmp(trap.env)) {
		mem_traps = trap.next;
		*fault_addr = trap.addr;
		return -1;
	} else {
		fn(cookie);
		mem_traps = trap.next;
		return 0;
	}
}

