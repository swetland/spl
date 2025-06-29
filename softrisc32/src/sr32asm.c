// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "sr32.h"

#define RBUFSIZE 4096
#define SMAXSIZE 1024

static unsigned linenumber = 0;
static char linestring[256];
static char *filename;

FILE *ofp = 0;

void die(const char *fmt, ...) {
	va_list ap;
	fprintf(stderr,"\n%s:%d: ", filename, linenumber);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr,"\n");
	if (linestring[0])
		fprintf(stderr,"%s:%d: >> %s <<\n", filename, linenumber, linestring);
	exit(1);
}

int is_signed16(uint32_t n) {
	n &= 0xFFFF0000;
	return ((n == 0) || (n == 0xFFFF0000));
}
int is_signed21(uint32_t n) {
	n &= 0xFFFFF800;
	return ((n == 0) || (n == 0xFFFFF800));
}

uint32_t image[1*1024*1024];
uint32_t image_base = 0;
uint32_t image_size = 0;
uint32_t PC = 0;

void wr32(uint32_t addr, uint32_t val) {
	addr -= image_base;
	if (addr < image_size) {
		image[addr >> 2] = val;
	}
}
uint32_t rd32(uint32_t addr) {
	addr -= image_base;
	if (addr < image_size) {
		return image[addr >> 2];
	}
	return 0;
}

#define TYPE_PCREL_S16	1
#define TYPE_PCREL_S21	2
#define TYPE_ABS_U32	3
#define TYPE_LI_U32     4

struct fixup {
	struct fixup *next;
	unsigned pc;
	unsigned type;
};

struct label {
	struct label *next;
	struct fixup *fixups;
	const char *name;
	unsigned pc;
	unsigned defined;
};

struct label *labels;
struct fixup *fixups;

uint32_t do_fixup(const char *name, uint32_t addr, uint32_t tgt, int type) {
	uint32_t n = tgt;
	//fprintf(stderr,"FIXUP '%s' @%08x val=%08x typ=%d\n", name, addr, n, type);
	switch(type) {
	case TYPE_PCREL_S16:
		n = n - (addr + 4);
		if (!is_signed16(n)) goto oops;
		wr32(addr, rd32(addr) | (n << 16));
		break;
	case TYPE_PCREL_S21:
		n = n - (addr + 4);
		if (!is_signed21(n)) goto oops;
		wr32(addr, rd32(addr) | (n << 11));
		break;
	case TYPE_ABS_U32:
		wr32(addr, n);
		break;
	case TYPE_LI_U32:
		wr32((addr + 0), rd32(addr + 0) | (n & 0xffff0000));
		wr32((addr + 4), rd32(addr + 4) | (n & 0x0000ffff) << 16);
		break;
	default:
		die("unknown branch type %d\n", type);
	}
	//fprintf(stderr,"FIXUP %08x (%d)\n", n, n);
	return n;
oops:
	die("label '%s' at %08x is out of range of %08x\n", name, tgt, addr);
	return 0;
}

void setlabel(const char *name, unsigned pc) {
	struct label *l;
	struct fixup *f;

	for (l = labels; l; l = l->next) {
		if (!strcasecmp(l->name, name)) {
			if (l->defined) die("cannot redefine '%s'", name);
			l->pc = pc;
			l->defined = 1;
			for (f = l->fixups; f; f = f->next) {
				do_fixup(name, f->pc, l->pc, f->type);
			}
			return;
		}
	}
	l = malloc(sizeof(*l));
	l->name = name;
	l->pc = pc;
	l->fixups = 0;
	l->defined = 1;
	l->next = labels;
	labels = l;
}

const char *getlabel(unsigned pc) {
	struct label *l;
	for (l = labels; l; l = l->next)
		if (l->pc == pc)
			return l->name;
	return 0;
}

