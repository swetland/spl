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

#define R_SP 2
#define R_GP 3
#define R_RV 5

void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr,"\n");
	exit(1);
}

static int do_trace = 0;
static int do_check_state = 0;

#define RAMSIZE   (8*1024*1024)
#define RAMMASK8  (RAMSIZE - 1)
#define RAMMASK32 (RAMMASK8 & (~3))
#define RAMMASK16 (RAMMASK8 & (~1))

// emulator-only instructions
#define INS_BLOCK (INS_COUNT + 0) // start of function
#define INS_MAGIC (INS_COUNT + 1) // missing or builtin fn

#define VF_PHYS  0x10000000
#define VF_UNDEF 0x20000000

typedef struct Inst {
	uint32_t op;
	uint32_t a;
	uint32_t b;
	uint32_t c;
} Inst;

void fmtarg(char *buf, uint32_t n, int isreg) {
	if (!isreg) {
		sprintf(buf, "%d", n);
	} else {
		if (n & VF_PHYS) {
			n &= 0x0fffffff;
			sprintf(buf, "$%d", n);
		} else if (n & VF_UNDEF) {
			sprintf(buf, "$und");
		} else {
			sprintf(buf, "%%%d", n);
		}
	}
}

const char* opname(uint32_t op) {
	if (op == INS_BLOCK) return "block";
	if (op == INS_MAGIC) return "magic";
	if (op < INS_COUNT) return iop_name[op];
	return "invalid";
}

void trace(Inst *i, uint32_t pc, uint32_t v) {
	uint32_t op = i->op & INS_OP_MASK;
	char sop[32], sa[32], sb[32], sc[32];
	fmtarg(sa, i->a, i->op & (INF_SET_A|INF_USE_A));
	fmtarg(sb, i->b, i->op & INF_USE_B);
	fmtarg(sc, i->c, i->op & INF_USE_C);
	const char *ssz = "";
	if (i->op & INF_IS_MEM) {
		if (i->op & INF_SZ_U32) { ssz = "w"; }
		else if (i->op & INF_SZ_U16) { ssz = "h"; }
		else { ssz = "b"; }
	}
	sprintf(sop, "%s%s%s", opname(op), ssz, i->op & INF_C_IMM ? "i" : "");
	if (i->op & INF_SET_A) {
		fprintf(stderr, "%04d %08x %-8s %s, %s, %s\n", pc - 1, v, sop, sa, sb, sc);
	} else {
		fprintf(stderr, "%04d          %-8s %s, %s, %s\n", pc - 1, sop, sa, sb, sc);
	}
}

typedef struct RegPair {
	uint32_t r;
	uint32_t s;
} RegPair;

typedef struct State {
	Inst *code;
	uint32_t pcmax;  // last valid instruction
	uint32_t vrmax;  // active virtual register window size
	RegPair *vr;    // active virtual register window base
	uint32_t pr[32]; // physical register file
	uint8_t *data;   // RAM
	RegPair *vregs; // virtual register storage start
	RegPair *vlast; // virtual register storage end
	uint32_t gmax;   // number of global names
	char**   gname;  // array of global names
} State;

void magic(State *s, uint32_t n);

static uint32_t memrd(State *s, uint32_t op, uint32_t addr) {
	if (addr < 65536) die("memrd fault %08x", addr);
	if (op & INF_SZ_U32) {
		return *((uint32_t*) (s->data + (addr & RAMMASK32)));
	} else if (op & INF_SZ_U16) {
		return *((uint16_t*) (s->data + (addr & RAMMASK16)));
	} else {
		return *((uint8_t*) (s->data + (addr & RAMMASK8)));
	}
}
static void memwr(State *s, uint32_t op, uint32_t addr, uint32_t val) {
	if (addr < 65536) die("memwr fault %08x", addr);
	if (op & INF_SZ_U32) {
		*((uint32_t*) (s->data + (addr & RAMMASK32))) = val;
	} else if (op & INF_SZ_U16) {
		*((uint16_t*) (s->data + (addr & RAMMASK16))) = val;
	} else {
		*((uint8_t*) (s->data + (addr & RAMMASK8))) = val;
	}
}

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
		return s->vr[r].r;
	}
}
static void regwr(State *s, uint32_t r, uint32_t v) {
	if (r & VF_PHYS) {
		r &= 0x0FFFFFFF;
		if (r > 31) die("regwr $%d", r);
		s->pr[r] = v;
	} else {
		if (r >= s->vrmax) die("regwr %%%d", r);
		s->vr[r].r = v;
	}
}

