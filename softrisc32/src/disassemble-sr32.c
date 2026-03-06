// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <string.h>
#include <stdio.h>

#include "sr32.h"

static char *append_str(char *buf, const char *s) {
	while (*s) *buf++ = *s++;
	return buf;
}
static char *append_i32(char *buf, int32_t n) {
	return buf + sprintf(buf, "%d", n);
}
static char *append_u32(char *buf, int32_t n) {
	return buf + sprintf(buf, "0x%x", n);
}

static const char* regname[32] = {
#if RISCV_STYLE
	"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
	"x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
	"x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
	"x24", "x25", "x26", "x27", "x28", "x29", "x30", "x31",
#else
	"zero", "ra", "sp", "gp", "tp", "rv", "rw", "fp",
	"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
	"t8", "t9", "t10", "t11", "t12", "t13", "t14", "t15",
	"t16", "t17", "t18", "t19", "t20", "t21", "t22", "t23",
#endif
};

typedef struct {
	uint32_t mask;
	uint32_t bits;
	const char* fmt;
} sr32ins_t;

static sr32ins_t instab[] = {
#include <instab-sr32.h>
};

static char* append_addr(char *out, uint32_t n, const char* name) {
	if (name) {
		out = append_str(out, name);
		out = append_str(out, " <");
	}
	out = append_u32(out, n);
	if (name) {
		out = append_str(out, ">");
	}
	return out;
}

void sr32dis(uint32_t pc, uint32_t ins, char *out, const char* (lookup)(uint32_t)) {
	unsigned n = 0;
	while ((ins & instab[n].mask) != instab[n].bits) n++;
	const char* fmt = instab[n].fmt;
	char c;
	while ((c = *fmt++) != 0) {
		if (c != '%') {
			*out++ = c;
			continue;
		}
		switch (*fmt++) {
		case 'a': out = append_str(out, regname[get_ra(ins)]); break;
		case 'b': out = append_str(out, regname[get_rb(ins)]); break;
		case 't': out = append_str(out, regname[get_rt(ins)]); break;
		case 'i': out = append_i32(out, get_i16(ins)); break;
		case 'u': out = append_u32(out, get_i16(ins)); break;
		case 'j': out = append_i32(out, get_i21(ins)); break;
		case 's': out = append_i32(out, get_rb(ins)); break;
		case 'J': {
			uint32_t n = pc + 4 + get_i21(ins);
			out = append_addr(out, n, lookup(n));
			break;
		}
		case 'B': {
			uint32_t n = pc + 4 + get_i16(ins);
			out = append_addr(out, n, lookup(n));
			break;
		}
		case 'U': out = append_u32(out, get_i16(ins) << 16); break;
		}
	}
	*out = 0;
}
