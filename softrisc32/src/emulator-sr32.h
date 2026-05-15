// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#pragma once

#include <stdint.h>
#include <memory.h>

typedef struct CpuState CpuState;

struct CpuState {
	int32_t r[32];
	volatile uint32_t pc;
	void *mem;
	uint32_t xpc;
	uint32_t flags;
	void (*syscall)(CpuState *s, uint32_t n);
};

#define F_TRACE_FETCH 1
#define F_TRACE_REGS  2
#define F_TRACE_BRANCH 4
#define F_TRACE_IO 8

uint32_t io_rd32(CpuState *s, uint32_t addr);
void io_wr32(CpuState *s, uint32_t addr, uint32_t val);

void do_undef(CpuState *s, uint32_t ins);

void sr32core(CpuState *s);
