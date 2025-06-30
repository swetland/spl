// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <emulator-sr32.h>

#define MAXDATA 1024
uint32_t indata[MAXDATA];
uint32_t incount = 0;
uint32_t innext = 0;

uint32_t outdata[MAXDATA];
uint32_t outcount = 0;
uint32_t outnext = 0;

#define RAMSIZE   (8*1024*1024)
#define RAMMASK8  (RAMSIZE - 1)
#define RAMMASK32 (RAMMASK8 & (~3))
#define RAMMASK16 (RAMMASK8 & (~1))
uint8_t emu_ram[RAMSIZE];

uint32_t mem_rd32(uint32_t addr) {
	return *((uint32_t*) (emu_ram + (addr & RAMMASK32)));
}
uint32_t mem_rd16(uint32_t addr) {
	return *((uint16_t*) (emu_ram + (addr & RAMMASK16)));
}
uint32_t mem_rd8(uint32_t addr) {
	return *((uint8_t*) (emu_ram + (addr & RAMMASK8)));
}

void mem_wr32(uint32_t addr, uint32_t val) {
	*((uint32_t*) (emu_ram + (addr & RAMMASK32))) = val;
}
void mem_wr16(uint32_t addr, uint32_t val) {
	*((uint16_t*) (emu_ram + (addr & RAMMASK16))) = val;
}
void mem_wr8(uint32_t addr, uint32_t val) {
	*((uint8_t*) (emu_ram + (addr & RAMMASK8))) = val;
}

void *mem_dma(uint32_t addr, uint32_t len) {
	if (addr >= RAMSIZE) return 0;
	if ((RAMSIZE - addr) < len) return 0;
	return emu_ram + addr;
}

uint32_t io_rd32(CpuState *cs, uint32_t addr) {
	if (addr == -1) {
		if (innext == incount) {
			fprintf(stderr, "FAIL: PC=%08x: input data exhausted\n", cs->pc);
			exit(1);
		}
		if (cs->flags & F_TRACE_IO) {
			fprintf(stderr, "< %08x\n", indata[innext]);
		}
		return indata[innext++];
	}
	return 0;
}

void io_wr32(CpuState *cs, uint32_t addr, uint32_t val) {
	switch (addr) {
	case -1:
		if (cs->flags & F_TRACE_IO) {
			fprintf(stderr, "> %08x\n", val);
		}
		if (outnext == outcount) {
			fprintf(stderr, "FAIL: output data overrun\n");
			exit(1);
		}
		uint32_t data = outdata[outnext++];
		if (data != val) {
			fprintf(stderr, "FAIL: PC=%08x: output data %08x should be %08x\n", cs->pc, val, data);
			exit(1);
		}
		break;
	case -2: {
		uint8_t x = val;
		if (write(2, &x, 1) != 1) ;
		break;
	}
	case -3:
		exit(0);
	}
}

void do_syscall(CpuState *s, uint32_t n) {
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
			mem_wr32(addr, val);
		}
	}
	fclose(fp);
}

void load_test_data(const char *fn) {
	char line[1024];
	FILE *fp = fopen(fn, "r");
	if (fp == NULL) {
		fprintf(stderr, "emu: cannot open: %s\n", fn);
		exit(1);
	}
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *x;
		uint32_t n;
		if ((x = strstr(line, "//>"))) {
			x += 3;
			for (;;) {
				n = strtoul(x, &x, 0);
				outdata[outcount++] = n;
				x = strchr(x, ',');
				if (x == NULL) break;
				x++;
			}
		}
		if ((x = strstr(line, "//<"))) {
			x += 3;
			for (;;) {
				n = strtoul(x, &x, 0);
				indata[incount++] = n;
				x = strchr(x, ',');
				if (x == NULL) break;
				x++;
			}
		}
	}
	fclose(fp);
}

void usage(int status) {
	fprintf(stderr,
		"usage:    emu <options> <image.hex> <arguments>\n"
		"options: -x <datafile>     Load Test Vector Data\n"
		"         -tf               Trace Instruction Fetches\n"
		"         -tr               Trace Register Writes\n"
		"         -tb               Trace Branches\n"
		"         -ti               Trace IO Reads & Writes\n");
	exit(status);
}

int main(int argc, char** argv) {
	uint32_t entry = 0x100000;
	const char* fn = NULL;
	int args = 0;

	CpuState cs;
	memset(&cs, 0, sizeof(cs));
	memset(emu_ram, 0, sizeof(emu_ram));

	while (argc > 1) {
		if (!strcmp(argv[1], "-x")) {
			if (argc < 3) usage(1);
			load_test_data(argv[2]);
			argc--;
			argv++;
		} else if (!strcmp(argv[1], "-tf")) {
			cs.flags |= F_TRACE_FETCH;
		} else if (!strcmp(argv[1], "-tr")) {
			cs.flags |= F_TRACE_REGS;
		} else if (!strcmp(argv[1], "-tb")) {
			cs.flags |= F_TRACE_BRANCH;
		} else if (!strcmp(argv[1], "-ti")) {
			cs.flags |= F_TRACE_IO;
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
	mem_wr32(lr + 0, 0xfffd006b);

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
				mem_wr8(sp + i, argv[0][i]);
			}
			mem_wr32(p, sp);
			p += 4;
			args--;
			argv++;
		}
		mem_wr32(p, 0);
	}

	cs.pc = entry;
	cs.r[1] = lr;
	cs.r[2] = sp;
	cs.r[4] = guest_argc;
	cs.r[5] = guest_argv;

	sr32core(&cs);
	return 0;
}