uint32_t uselabel(const char *name, unsigned pc, unsigned type) {
	struct label *l;
	struct fixup *f;

	for (l = labels; l; l = l->next) {
		if (!strcmp(l->name, name)) {
			if (l->defined) {
				return do_fixup(name, pc, l->pc, type);
			} else {
				goto add_fixup;
			}
		}
	}
	l = malloc(sizeof(*l));
	l->name = strdup(name);
	l->pc = 0;
	l->fixups = 0;
	l->defined = 0;
	l->next = labels;
	labels = l;
add_fixup:
	f = malloc(sizeof(*f));
	f->pc = pc;
	f->type = type;
	f->next = l->fixups;
	l->fixups = f;
	return 0;
}

void checklabels(void) {
	struct label *l;
	for (l = labels; l; l = l->next) {
		if (!l->defined) {
			die("undefined label '%s'", l->name);
		}
	}
}

void sr32dis(uint32_t pc, uint32_t ins, char *out);

void emit(uint32_t instr) {
	//fprintf(stderr,"{%08x:%08x} ", PC,instr);
	wr32(PC, instr);
	PC += 4;
}

void save(const char *fn) {
	const char *name;
	uint32_t n;
	char dis[128];

	FILE *fp = fopen(fn, "w");
	if (!fp) die("cannot write to '%s'", fn);
	for (n = image_base; n < PC; n += 4) {
		uint32_t ins = rd32(n);
		sr32dis(n, ins, dis);
		name = getlabel(n);
		char bs[8] = "000000 ";
		for (unsigned i = 0; i < 6; i++) {
			if (ins & (1<<i)) bs[5-i] = '1';
		}
		if (name) {
			fprintf(fp, "%08x: %08x // %s %-25s <- %s\n", n, ins, bs, dis, name);
		} else {
			fprintf(fp, "%08x: %08x // %s %s\n", n, ins, bs, dis);
		}
	}
	fclose(fp);
}

enum tokens {
	tEOF, tEOL, tIDENT, tREGISTER, tNUMBER, tSTRING,
	tCOMMA, tCOLON, tOPAREN, tCPAREN, tDOT, tHASH,
	tADD, tSUB, tAND, tOR, tXOR, tSLL, tSRL, tSRA,
	tSLT, tSLTU, tMUL, tDIV,
	tADDI, tSUBI, tANDI, tORI, tXORI, tSLLI, tSRLI, tSRAI,
	tSLTI, tSLTUI, tMULI, tDIVI,
	tJALR,
	tBEQ, tBNE, tBLT, tBLTU, tBGE, tBGEU,
	tLDW, tLDH, tLDB, tLDX, tLUI, tLDHU, tLDBU, tAUIPC,
	tSTW, tSTH, tSTB, tSTX,
	tJAL, tSYSCALL, tBREAK, tSYSRET,
	tNOP, tMV, tLI, tJ, tJR, tRET,
	tEQU, tBYTE, tHALF, tWORD,
	NUMTOKENS,
};

char *tnames[] = { "<EOF>", "<EOL>", "IDENT", "REGISTER", "NUMBER", "STRING",
	",", ":", "(", ")", ".", "#",
	"ADD", "SUB", "AND", "OR", "XOR", "SLL", "SRL", "SRA",
	"SLT", "SLTU", "MUL", "DIV",
	"ADDI", "SUBI", "ANDI", "ORI", "XORI", "SLLI", "SRLI", "SRAI",
	"SLTI", "SLTUI", "MULI", "DIVI",
	"JALR",
	"BEQ", "BNE", "BLT", "BLTU", "BGE", "BGEU",
	"LDW", "LDH", "LDB", "LDX", "LUI", "LDHU", "LDBU", "AUIPC",
	"STW", "STH", "STB", "STX",
	"JAL", "SYSCALL", "BREAK", "SYSRET",
	"NOP", "MV", "LI", "J", "JR", "RET",
	"EQU", "BYTE", "HALF", "WORD",
};

