// Copyright 2026, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

#include <ir.h>

void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr,"\n");
	exit(1);
}

#define RAMSIZE   (8*1024*1024)
#define RAMMASK8  (RAMSIZE - 1)
#define RAMMASK32 (RAMMASK8 & (~3))
#define RAMMASK16 (RAMMASK8 & (~1))

// emulator-only instructions
#define INS_BLOCK (INS_COUNT + 0) // start of function
#define INS_MAGIC (INS_COUNT + 1) // missing or builtin fn

typedef struct Inst {
	uint32_t op;
	uint32_t a;
	uint32_t b;
	uint32_t c;
} Inst;

typedef struct State {
	Inst *code;
	uint32_t pcmax;  // last valid instruction
	uint32_t vrmax;  // active virtual register window size
	uint32_t *vr;    // active virtual register window base
	uint32_t pr[32]; // physical register file
	uint8_t *data;   // RAM
	uint32_t *vregs; // virtual register storage start
	uint32_t *vlast; // virtual register storage end
	uint32_t gmax;   // number of global names
	char**   gname;  // array of global names
} State;

void magic(State *s, uint32_t n);

static uint32_t memrd(State *s, uint32_t op, uint32_t addr) {
	if (op & INF_SZ_U32) {
		return *((uint32_t*) (s->data + (addr & RAMMASK32)));
	} else if (op & INF_SZ_U16) {
		return *((uint16_t*) (s->data + (addr & RAMMASK16)));
	} else {
		return *((uint8_t*) (s->data + (addr & RAMMASK8)));
	}
}
static void memwr(State *s, uint32_t op, uint32_t addr, uint32_t val) {
	if (op & INF_SZ_U32) {
		*((uint32_t*) (s->data + (addr & RAMMASK32))) = val;
	} else if (op & INF_SZ_U16) {
		*((uint16_t*) (s->data + (addr & RAMMASK16))) = val;
	} else {
		*((uint8_t*) (s->data + (addr & RAMMASK8))) = val;
	}
}

#define VF_PHYS  0x10000000
#define VF_UNDEF 0x20000000

static uint32_t regrd(State *s, uint32_t r) {
	if (r & VF_UNDEF) {
		return 0xe1e1e1e1;
	}
	if (r & VF_PHYS) {
		r &= 0x0FFFFFFF;
		if (r > 31) die("regrd $%d", r);
		return s->pr[r];
	} else {
		if (r >= s->vrmax) die("regrd %%%d", r);
		return s->vr[r];
	}
}
static void regwr(State *s, uint32_t r, uint32_t v) {
	if (r & VF_PHYS) {
		r &= 0x0FFFFFFF;
		if (r > 31) die("regwr $%d", r);
		s->pr[r] = v;
	} else {
		if (r >= s->vrmax) die("regwr %%%d", r);
		s->vr[r] = v;
	}
}

#define XA (regrd(s, i.a))
#define XB (regrd(s, i.b))
#define XC ((i.op & INF_C_IMM) ? (i.c) : regrd(s, i.c))

#define IB ((int32_t) XB)
#define IC ((int32_t) XC)

void emu(State *s, uint32_t pc) {
	// ensure that the entry point is a valid fn block
	if (s->code[pc].op != INS_BLOCK) die("invalid fn @ %d", pc);

	// ensure we have room for the virtual registers
	uint32_t n = s->code[pc].a;
	if ((s->vlast - s->vr) < n) die("vreg overflow");
	s->vrmax = n;

	for (;;) {
		if (pc >= s->pcmax) die("invalid pc: %d", pc);
		Inst i = s->code[pc++];
//fprintf(stderr, "%04d: %08x %08x %08x %08x\n", pc-1, i.op, i.a, i.b, i.c);
		switch (i.op & INS_OP_MASK) {
		case INS_ADD:    n = XB + XC; break;
		case INS_SUB:    n = XB - XC; break;
		case INS_MUL:    n = XB * XC; break;
		case INS_UDIV:   n = XB / XC; break;
		case INS_SDIV:   n = IB / IC; break;
		case INS_UREM:   n = XB % XC; break;
		case INS_SREM:   n = IB % IC; break;
		case INS_LSL:    n = XB << XC; break;
		case INS_LSR:    n = XB >> XC; break;
		case INS_ASR:    n = IB >> XC; break;
		case INS_AND:    n = XB & XC; break;
		case INS_OR:     n = XB | XC; break;
		case INS_XOR:    n = XB ^ XC; break;
		case INS_CMP_EQ: n = XB == XC; break;
		case INS_CMP_NE: n = XB != XC; break;
		case INS_CMP_LT: n = IB < IC; break;
		case INS_CMP_LE: n = IB <= IC; break;
		case INS_CMP_GT: n = IB > IC; break;
		case INS_CMP_GE: n = IB >= IC; break;
		case INS_NOT:    n = ~XC; break;
		case INS_NEG:    n = -IC; break;
		case INS_MOV:    n = XC; break;
		case INS_LOAD:   n = memrd(s, i.op, XB + IC); break;
		case INS_STORE:  memwr(s, i.op, XB + IC, XA); continue;
		case INS_LABEL:  continue;
		case INS_BLOCK:  continue;
		case INS_JUMP:   pc = i.a; continue;
		case INS_BRANCH: pc = XA ? i.b : i.c; continue;
		case INS_CALL:   {
			n = s->vrmax;
			s->vr += n;
			emu(s, i.a);
			s->vr -= n;
			s->vrmax = n;
			continue;
		}
		case INS_RET:    return;
		case INS_SET:    n = XB; break;
		case INS_GET:    continue;
		case INS_MAGIC:  magic(s, i.a); continue;
		default:         die("invalid op %d", i.op & INS_OP_MASK);
		}
		regwr(s, i.a, n);
	}
}

