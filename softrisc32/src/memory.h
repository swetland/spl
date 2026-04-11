// Copyright 2026, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#pragma once

#include <stdint.h>

#define MEM_RD 1
#define MEM_WR 2
#define MEM_RW 3

void *mem_init();

void mem_protect(void *mem, uint32_t addr, uint32_t length, uint32_t flags);

typedef void mem_try_fn(void *cookie);

int mem_try(mem_try_fn, void *cookie, uint32_t *fault_addr);

int mem_rd32_safe(void *mem, uint32_t addr, uint32_t *value);

static inline uint32_t mem_rd32(void *mem, uint32_t addr) {
	return *((uint32_t*) (mem + addr));
}
static inline uint32_t mem_rd16(void *mem, uint32_t addr) {
	return *((uint16_t*) (mem + addr));
}
static inline uint32_t mem_rd8(void *mem, uint32_t addr) {
	return *((uint8_t*) (mem + addr));
}

static inline void mem_wr32(void *mem, uint32_t addr, uint32_t val) {
	*((uint32_t*) (mem + addr)) = val;
}
static inline void mem_wr16(void *mem, uint32_t addr, uint32_t val) {
	*((uint16_t*) (mem + addr)) = val;
}
static inline void mem_wr8(void *mem, uint32_t addr, uint32_t val) {
	*((uint8_t*) (mem + addr)) = val;
}