static uint32_t srrd(State *s, uint32_t r) {
	if ((r & 0xF0000000) || (r >= s->vrmax)) {
		die("srrd %d", r);
	}
	return s->vr[r].s;
}
static void srwr(State *s, uint32_t r, uint32_t v) {
	if ((r & 0xF0000000) || (r >= s->vrmax)) {
		die("srrd %d", r);
	}
	s->vr[r].s = v;
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
		if (do_trace && !(i.op & INF_SET_A)) { trace(&i, pc, 0); }
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
		case INS_CDATA:  n = i.b; break;
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
		case INS_SET:    srwr(s, i.a, regrd(s, i.c)); continue;
		case INS_GET:    n = srrd(s, i.c); break;
		case INS_MAGIC:  magic(s, i.a); continue;
		default:         die("invalid op %d", i.op & INS_OP_MASK);
		}
		regwr(s, i.a, n);
		if (do_trace) { trace(&i, pc, n); }
	}
}

#define MAGIC_UNDEF  -1
#define MAGIC_HEXOUT -2
#define MAGIC_ALLOC  -3

static uint32_t heap_base = 4 * 1024 * 1024;

uint32_t sys_alloc(uint32_t sz) {
	uint32_t addr = heap_base;
	sz = (sz + 15) & (~15);
	heap_base += sz;
	return addr;
}