static_assert(NUMTOKENS == (sizeof(tnames) / sizeof(tnames[0])),
	"length of tokens and tnames must be equal");

#define FIRSTKEYWORD tADD

int is_stopchar(unsigned x) {
	switch (x) {
	case 0: case ' ': case '\t': case '\r': case '\n':
	case '.': case ',': case ':': case ';':
	case '#': case '"': case '/': case '(': case ')':
		return 1;
	default:
		return 0;
	}
}

typedef struct State {
	char *next;
	unsigned avail;
	unsigned tok;
	unsigned num;
	char *str;
	int fd;
	char rbuf[RBUFSIZE];
	char sbuf[SMAXSIZE + 1];
} State;

static char* rnames[] = {
	"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
	"x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
	"x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
	"x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31",
	"zero", "ra", "sp",
};

// may be called once after nextchar
void pushback(State *state, unsigned ch) {
	state->avail++;
	state->next--;
	*(state->next) = ch;
}

unsigned nextchar(State *state) {
	for (;;) {
		if (state->avail) {
			state->avail--;
			return *(state->next)++;
		} else {
			if (state->fd < 0) {
				return 0;
			}
			state->next = state->rbuf;
			ssize_t n = read(state->fd, state->rbuf, RBUFSIZE);
			if (n <= 0) {
				close(state->fd);
				state->fd = -1;
				state->avail = 0;
				return 0;
			} else {
				state->avail = n;
			}
		}
	}
}

unsigned tokenize(State *state) {
	char *sbuf = state->sbuf;
	char *s = sbuf;
	unsigned x, n;

	for (;;) {
		switch (x = nextchar(state)) {
		case 0:
			return tEOF;
		case ' ': case '\t': case '\r':
			continue;
		case '\n':
			linenumber++;
			return tEOL;
		case '/':
			if (nextchar(state) != '/') {
				die("stray /");
			}
		case ';':
			for (;;) {
				x = nextchar(state);
				if ((x == '\n') || (x == 0)) {
					linenumber++;
					return tEOL;
				}
			}
		case ',': return tCOMMA;
		case ':': return tCOLON;
		case '(': return tOPAREN;
		case ')': return tCPAREN;
		case '.': return tDOT;
		case '#': return tHASH;
		case '"':
			while ((x = nextchar(state)) != '"') {
				if ((x == '\n') || (x == 0)) {
					die("unterminated string");
				}
				if ((s - sbuf) == SMAXSIZE) {
					die("string too long");
				}
				*s++ = x;
			}
			*s++ = 0;
			state->str = sbuf;
			return tIDENT;
		}
		*s++ = x;
		while (!is_stopchar(x = nextchar(state))) {
			if ((s - sbuf) == SMAXSIZE) {
				die("token too long");
			}
			*s++ = x;
		}
		*s = 0;
		pushback(state, x);
		state->str = sbuf;

		char *end = sbuf;
		n = strtoul(sbuf, &end, 0);
		if (*end == '\0') {
			state->num = n;
			return tNUMBER;
		}
		for (n = 0; n < (sizeof(rnames)/sizeof(rnames[0])); n++) {
			if (!strcasecmp(sbuf, rnames[n])) {
				state->str = rnames[n];
				state->num = n & 31;
				return tREGISTER;
			}
		}
		if (isalpha(sbuf[0])) {
			s = sbuf;
			for (n = FIRSTKEYWORD; n < NUMTOKENS; n++) {
				if (!strcasecmp(s, tnames[n])) {
					return n;
				}
			}
			while (*s) {
				if (!isalnum(*s) && (*s != '_')) {
					die("invalid character '%c' (%d) in identifier", *s, *s);
				}
				s++;
			}
			return tIDENT;
		}
		die("invalid character '%c' (%d)", s[0], s[0]);
	}
	// impossible
	return tEOF;
}