#define MAGIC_UNDEF  -1
#define MAGIC_HEXOUT -2

void magic(State *s, uint32_t n) {
	switch (n) {
	case MAGIC_HEXOUT:
		printf("D %08x\n", memrd(s, INF_SZ_U32, s->pr[2]));
		return;
	default:
		die("bad magic %d", n);
	}
}

static uint32_t rd32(int fd) {
	uint32_t n;
	if (read(fd, &n, sizeof(n)) != sizeof(n)) {
		die("read error");
	}
	return n;
}

int32_t load(State *s, const char *fn) {
	int32_t *gentry = 0;
	int32_t start = 0;
	int fd = open(fn, O_RDONLY);
	uint32_t n;
	if (fd < 0) die("cannot open '%s'", fn);
	if (rd32(fd) != TAG_IR_XIR0) die("invalid xir file");
	rd32(fd);
	rd32(fd);
	rd32(fd);
	for (;;) {
		n = rd32(fd);
		if (n == TAG_IR_FUNC) {
			uint32_t icount = rd32(fd);
			uint32_t id = rd32(fd);
			uint32_t rcount = rd32(fd);
			// the header becomes a start-of-block instruction
			s->code[s->pcmax].op = INS_BLOCK;
			s->code[s->pcmax].a = rcount;
			s->code[s->pcmax].b = id;
			s->code[s->pcmax].c = s->pcmax;
			s->pcmax++;
			n = icount * sizeof(Inst);
			if (read(fd, s->code + s->pcmax, n) != n) die("read fail: code");
			s->pcmax += icount;
		} else if (n == TAG_IR_GLBL) {
			s->gmax = rd32(fd);
			n = s->gmax * sizeof(int32_t);
			gentry = malloc(n);
			s->gname = malloc(s->gmax * sizeof(char*));
			if (read(fd, gentry, n) != n) die("read fail: str entries");
			n = 0;
			for (int i = 0; i < s->gmax; i++) {
				n += gentry[i] + 1;
			}
			char* strtab = malloc(n);
			if (read(fd, strtab, n) != n) die("read fail: str table");
			for (int i = 0; i < s->gmax; i++) {
				s->gname[i] = strtab;
				strtab += gentry[i] + 1;
				gentry[i] = MAGIC_UNDEF;
			}
			break;
		}
	}
	close(fd);
	// map functions
	for (n = 0; n < s->pcmax; n++) {
		Inst *i = s->code + n;
		if (i->op == INS_BLOCK) {
			if (i->c != n) die("%d: bad block pc", n);
			if (i->b >= s->gmax) die("%d: bad block id", n);
			gentry[i->b] = i->c;
		} else if (i->op == INS_LABEL) {
			if (i->c != n) die("%d: bad label pc", n);
		}
	}
	// resolve start and builtin functions
	for (n = 0; n < s->gmax; n++) {
		if (!strcmp(s->gname[n],"start")) start = gentry[n];
		if (gentry[n] < 0) {
			if (!strcmp(s->gname[n],"_hexout_")) gentry[n] = MAGIC_HEXOUT;
		}
	}
	// convert CALLs from global id to instruction id
	// translate CALLs to undef fns to MAGICs
	for (n = 0; n < s->pcmax; n++) {
		Inst *i = s->code + n;
		if (i->op == INS_CALL) {
			if (i->a >= s->gmax) die("bad call");
			if (gentry[i->a] < 0) {
				i->op = INS_MAGIC;
			}
			i->a = gentry[i->a];
		}
	}
	if (start < 0) die("no start function");
	return start;
}

int main(int argc, char **argv) {
	State *s = calloc(1, sizeof(State));
	s->code = malloc(1 * 1024 * 1024);
	s->vregs = malloc(4096 * sizeof(uint32_t));
	s->data = malloc(RAMSIZE);
	s->vlast = s->vregs + 4096;
	s->vr = s->vregs;
	s->vrmax = 0;
	s->pr[2] = 1024*1024; // SP
	if (argc != 2) {
		return -1;
	}
	emu(s, load(s, argv[1]));
	printf("X %08x\n", s->pr[5]);
	return 0;
}