void magic(State *s, uint32_t n) {
	switch (n) {
	case MAGIC_HEXOUT:
		printf("D %08x\n", memrd(s, INF_SZ_U32, s->pr[R_SP]));
		return;
	case MAGIC_ALLOC:
		s->pr[R_RV] = sys_alloc(memrd(s, INF_SZ_U32, s->pr[R_SP]));
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

typedef struct DataChunk DataChunk;
struct DataChunk {
	DataChunk *next;
	uint32_t id;
	uint32_t count;
	uint32_t flags;
	uint32_t addr;
	uint32_t *pmap;
	uint32_t data[0];
};

static DataChunk *dclist = NULL;

static DataChunk *dc_find(uint32_t id) {
	DataChunk *dc = dclist;
	while (dc != NULL) {
		if (dc->id == id) {
			return dc;
		}
		dc = dc->next;
	}
	die("missing data chunk 0x%08x", id);
	return NULL;
}

void setup_data(State *s) {
	uint32_t addr = 2 * 1024 * 1024;

	// assign chunks to ram ranges
	DataChunk *dc = dclist;
	while (dc != NULL) {
		dc->addr = addr;
		if (dc->id == 0) {
			s->pr[R_GP] = addr; // init $gp
		}
		addr += dc->count * sizeof(uint32_t);
		dc = dc->next;
	}
	// resolve pointers from dc id to address
	dc = dclist;
	while (dc != NULL) {
		if (dc->pmap != NULL) {
			for (uint32_t n = 0; n < dc->count; n++) {
				// for every pointer word
				if (dc->pmap[n >> 5] & (1 << (n & 31))) {
					// translate ID to global address
					dc->data[n] = dc_find(dc->data[n])->addr;
				}
			}
		}
		dc = dc->next;
	}
	// copy chunk data into ram
	dc = dclist;
	while (dc != NULL) {
		for (uint32_t n = 0; n < dc->count; n++) {
			memwr(s, INF_SZ_U32, dc->addr + n * sizeof(uint32_t), dc->data[n]);
		}
		dc = dc->next;
	}
}

int32_t load(State *s, const char *fn) {
	int32_t *gentry = 0;
	int32_t start = 0xffffffff;
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
		} else if (n == TAG_IR_DATA) {
			uint32_t count = rd32(fd);
			uint32_t id = rd32(fd);
			uint32_t flags = rd32(fd);
			uint32_t pcount = 0;
			if (flags & 1) {
				pcount = (count + 31) >> 5;
			}
			// combined size of data chunk words + pmap
			uint32_t n = (count + pcount) * sizeof(uint32_t);
			DataChunk *dc = calloc(1, sizeof(DataChunk) + n);
			dc->id = id;
			dc->count = count;
			dc->flags = flags;
			dc->pmap = pcount ? (dc->data + count) : NULL;
			dc->next = dclist;
			dclist = dc;
			if (read(fd, dc->data, n) != n) die("read fail: data chunk");
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

	// load data blocks into ram and resolve pointers
	setup_data(s);

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
			if (!strcmp(s->gname[n],"_hexout_i")) gentry[n] = MAGIC_HEXOUT;
			if (!strcmp(s->gname[n],"_hexout_u")) gentry[n] = MAGIC_HEXOUT;
			if (!strcmp(s->gname[n],"__new")) gentry[n] = MAGIC_ALLOC;
		}
	}

	for (n = 0; n < s->pcmax; n++) {
		Inst *i = s->code + n;
		uint32_t op = i->op & INS_OP_MASK;
		// convert CALLs from global id to instruction id
		// translate CALLs to undef fns to MAGICs
		if (op == INS_CALL) {
			if (i->a >= s->gmax) die("bad call");
			if (gentry[i->a] < 0) {
				i->op = INS_MAGIC;
			}
			i->a = gentry[i->a];
		}
		// resolve cdata references to addresses
		if (op == INS_CDATA) {
			DataChunk *dc = dc_find(i->b);
			i->b = dc->addr + i->c;
		}
		if (op == INS_SET) {
			// transform for the sake of tracing clarity
			i->op = INS_SET | INF_SET_A | INF_USE_C;
		}
	}
	for (n = 0; n < s->gmax; n++) {
		if (gentry[n] == MAGIC_UNDEF) {
			die("undefined reference to '%s'", s->gname[n]);
		}
	}
	if (start == 0xffffffff) die("no start function");
	return start;
}

void check_state(State *s) {
	int fail = 0;
	char line[256];
	while (fgets(line, sizeof(line), stdin) != NULL) {
		if (line[0] == 'M') {
			uint32_t addr;
			uint32_t val;
			if (sscanf(line, "M %x %x", &addr, &val) == 2) {
				uint32_t actual = memrd(s, INF_SZ_U32, addr);
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

int main(int argc, char **argv) {
	State *s = calloc(1, sizeof(State));
	s->code = malloc(1 * 1024 * 1024);
	s->vregs = malloc(4096 * sizeof(RegPair));
	s->data = malloc(RAMSIZE);
	s->vlast = s->vregs + 4096;
	s->vr = s->vregs;
	s->vrmax = 0;
	s->pr[R_SP] = 1024*1024;
	while (argc > 2) {
		if (!strcmp(argv[1], "-t")) {
			do_trace = 1;
		} else if (!strcmp(argv[1], "-check")) {
			do_check_state = 1;
		} else {
			goto usage;
		}
		argc--;
		argv++;
	}
	if (argc != 2) {
usage:
		fprintf(stderr, "error: usage: iremu [-t|-check] <xir>\n");
		return -1;
	}
	uint32_t entry = load(s, argv[1]);
	emu(s, entry);
	printf("X %08x\n", s->pr[R_RV]);
	if (do_check_state) {
		check_state(s);
	}
	return 0;
}

