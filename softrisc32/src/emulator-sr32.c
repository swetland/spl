// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include <emulator-sr32.h>

void do_syscall_abi0(CpuState *s, uint32_t n);
void do_syscall_abi1(CpuState *s, uint32_t n);

int quiet = 0;

#define RAMSIZE   (8*1024*1024)
void *mem;

static CpuState CS;

static inline uint32_t RD(uint32_t addr) {
	uint32_t value;
	mem_rd32_safe(mem, addr, &value);
	return value;
}

typedef struct Symbol Symbol;
struct Symbol {
	Symbol *next;
	uint32_t addr;
	uint64_t perf;
	char name[0];
};

Symbol *symlist = NULL;

void add_symbol(uint32_t addr, const char *name) {
	if (name[0] && (name[1] == '$')) {
		return;
	}
	int len = strlen(name);
	Symbol *sym = malloc(sizeof(Symbol) + len + 1);
	memcpy(sym->name, name, len);
	sym->name[len - 1] = 0;
	sym->addr = addr;
	sym->next = symlist;
	sym->perf = 0;
	symlist = sym;
}

static uint32_t program_start = 0xffffffff;
static uint32_t program_end = 0x00000000;

uint32_t get_exec_count(uint32_t addr);

void dump_perf(const char* fn) {
	uint64_t total = 0;
	for (uint32_t pc = program_start; pc < program_end; pc += 4) {
		Symbol *sym = symlist;
		uint32_t count = get_exec_count(pc);
		total += count;
		while (sym && sym->next) {
			if (sym->next->addr <= pc) {
				sym->next->perf += count;
				break;
			}
			sym = sym->next;
		}
	}
	FILE *fp = fopen(fn, "w+");
	if (fp != NULL) {
		Symbol *sym = symlist;
		while (sym && sym->next) {
			fprintf(fp, "%10.3f %08x %s\n",
				((double)sym->perf) / ((double)total) * 100.0,
				sym->addr, sym->name);
			sym = sym->next;
		}
		fclose(fp);
	}
}

void backtrace(uint32_t pc, uint32_t fp) {
	unsigned max = 32;
	while (max > 0) {
		fprintf(stderr, "PC %08x  FP %08x  ", pc, fp);

		Symbol *sym = symlist;
		while (sym && sym->next) {
			if (sym->next->addr <= pc) {
				fprintf(stderr, "%s + 0x%x", sym->next->name, pc - sym->next->addr);
				break;
			}
			sym = sym->next;
		}
		fprintf(stderr, "\n");

		pc = RD(fp - 4);
		fp = RD(fp - 8);
		if (pc < 0x00100000) return;
		if (fp > 0x00100000) return;
		max--;
	}
}

void dump_cpu_state(void) {
	fprintf(stderr, "\nPC %08x  RA %08x  SP %08x  FP %08x\n",
		CS.pc, CS.r[1], CS.r[2], CS.r[8]);
	fprintf(stderr, "A0 %08x  A1 %08x  A2 %08x  A3 %08x\n",
		CS.r[10], CS.r[11], CS.r[12], CS.r[13]);
	fprintf(stderr, "T0 %08x  T1 %08x  T2 %08x  T3 %08x\n",
		CS.r[5], CS.r[6], CS.r[7], CS.r[28]);
	fprintf(stderr, "00 %08x  04 %08x  08 %08x  12 %08x\n",
		RD(CS.r[8] + 0), RD(CS.r[8] + 4), RD(CS.r[8] + 8), RD(CS.r[8] + 12));
	backtrace(CS.pc, CS.r[8]);
}

void memory_fault(uint32_t addr) {
	fprintf(stderr, "ERROR: %08x: memory fault: %08x\n", CS.pc, addr);
	dump_cpu_state();
	exit(1);
}

// TODO handle traps
void *mem_dma(uint32_t addr, uint32_t len) {
	return mem + addr;
}