unsigned next(State *state) {
	state->num = 0;
	state->str = 0;
	state->tok = tokenize(state);
	if (state->str == 0) {
		state->str = tnames[state->tok];
	}
#if 0
	if (state->tok == tNUMBER) {
		fprintf(stderr, "#%08x ", state->num);
	} else {
		fprintf(stderr,"%s ", state->str);
	}
	if (state->tok == tEOL) fprintf(stderr,"\n");
#endif
	return state->tok;
}

void require(State *s, unsigned expected) {
	if (expected != s->tok) {
		die("expected %s, got %s", tnames[expected], tnames[s->tok]);
	}
}

void expect(State *s, unsigned expected) {
	if (expected != next(s)) {
		die("expected %s, got %s", tnames[expected], tnames[s->tok]);
	}
}

unsigned expect_reg(State *s) {
	if (next(s) != tREGISTER) {
		die("expected register, got %s", tnames[s->tok]);
	}
	return s->num;
}

uint32_t expect_num(State *s) {
	if (next(s) != tNUMBER) {
		die("expected number, got %s", tnames[s->tok]);
	}
	return s->num;
}

// next()        advance to next token
// expect...()   advance to next token and fail if wrong kind
// require...()  fail if current token is wrong kind
// allow...()    advance to next token and consume if it matches

void expect_memref(State *s, uint32_t *r, uint32_t *i) {
	if (next(s) == tNUMBER) {
		*i = s->num;
		next(s);
	} else {
		*i = 0;
	}
	require(s, tOPAREN);
	*r = expect_reg(s);
	expect(s, tCPAREN);
}

uint32_t expect_rel(State *s, unsigned type) {
	switch (next(s)) {
	case tIDENT:
		return uselabel(s->str, PC, type);
	case tDOT:
		return -4;
	default:
		die("expected address");
		return 0;
	}
}

void expect_r_c(State *s, uint32_t *one) {
	*one = expect_reg(s);
	expect(s, tCOMMA);
}
void expect_2r(State *s, uint32_t *one, uint32_t *two) {
	expect_r_c(s, one);
	*two = expect_reg(s);
}
void expect_2r_c(State *s, uint32_t *one, uint32_t *two) {
	expect_2r(s, one, two);
	expect(s, tCOMMA);
}

