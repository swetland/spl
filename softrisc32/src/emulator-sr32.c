// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <emulator-sr32.h>

int quiet = 0;

#define RAMSIZE   (8*1024*1024)
#define RAMMASK8  (RAMSIZE - 1)
#define RAMMASK32 (RAMMASK8 & (~3))
#define RAMMASK16 (RAMMASK8 & (~1))
uint8_t emu_ram[RAMSIZE];

static CpuState CS;

uint32_t RD(uint32_t addr) {
	return *((uint32_t*) (emu_ram + (addr & RAMMASK32)));
}

typedef struct Symbol Symbol;
struct Symbol {
	Symbol *next;
	uint32_t addr;
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
	symlist = sym;
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
		CS.pc, CS.r[1], CS.r[2], CS.r[7]);
	fprintf(stderr, "T0 %08x  T1 %08x  T2 %08x  T3 %08x\n",
		CS.r[8], CS.r[9], CS.r[10], CS.r[11]);
	fprintf(stderr, "00 %08x  04 %08x  08 %08x  12 %08x\n",
		RD(CS.r[7] + 0), RD(CS.r[7] + 4), RD(CS.r[7] + 8), RD(CS.r[7] + 12));
	backtrace(CS.pc, CS.r[7]);
}

void memory_fault(uint32_t addr, int rd) {
	fprintf(stderr, "ERROR: %08x: memory %s fault: %08x\n", CS.pc, rd ? "read" : "write", addr);
	dump_cpu_state();
	exit(1);
}

static inline void check_rd(uint32_t addr) {
	if (addr < 65536) memory_fault(addr, 1);
	if (addr > RAMSIZE) memory_fault(addr, 1);
}
static inline void check_wr(uint32_t addr) {
	if (addr < 65536) memory_fault(addr, 0);
	if (addr > RAMSIZE) memory_fault(addr, 0);
}

#if 0
#define CHECK_RD(addr) (addr)
#define CHECK_WR(addr,val) (addr)
#else
#define CHECK_RD(addr) (check_rd(addr), addr)
#define CHECK_WR(addr,val) (check_wr(addr), addr)
#endif

uint32_t mem_rd32(uint32_t addr) {
	return *((uint32_t*) (emu_ram + (CHECK_RD(addr) & RAMMASK32)));
}
uint32_t mem_rd16(uint32_t addr) {
	return *((uint16_t*) (emu_ram + (CHECK_RD(addr) & RAMMASK16)));
}
uint32_t mem_rd8(uint32_t addr) {
	return *((uint8_t*) (emu_ram + (CHECK_RD(addr) & RAMMASK8)));
}

void mem_wr32(uint32_t addr, uint32_t val) {
	*((uint32_t*) (emu_ram + (CHECK_WR(addr, val) & RAMMASK32))) = val;
}
void mem_wr16(uint32_t addr, uint32_t val) {
	*((uint16_t*) (emu_ram + (CHECK_WR(addr, val) & RAMMASK16))) = val;
}
void mem_wr8(uint32_t addr, uint32_t val) {
	*((uint8_t*) (emu_ram + (CHECK_WR(addr, val) & RAMMASK8))) = val;
}

void *mem_dma(uint32_t addr, uint32_t len) {
	if (addr >= RAMSIZE) return 0;
	if ((RAMSIZE - addr) < len) return 0;
	return emu_ram + addr;
}

// quiet writes for init
void mem_wr32q(uint32_t addr, uint32_t val) {
	*((uint32_t*) (emu_ram + (addr & RAMMASK32))) = val;
}
void mem_wr8q(uint32_t addr, uint32_t val) {
	*((uint8_t*) (emu_ram + (addr & RAMMASK8))) = val;
}

int do_check_state = 0;
int do_dump_state = 0;

void check_state(void) {
	int fail = 0;
	char line[256];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if (line[0] == 'M') {
			uint32_t addr;
			uint32_t val;
			if (sscanf(line, "M %x %x", &addr, &val) == 2) {
				uint32_t actual = mem_rd32(addr);
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

void load_hex_image(const char* fn) {
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
			mem_wr32q(addr, val);
			char *x = strchr(line + 18, ':');
			if (x) {
				add_symbol(addr, x + 1);
			}
		}
	}
	fclose(fp);
}

void usage(int status) {
	fprintf(stderr,
		"usage:    emu <options> <image.hex> <arguments>\n"
		"options: -check            Check Machine State\n"
		"         -panic            Print Backtrace on Exit\n"
		"         -tf               Trace Instruction Fetches\n"
		"         -tr               Trace Register Writes\n"
		"         -tb               Trace Branches\n"
		"         -ti               Trace IO Reads & Writes\n"
		"         -q                Quiet\n");
	exit(status);
}

void sys_init(uint32_t argc, uint32_t argv);

int main(int argc, char** argv) {
	uint32_t entry = 0x100000;
	const char* fn = NULL;
	int args = 0;

	CpuState *cs = &CS;
	memset(cs, 0, sizeof(CpuState));
	memset(emu_ram, 0, sizeof(emu_ram));

	while (argc > 1) {
		if (!strcmp(argv[1], "-tf")) {
			cs->flags |= F_TRACE_FETCH;
		} else if (!strcmp(argv[1], "-tr")) {
			cs->flags |= F_TRACE_REGS;
		} else if (!strcmp(argv[1], "-tb")) {
			cs->flags |= F_TRACE_BRANCH;
		} else if (!strcmp(argv[1], "-ti")) {
			cs->flags |= F_TRACE_IO;
		} else if (!strcmp(argv[1], "-check")) {
			do_check_state = 1;
		} else if (!strcmp(argv[1], "-panic")) {
			do_dump_state = 1;
		} else if (!strcmp(argv[1], "-q")) {
			quiet = 1;
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

	load_hex_image(fn);

	uint32_t sp = entry - 16;
	uint32_t lr = sp;
	mem_wr32q(lr + 0, 0xfffd016b); // stx rv, -3 (exit)

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
				mem_wr8q(sp + i, argv[0][i]);
			}
			mem_wr32q(p, sp);
			p += 4;
			args--;
			argv++;
		}
		mem_wr32q(p, 0);
	}

	// args on stack for abi0
	sp -= 8;
	mem_wr32q(sp + 0, guest_argc);
	mem_wr32q(sp + 4, guest_argv);

	cs->pc = entry;
	cs->r[1] = lr;
	cs->r[2] = sp;
	// cs->r[10] = guest_argc;
	// cs->r[11] = guest_argv;

	sys_init(guest_argc, guest_argv);

	sr32core(cs);
	return 0;
}