static int do_check_state = 0;
static int do_dump_state = 0;
static const char* dump_perf_fn = NULL;

void check_state(void) {
	int fail = 0;
	char line[256];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if (line[0] == 'M') {
			uint32_t addr;
			uint32_t val;
			if (sscanf(line, "M %x %x", &addr, &val) == 2) {
				uint32_t actual = RD(addr);
				if (actual != val) {
					fprintf(stderr, "M %08x %08x != %08x\n", addr, val, actual);
					fail = 1;
				}
			}
		}
	}
	if (fail) {
		exit(1);
	}
}

void exit_emu(int status) {
	if (do_check_state) check_state();
	if (do_dump_state) dump_cpu_state();
	if (dump_perf_fn) dump_perf(dump_perf_fn);
	exit(status);
}

uint32_t io_rd32(CpuState *cs, uint32_t addr) {
	return 0;
}

void io_wr32(CpuState *cs, uint32_t addr, uint32_t val) {
	switch (addr) {
	case -1:
		uint8_t x = val;
		if (write(1, &x, 1) != 1) ;
		break;
	case -2:
		fprintf(stdout, "D %08x\n", val);
		break;
	case -3:
		if (!quiet) {
			fprintf(stdout, "X %08x\n", val);
		}
		exit_emu(0);
	case -4:
		fprintf(stderr, "FAILURE %08x\n", val);
		exit_emu(1);
	}
}

void do_undef(CpuState *s, uint32_t ins) {
	fprintf(stderr, "UNDEF INSTR (PC=%08x INS=%08x)\n", s->pc, ins);
	exit(1);
}

uint32_t load_hex_image(const char* fn) {
	char line[1024];
	FILE *fp = fopen(fn, "r");
	if (fp == NULL) {
		fprintf(stderr, "emu: cannot open: %s\n", fn);
		exit(1);
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
		if ((line[0] == '#') || (line[0] == '/')) {
			continue;
		}
		if ((strlen(line) > 18) && (line[8] == ':')) {
			uint32_t addr = strtoul(line, 0, 16);
			uint32_t val = strtoul(line + 10, 0, 16);
			mem_wr32(mem, addr, val);
			char *x = strchr(line + 18, ':');
			if (x) {
				add_symbol(addr, x + 1);
			}
			if (addr < program_start) {
				program_start = addr;
			}
			if (addr > program_end) {
				program_end = addr;
			}
		}
	}
	fclose(fp);
	return program_start;
}

void usage(int status) {
	fprintf(stderr,
		"usage:    emu <options> <image.hex> <arguments>\n"
		"options: -check            Check Machine State\n"
		"         -panic            Print Backtrace on Exit\n"
		"         -hgp              Enable Heap Guard Pages\n"
		"         -limit <s>        Impose Time Limit (Seconds)\n"
		"         -tf               Trace Instruction Fetches\n"
		"         -tr               Trace Register Writes\n"
		"         -tb               Trace Branches\n"
		"         -ti               Trace IO Reads & Writes\n"
		"         -perf <filename>  Write Perf Dump\n"
		"         -q                Quiet\n");
	exit(status);
}

void timeout_handler(int sig, siginfo_t *si, void *uc) {
	if (write(2, "\nTIMEOUT\n", 9) != 9) ;
	exit(1);
}

void ctrl_c_handler(int n) {
	if (write(2, "\nINTERRUPT\n", 11) != 11) ;
	dump_cpu_state();
	exit(1);
}

void set_time_limit(int seconds) {
	timer_t tid;
	struct sigevent sev;
	struct sigaction sa;
	struct itimerspec its;

	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = timeout_handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGRTMIN, &sa, NULL) < 0) exit(1);

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &tid;
	if (timer_create(CLOCK_MONOTONIC, &sev, &tid) < 0) exit(1);

	its.it_value.tv_sec = seconds;
	its.it_value.tv_nsec = 0;
	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	if (timer_settime(tid, 0, &its, NULL) < 0) exit(1);
}

