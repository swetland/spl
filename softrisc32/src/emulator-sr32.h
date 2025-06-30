// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#pragma once

#include <stdint.h>

typedef struct {
	int32_t r[32];
	uint32_t pc;
	uint32_t xpc;
	uint32_t flags;
} CpuState;

#define F_TRACE_FETCH 1
#define F_TRACE_REGS  2
#define F_TRACE_BRANCH 4
#define F_TRACE_IO 8

uint32_t mem_rd32(uint32_t addr);
uint32_t mem_rd16(uint32_t addr);
uint32_t mem_rd8(uint32_t addr);
void mem_wr32(uint32_t addr, uint32_t val);
void mem_wr16(uint32_t addr, uint32_t val);
void mem_wr8(uint32_t addr, uint32_t val);

uint32_t io_rd32(CpuState *s, uint32_t addr);
void io_wr32(CpuState *s, uint32_t addr, uint32_t val);

void do_syscall(CpuState *s, uint32_t n);
void do_undef(CpuState *s, uint32_t ins);

void sr32core(CpuState *s);
