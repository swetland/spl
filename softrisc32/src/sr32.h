// Copyright 2025, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#pragma once
#include <stdint.h>

#define IR_ADD 0
#define IR_SUB 1
#define IR_AND 2
#define IR_OR 3
#define IR_XOR 4
#define IR_SLL 5
#define IR_SRL 6
#define IR_SRA 7
#define IR_SLT 8
#define IR_SLTU 9
#define IR_MUL 10
#define IR_DIV 11
#define IR_JALR 15

#define B_BEQ 0
#define B_BNE 1
#define B_BLT 2
#define B_BLTU 3
#define B_BGE 4
#define B_BGEU 5

#define L_LDW 0
#define L_LDH 1
#define L_LDB 2
#define L_LDX 3
#define L_LUI 4
#define L_LDHU 5
#define L_LDBU 6
#define L_AUIPC 7

#define S_STW 0
#define S_STH 1
#define S_STB 2
#define S_STX 3

#define J_JAL 0
#define J_SYSCALL 1
#define J_BREAK 2
#define J_SYSRET 3

static inline uint32_t ins_i(uint32_t o, uint32_t t, uint32_t a, uint32_t i) {
	return (i << 16) | ((a & 0x1F) << 11) | ((t & 0x1F) << 6) | (o & 0xF);
}
static inline uint32_t ins_r(uint32_t o, uint32_t t, uint32_t a, uint32_t b, uint32_t n) {
	return ((n & 7) << 21) | ((b & 0x1F) << 16) | ((a & 0x1F) << 11) | ((t & 0x1F) << 6) | (o & 0xF) | 0x10;
}
static inline uint32_t ins_l(uint32_t o, uint32_t t, uint32_t a, uint32_t i) {
	return (i << 16) | ((a & 0x1F) << 11) | ((t & 0x1F) << 6) | (o & 7) | 0x20;
}
static inline uint32_t ins_s(uint32_t o, uint32_t b, uint32_t a, uint32_t i) {
	return (i << 16) | ((a & 0x1F) << 11) | ((b & 0x1F) << 6) | (o & 7) | 0x28;
}
static inline uint32_t ins_b(uint32_t o, uint32_t a, uint32_t b, uint32_t i) {
	return (i << 16) | ((a & 0x1F) << 11) | ((b & 0x1F) << 6) | (o & 7) | 0x30;
}
static inline uint32_t ins_j(uint32_t o, uint32_t t, uint32_t i) {
	return (i << 11) | ((t & 0x1F) << 6) | (o & 7) | 0x38;
}

static inline uint32_t get_i16(uint32_t ins) {
	return ((int32_t) ins) >> 16;
}
static inline uint32_t get_i21(uint32_t ins) {
	return ((int32_t) ins) >> 11;
}
static inline uint32_t get_rt(uint32_t ins) {
	return (ins >> 6) & 0x1F;
}
static inline uint32_t get_ra(uint32_t ins) {
	return (ins >> 11) & 0x1F;
}
static inline uint32_t get_rb(uint32_t ins) {
	return (ins >> 16) & 0x1F;
}

void sr32dis(uint32_t pc, uint32_t ins, char *out);