void sys_init(uint32_t argc, uint32_t argv, uint32_t heap_guard_pages);

int main(int argc, char** argv) {
	uint32_t guard_pages = 0;
	const char* fn = NULL;
	int args = 0;
	int timeout = 0;

	mem = mem_init();
	mem_protect(mem, 65536, RAMSIZE - 65536, MEM_RW);
	memset(mem + 65536, 0, RAMSIZE - 65536);

	CpuState *cs = &CS;
	memset(cs, 0, sizeof(CpuState));
	cs->mem = mem;
	cs->syscall = do_syscall_abi0;

	signal(SIGINT, ctrl_c_handler);

	while (argc > 1) {
		if (!strcmp(argv[1], "-tf")) {
			cs->flags |= F_TRACE_FETCH;
		} else if (!strcmp(argv[1], "-tr")) {
			cs->flags |= F_TRACE_REGS;
		} else if (!strcmp(argv[1], "-tb")) {
			cs->flags |= F_TRACE_BRANCH;
		} else if (!strcmp(argv[1], "-ti")) {
			cs->flags |= F_TRACE_IO;
		} else if (!strcmp(argv[1], "-hgp")) {
			guard_pages = 4;
		} else if (!strcmp(argv[1], "-check")) {
			do_check_state = 1;
		} else if (!strcmp(argv[1], "-panic")) {
			do_dump_state = 1;
		} else if (!strcmp(argv[1], "-perf")) {
			if (argc > 1) {
				argc--;
				argv++;
				dump_perf_fn = argv[1];
			}
		} else if (!strcmp(argv[1], "-limit")) {
			if (argc > 1) {
				argc--;
				argv++;
				timeout = atoi(argv[1]);
			}
		} else if (!strcmp(argv[1], "-q")) {
			quiet = 1;
		} else if (!strcmp(argv[1], "-abi1")) {
			cs->syscall = do_syscall_abi1;
		} else if (argv[1][0] == '-') {
			fprintf(stderr, "emu: unknown option: %s\n", argv[1]);
			return -1;
		} else {
			fn = argv[1];
			// arguments after the image file belong to the guest
			args = argc - 2;
			argv += 2;
			break;
		}
		argc--;
		argv++;
	}
	if (fn == NULL) {
		usage(1);
	}

	uint32_t entry = load_hex_image(fn);

	if (entry == 0xffffffff) {
		fprintf(stderr, "nothing to execute\n");
		return -1;
	}

	uint32_t sp = entry - 16;
	uint32_t lr = sp;
	mem_wr32(mem, lr + 0, 0xfffd016b); // stx rv, -3 (exit)

	uint32_t guest_argc = args;
	uint32_t guest_argv = 0;
	if (args) {
		sp -= (args + 1) * 4;
		uint32_t p = sp;
		guest_argv = p;
		while (args > 0) {
			uint32_t n = strlen(argv[0]) + 1;
			sp -= (n + 3) & (~3);
			for (uint32_t i = 0; i < n; i++) {
				mem_wr8(mem, sp + i, argv[0][i]);
			}
			mem_wr32(mem, p, sp);
			p += 4;
			args--;
			argv++;
		}
		mem_wr32(mem, p, 0);
	}

	// args on stack for abi0
	sp -= 8;
	mem_wr32(mem, sp + 0, guest_argc);
	mem_wr32(mem, sp + 4, guest_argv);

	cs->pc = entry;
	cs->r[1] = lr;
	cs->r[2] = sp;
	// cs->r[10] = guest_argc;
	// cs->r[11] = guest_argv;

	if (timeout > 0) {
		set_time_limit(timeout);
	}

	sys_init(guest_argc, guest_argv, guard_pages);

	// TODO: protect program memory from writes

	uint32_t fault_addr = 0;
	if (mem_try((void*) sr32core, cs, &fault_addr)) {
		memory_fault(fault_addr);
	}
	return 0;
}
