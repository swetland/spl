// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <emulator-sr32.h>

#define WITH_TRACE 1
#define WITH_EXEC_COUNT 1

#define EXEC_COUNT_RANGE (1024*1024)
#define EXEC_COUNT_MASK (EXEC_COUNT_RANGE - 1)

#if WITH_EXEC_COUNT
uint32_t exec_count[EXEC_COUNT_RANGE / 4];
#endif

uint32_t get_exec_count(uint32_t addr) {
#if WITH_EXEC_COUNT
	return exec_count[(addr & EXEC_COUNT_MASK) >> 2];
#else
	return 0;
#endif
}

void sr32core(CpuState *s) {
	void *mem = s->mem;
	int32_t a, b, n;
	uint32_t pc = s->pc;
	for (;;) {
	int32_t ins = mem_rd32(mem, pc);
#if WITH_EXEC_COUNT
	exec_count[(pc & EXEC_COUNT_MASK) >> 2]++;
#endif
#if WITH_TRACE
	if (s->flags & F_TRACE_FETCH) {
		fprintf(stderr,"%08x %08x\n", pc, ins);
	}
#endif
	s->pc = pc;
	pc += 4;
	switch ((ins >> 3) & 7) {
	case 0b000:
	case 0b001:
	case 0b010:
	case 0b011: // I/R
		a = s->r[(ins >> 11) & 31];
		b = ins >> 16;
		if (ins & 0b010000) b = s->r[b & 31];
		switch (ins & 15) {
		case 0x0: n = a + b; break;
		case 0x1: n = a - b; break;
		case 0x2: n = a & b; break;
		case 0x3: n = a | b; break;
		case 0x4: n = a ^ b; break;
		case 0x5: n = a << (b & 31); break;
		case 0x6: n = ((uint32_t)a) >> (b & 31); break;
		case 0x7: n = a >> (b & 31); break;
		case 0x8: n = (a < b) ? 1 : 0; break;
		case 0x9: n = (((uint32_t)a) < ((uint32_t)b)) ? 1 : 0; break;
		case 0xa: n = a * b; break;
		case 0xb: {
			if ((ins & 0x200010) == 0x200010) {
				n = a % b;
			} else {
				n = a / b;
			}
			break;
		}
		case 0xf: n = pc; pc = a + b; break;
		default: goto undef;
		}
		b = (ins >> 6) & 31;
		if (b) {
			s->r[b] = n;
#if WITH_TRACE
			if (s->flags & F_TRACE_REGS) {
				fprintf(stderr,"%08x -> X%d\n", n, b);
			}
#endif
		}
		break;
	case 0b100: // L
		a = s->r[(ins >> 11) & 31] + (ins >> 16);
		switch (ins & 7) {
		case 0: n = mem_rd32(mem, a); break;
		case 1: n = mem_rd16(mem, a); if (n & 0x8000) n |= 0xFFFF0000; break;
		case 2: n = mem_rd8(mem, a); if (n & 0x80) n |= 0xFFFFFF00; break;
		case 3: n = io_rd32(s, a); break;
		case 4: n = ins & 0xFFFF0000; break;
		case 5: n = mem_rd16(mem, a); break;
		case 6: n = mem_rd8(mem, a); break;
		case 7: n = pc + (ins & 0xFFFF0000); break;
		}
		b = (ins >> 6) & 31;
		if (b) {
			s->r[b] = n;
#if WITH_TRACE
			if (s->flags & F_TRACE_REGS) {
				fprintf(stderr,"%08x -> X%d\n", n, b);
			}
#endif
		}
		break;
	case 0b101: // S
		a = s->r[(ins >> 11) & 31] + (ins >> 16);
		b = s->r[(ins >> 6) & 31];
		switch (ins & 7) {
		case 0: mem_wr32(mem, a, b); break;
		case 1: mem_wr16(mem, a, b); break;
		case 2: mem_wr8(mem, a, b); break;
		case 3:	io_wr32(s, a, b); break;
		default: goto undef;
		}
		break;
	case 0b110: // B
		a = s->r[(ins >> 11) & 31];
		b = s->r[(ins >> 6) & 31];
		switch (ins & 7) {
		case 0: n = (a == b); break;
		case 1: n = (a != b); break;
		case 2: n = (a < b); break;
		case 3: n = (((uint32_t)a) < ((uint32_t)b)); break;
		case 4: n = (a >= b); break;
		case 5: n = (((uint32_t)a) >= ((uint32_t)b)); break;
		default: goto undef;
		}
		if (n) pc = pc + (ins >> 16);
		break;
	case 0b111: // J
		switch (ins & 7) {
		case 0:
			a = ins >> 11;
			b = (ins >> 6) & 31;
			if (b) s->r[b] = pc;
			pc = pc + a;
			break;
		case 1: s->pc = pc; s->syscall(s, ins >> 11); break;
		//case 1: s->xpc = pc; pc = s->vec_syscall; break;
		//case 2: s->xpc = pc; pc = s->vec_break; break;
		//case 3: pc = s->xpc;
		default: /* undefined instruction */
undef:
		s->pc = pc;
		do_undef(s, ins);
		return;
		//s->xpc = pc; pc = s->vec_undef;
		}
		break;
	}
	}
}