int parse_line(State *s) {
	uint32_t a, b, t, i, o;
	char *name;

	unsigned tok = next(s);
	if (tok == tIDENT) {
		name = strdup(s->str);
		setlabel(name, PC);
		if (next(s) != tCOLON) {
			die("unexpected '%s'\n", name);
		}
		tok = next(s);
	}
	switch (tok) {
	case tADD: case tSUB: case tAND: case tOR:
	case tXOR: case tSLL: case tSRL: case tSRA:
	case tSLT: case tSLTU:
		o = tok - tADD;
		expect_2r_c(s, &t, &a);
		b = expect_reg(s);
		emit(ins_r(0, b, a, t, o));
		break;
	case tADDI: case tSUBI: case tANDI: case tORI:
	case tXORI: case tSLLI: case tSRLI: case tSRAI:
	case tSLTI: case tSLTUI:
		o = tok - tADDI;
		expect_2r_c(s, &t, &a);
		b = expect_num(s);
		emit(ins_i(b, a, t, o));
		break;
	// todo: mul div
	case tBEQ: case tBNE: case tBLT:
	case tBLTU: case tBGE: case tBGEU:
		o = tok - tBEQ;
		expect_2r_c(s, &a, &b);
		i = expect_rel(s, TYPE_PCREL_S16);
		emit(ins_b(i, a, b, o));
		break;
	case tLDW: case tLDH: case tLDB: case tLDX:
	case tLDHU: case tLDBU:
		o = tok - tLDW;
		expect_r_c(s, &t);
		expect_memref(s, &a, &i);
		emit(ins_l(i, a, t, o));
		break;
	case tLUI:
 	case tAUIPC:
		o = tok - tLDW;
		expect_r_c(s, &t);
		i = expect_num(s);
		emit(ins_l(i >> 16, 0, t, o));
		break;
	case tSTW: case tSTH: case tSTB: case tSTX:
		o = tok - tSTW;
		expect_r_c(s, &b);
		expect_memref(s, &a, &i);
		emit(ins_s(i, a, b, o));
		break;
	case tJAL:
		expect_r_c(s, &t);
		i = expect_rel(s, TYPE_PCREL_S21);
		emit(ins_j(i, t, J_JAL));
		break;
	case tSYSCALL:
		i = expect_num(s);
		emit(ins_j(i, 0, J_SYSCALL));
		break;
	case tBREAK:
		emit(ins_j(0, 0, J_BREAK));
		break;
	case tSYSRET:
		emit(ins_j(0, 0, J_SYSRET));
		break;
	case tJALR:
		expect_2r_c(s, &t, &a);
		if (s->tok == tNUMBER) {
			emit(ins_i(s->num, a, t, IR_JALR));
		} else if (s->tok == tREGISTER) {
			emit(ins_r(0, s->num, a, t, IR_JALR));
		} else {
			die("expected register or immediate");
		}
		break;
	case tJR:
		a = expect_reg(s);
		emit(ins_r(0, 0, a, 0, IR_JALR));
		break;
	case tRET:
		emit(ins_r(0, 0, 1, 0, IR_JALR));
		break;
	case tNOP:
		emit(ins_i(0, 0, 0, IR_ADD));
		break;
	case tMV:
		expect_2r(s, &t, &a);
		emit(ins_r(0, 0, a, t, IR_ADD));
		break;
	case tLI:
		expect_r_c(s, &t);
		i = expect_num(s);
		if (is_signed16(i)) {
			emit(ins_i(i, 0, t, IR_ADD));
		} else {
			uint32_t hi = i >> 16;
			if (i & 0x8000) {
				hi += 1;
			}
			emit(ins_l(hi, 0, t, L_LUI));
			if (i & 0xFFFF) {
				emit(ins_i(i & 0xFFFF, t, t, IR_ADD));
			}
		}
		break;
	case tJ:
		i = expect_rel(s, TYPE_PCREL_S21);
		emit(ins_j(i, 0, J_JAL));
		break;
	case tEQU:
		expect(s, tIDENT);
		name = strdup(s->str);
		setlabel(name, expect_num(s));
		break;

	case tWORD:
		do {
			switch (next(s)) {
			case tNUMBER:
				emit(s->num);
				break;
			case tIDENT:
				emit(0);
				uselabel(s->str, PC - 4, TYPE_ABS_U32);
				break;
			default:
				die("expected constant or symbol");
			}
		} while (next(s) == tCOMMA);
		goto end_of_line;

	// todo: BYTE, HALF, string data
	case tEOL:
		return 1;
	}

	next(s);
end_of_line:
	switch (s->tok) {
	case tEOF:
		return 0;
	case tEOL:
		return 1;
	default:
		die("unexpected %s\n", tnames[tok]);
		return 0;
	}
}

void assemble(const char *fn) {
	State state;
	memset(&state, 0, sizeof(state));
	state.fd = open(fn, O_RDONLY);
	if (state.fd < 0) {
		die("cannot open '%s'", fn);
	}
	state.next = state.sbuf;
	linenumber = 1;
	while (parse_line(&state)) ;
}

int main(int argc, char **argv) {
	const char *outname = "out.hex";
	filename = argv[1];

	image_base = 0x100000;
	image_size = sizeof(image);
	PC = image_base;

	if (argc < 2)
		die("no file specified");
	if (argc == 3)
		outname = argv[2];

	assemble(filename);
	linestring[0] = 0;
	checklabels();
	save(outname);
	return 0;
}
