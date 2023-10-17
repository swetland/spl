// Copyright 2022, Brian Swetland <swetland@frotz.net>
// Licensed under the Apache License, Version 2.0.

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

// builtin types
#define nil 0
typedef uint32_t u32;
typedef int32_t i32;
typedef uint8_t u8;

typedef uint32_t token_t;

void error(const char *fmt, ...);

// ------------------------------------------------------------------
// structures

typedef struct String String;
typedef struct Symbol Symbol;
typedef struct Scope Scope;
typedef struct Type Type;

struct String {
	String *next;
	u32 len;
	char text[0];
};

struct Symbol {
	Symbol *next;
	String *name;
	Type *type;
	u32 kind;
};
enum {
	SYMBOL_VAR,
	SYMBOL_FLD, // struct field
	SYMBOL_PTR, // struct *field
	SYMBOL_DEF, // enum
	SYMBOL_FN,
};

struct Scope {
	Scope *parent;
	Symbol *first;
	Symbol *last;
	u32 kind;
};
enum {
	SCOPE_GLOBAL,
	SCOPE_FUNC,
	SCOPE_BLOCK,
	SCOPE_LOOP,
	SCOPE_STRUCT,
};

struct Type {
	Type *next;
	String *name;
	Type *of;        // for: slice, array, ptr
	Symbol *fields;  // for: struct
	u32 kind;
	u32 count;       // for: arrays
};
enum {
	TYPE_VOID,
	TYPE_BOOL,
	TYPE_U8,
	TYPE_U32,
//	TYPE_NIL,
//	TYPE_POINTER,
	TYPE_ARRAY,
	TYPE_SLICE,
	TYPE_STR,
	TYPE_STRUCT,
//	TYPE_FUNC,
	TYPE_ENUM,
	TYPE_UNDEFINED,
};

typedef struct Ctx Ctx;

// ------------------------------------------------------------------
// compiler global context

struct Ctx {
	const char* filename;  // filename of active source
	const char* outname;   // base name for output files
	int fd;

	FILE *fp_decl;         // output files
	FILE *fp_type;
	FILE *fp_impl;

	int nl_decl;           // flag to update #line
	int nl_type;
	int nl_impl;

	u8 iobuffer[1024];     // scanner file io buffer
	u32 ionext;
	u32 iolast;

	u32 linenumber;        // line number of most recent line
	u32 lineoffset;        // position of start of most recent line
	u32 byteoffset;        // position of the most recent character
	u32 flags;
	u32 cc;                // scanner: next character

	token_t tok;           // most recent token
	u32 num;               // used for tNUM
	char tmp[256];         // used for tIDN, tSTR;
	String *ident;         // used for tIDN

	String *stringlist;    // all strings
	Type *typelist;        // all types

	Scope *scope;          // scope stack
	Scope *fn;             // args of active function being parsed

	Scope global;

	String *idn_if;        // identifier strings
	String *idn_fn;
	String *idn_for;
	String *idn_var;
	String *idn_nil;
	String *idn_new;
	String *idn_case;
	String *idn_else;
	String *idn_enum;
	String *idn_true;
	String *idn_break;
	String *idn_while;
	String *idn_false;
	String *idn_switch;
	String *idn_struct;
	String *idn_return;
	String *idn_continue;

	Type *type_void;       // base types
	Type *type_str;
	Type *type_u32;
	Type *type_i32;
	Type *type_u8;

	char *outptr;
	char outbuf[4096];
};

Ctx ctx;

// ------------------------------------------------------------------

String *string_make(const char* text, u32 len) {
	// obviously this wants to be a hash table
	String *str = ctx.stringlist;
	while (str != nil) {
		if ((str->len == len) && (memcmp(text, str->text, len) == 0)) {
			return str;
		}
		str = str->next;
	}

	str = malloc(sizeof(String) + len + 1);
	str->len = len;
	memcpy(str->text, text, len);
	str->text[len] = 0;
	str->next = ctx.stringlist;
	ctx.stringlist = str;

	return str;
}

Scope *scope_push(u32 kind) {
	Scope *scope = malloc(sizeof(Scope));
	scope->first = nil;
	scope->last = nil;
	scope->parent = ctx.scope;
	scope->kind = kind;
	ctx.scope = scope;
	return scope;
}

Scope *scope_pop(void) {
	Scope *scope = ctx.scope;
	ctx.scope = scope->parent;
	return scope;
}

Scope *scope_find(u32 scope_kind) {
	Scope *scope = ctx.scope;
	while (scope != nil) {
		if (scope->kind == scope_kind) {
			return scope;
		}
		scope = scope->parent;
	}
	return nil;
}

Symbol *symbol_find_in(String *name, Scope *scope) {
	for (Symbol *sym = scope->first; sym != nil; sym = sym->next) {
		if (sym->name == name) {
			return sym;
		}
	}
	return nil;
}

// find the first surrounding scope of a specified kind
Symbol *symbol_find(String *name) {
	for (Scope *scope = ctx.scope; scope != nil; scope = scope->parent) {
		Symbol *sym = symbol_find_in(name, scope);
		if (sym != nil) {
			return sym;
		}
	}
	return nil;
}

Symbol *symbol_make_in_scope(String *name, Type *type, Scope *scope) {
	Symbol *sym = malloc(sizeof(Symbol));
	sym->name = name;
	sym->type = type;
	sym->next = nil;
	sym->kind = SYMBOL_VAR;
	if (scope->first == nil) {
		scope->first = sym;
	} else {
		scope->last->next = sym;
	}
	scope->last = sym;
	return sym;
}

Symbol *symbol_make_global(String *name, Type *type) {
	return symbol_make_in_scope(name, type, &ctx.global);
}

Symbol *symbol_make(String *name, Type *type) {
	return symbol_make_in_scope(name, type, ctx.scope);
}

Type *type_make(String *name, u32 kind, Type *of, Symbol *fields, u32 count) {
	Type *type = malloc(sizeof(Type));
	type->name = name;
	type->of = of;
	type->fields = fields;
	type->kind = kind;
	type->count = count;
	if (name != nil) {
		type->next = ctx.typelist;
		ctx.typelist = type;
	} else {
		type->next = nil;
	}
	return type;
}

Type *type_find(String *name) {
	for (Type *t = ctx.typelist; t != nil; t = t->next) {
		if (t->name == name) {
			return t;
		}
	}
	return nil;
}

Symbol *type_find_field(Type *type, String *name) {
	if (type->kind != TYPE_STRUCT) {
		error("not a struct");
	}
	for (Symbol *s = type->fields; s != nil; s = s->next) {
		if (s->name == name) {
			return s;
		}
	}
	error("struct has no such field '%s'", name->text);
	return nil;
}

// ================================================================

enum {
	cfVisibleEOL   = 1,
	cfAbortOnError = 2,
	cfTraceCodeGen = 3,
};

void ctx_init() {
	memset(&ctx, 0, sizeof(ctx));

	// pre-intern keywords
	ctx.idn_if       = string_make("if", 2);
	ctx.idn_fn       = string_make("fn", 2);
	ctx.idn_for      = string_make("for", 3);
	ctx.idn_var      = string_make("var", 3);
	ctx.idn_nil      = string_make("nil", 3);
	ctx.idn_new      = string_make("new", 3);
	ctx.idn_case     = string_make("case", 4);
	ctx.idn_else     = string_make("else", 4);
	ctx.idn_enum     = string_make("enum", 4);
	ctx.idn_true     = string_make("true", 4);
	ctx.idn_break    = string_make("break", 5);
	ctx.idn_while    = string_make("while", 5);
	ctx.idn_false    = string_make("false", 5);
	ctx.idn_switch   = string_make("switch", 6);
	ctx.idn_struct   = string_make("struct", 6);
	ctx.idn_return   = string_make("return", 6);
	ctx.idn_continue = string_make("continue", 8);

	ctx.type_void    = type_make(string_make("void", 4), TYPE_VOID, nil, nil, 0);
	ctx.type_u32     = type_make(string_make("str", 3), TYPE_STR, nil, nil, 0);
	ctx.type_u32     = type_make(string_make("u32", 3), TYPE_U32, nil, nil, 0);
	ctx.type_i32     = type_make(string_make("i32", 3), TYPE_U32, nil, nil, 0);
	ctx.type_u8      = type_make(string_make("u8", 2), TYPE_U8, nil, nil, 0);

	ctx.scope = &(ctx.global);

	ctx.outptr = ctx.outbuf;
}

void dump_file_line(const char* fn, u32 offset);
void dump_error_ctxt();

void error(const char *fmt, ...) {
	va_list ap;

	fprintf(stderr,"\n%s:%d: ", ctx.filename, ctx.linenumber);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (ctx.linenumber > 0) {
		// dump_file_line(ctx.filename, ctx.lineoffset);
	}
	fprintf(stderr, "\n");

#if 0
	dump_error_ctxt();
#endif

	if (ctx.flags & cfAbortOnError) {
		abort();
	} else {
		exit(1);
	}
}

#define DECL ctx.fp_decl
#define TYPE ctx.fp_type
#define IMPL ctx.fp_impl

void emit(FILE* fp, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
}

int indent = 0;

void emit_impl(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(ctx.outptr, sizeof(ctx.outbuf) - (ctx.outptr - ctx.outbuf), fmt, ap);
	va_end(ap);
	ctx.outptr += n;
	if (fmt[strlen(fmt) - 1] == '\n') {
		unsigned len = ctx.outptr - ctx.outbuf;
		// any }s reduce the indent level of the current line
		for (unsigned n = 0; n < len; n++) {
			if (ctx.outbuf[n] == '}') indent--;
		}
		for (int n = 0; n < indent; n++) {
			fwrite("    ", 1, 4, ctx.fp_impl);
		}
		fwrite(ctx.outbuf, 1, len, ctx.fp_impl);
		// any {s increase the indent level of the next line
		for (unsigned n = 0; n < len; n++) {
			if (ctx.outbuf[n] == '{') indent++;
		}
		ctx.outptr = ctx.outbuf;
		ctx.outbuf[0] = 0;
	}
}

void emit_impl_str(void) {
	u32 n = 0;
	emit_impl("(void*) \"");
	while (n < 256) {
		u32 ch = ctx.tmp[n];
		if (ch == 0) {
			break;
		} else if ((ch < ' ') || (ch > '~') || (ch == '"') || (ch == '\\')) {
			emit_impl("\\x%02x", ch);
		} else {
			emit_impl("%c", ch);
		}
		n++;
	}
	emit_impl("\"");
}

#define emit_decl(fmt...) emit(DECL, fmt)
#define emit_type(fmt...) emit(TYPE, fmt)

#define KEEP_PARENS 0x10000

unsigned emit_impl_oparen(void) {
	unsigned idx = ctx.outptr - ctx.outbuf;
	*ctx.outptr++ = '(';
	*ctx.outptr = 0;
	return idx;
}

void emit_impl_cparen(unsigned idx) {
	if (idx & KEEP_PARENS) {
		*ctx.outptr++ = ')';
		*ctx.outptr = 0;
	} else {
		unsigned len = ctx.outptr - ctx.outbuf;
		idx &= 0xFFFF;
		memmove(ctx.outbuf + idx, ctx.outbuf + idx + 1, len - idx);
		ctx.outptr--;
	}
}

void ctx_open_source(const char* filename) {
	ctx.filename = filename;
	ctx.linenumber = 0;

	if (ctx.fd >= 0) {
		close(ctx.fd);
	}
	ctx.fd = open(filename, O_RDONLY);
	if (ctx.fd < 0) {
		error("cannot open file '%s'", filename);
	}
	ctx.ionext = 0;
	ctx.iolast = 0;
	ctx.linenumber = 1;
	ctx.lineoffset = 0;
	ctx.byteoffset = 0;
}

void ctx_open_output(void) {
	char tmp[1024];
	ctx.nl_decl = 1;
	ctx.nl_type = 1;
	ctx.nl_impl = 1;

	sprintf(tmp, "%s.decl.h", ctx.outname);
	if ((ctx.fp_decl = fopen(tmp, "w")) == NULL) {
		error("cannot open output '%s'", tmp);
	}

	sprintf(tmp, "%s.type.h", ctx.outname);
	if ((ctx.fp_type = fopen(tmp, "w")) == NULL) {
		error("cannot open output '%s'", tmp);
	}

	sprintf(tmp, "%s.impl.c", ctx.outname);
	if ((ctx.fp_impl = fopen(tmp, "w")) == NULL) {
		error("cannot open output '%s'", tmp);
	}

	emit_impl("#include <builtin.type.h>\n");
	emit_impl("#include \"%s.type.h\"\n", ctx.outname);
	emit_impl("#include \"%s.decl.h\"\n", ctx.outname);
}


// ================================================================
// lexical scanner

// token classes (tok & tcMASK)
enum {
	tcRELOP = 0x08, tcADDOP = 0x10, tcMULOP = 0x18,
	tcAEQOP = 0x20, tcMEQOP = 0x28, tcMASK = 0xF8,
};

enum {
	// EndMarks, Braces, Brackets Parens
	tEOF, tEOL, tOBRACE, tCBRACE, tOBRACK, tCBRACK, tOPAREN, tCPAREN,
	// RelOps (do not reorder)
	tEQ, tNE, tLT, tLE, tGT, tGE, tx0E, tx0F,
	// AddOps (do not reorder)
	tPLUS, tMINUS, tPIPE, tCARET, tx14, tx15, tx16, tx17,
	// MulOps (do not reorder)
	tSTAR, tSLASH, tPERCENT, tAMP, tLEFT, tRIGHT, tx1E, tx1F,
	// AsnOps (do not reorder)
	tADDEQ, tSUBEQ, tOREQ, tXOREQ, tx24, tx25, tx26, tx27,
	tMULEQ, tDIVEQ, tMODEQ, tANDEQ, tLSEQ, tRSEQ, t2E, t2F,
	// Various, UnaryNot, LogicalOps,
	tSEMI, tCOLON, tDOT, tCOMMA, tNOT, tAND, tOR, tBANG,
	tASSIGN, tINC, tDEC,
	// Keywords
	tNEW, tFN, tSTRUCT, tVAR, tENUM,
	tIF, tELSE, tWHILE,
	tBREAK, tCONTINUE, tRETURN,
	tFOR, tSWITCH, tCASE,
	tTRUE, tFALSE, tNIL,
	tIDN, tNUM, tSTR,
	// used internal to the lexer but never returned
	tSPC, tINV, tDQT, tSQT, tMSC,
};

const char *tnames[] = {
	"<EOF>", "<EOL>", "{",  "}",  "[",   "]",   "(",   ")",
	"==",    "!=",    "<",  "<=", ">",   ">=",  "",    "",
	"+",     "-",     "|",  "^",  "",    "",    "",    "",
	"*",     "/",     "%",  "&",  "<<",  ">>",  "",    "",
	"+=",    "-=",    "|=", "^=", "",    "",    "",    "",
	"*=",    "/=",    "%=", "&=", "<<=", ">>=", "",    "",
	";",     ":",     ".",  ",",  "~",   "&&",  "||",  "!",
	"=",     "++",    "--",
	"new", "fn", "struct", "var", "enum",
	"if", "else", "while",
	"break", "continue", "return",
	"for", "switch", "case",
	"true", "false", "nil",
	"<ID>", "<NUM>", "<STR>",
	"<SPC>", "<INV>", "<DQT>", "<SQT>", "<MSC>",
};

u8 lextab[256] = {
	tEOF, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tSPC, tEOL, tSPC, tINV, tSPC, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tSPC, tBANG, tDQT, tMSC, tMSC, tPERCENT, tAMP, tSQT,
	tOPAREN, tCPAREN, tSTAR, tPLUS, tCOMMA, tMINUS, tDOT, tSLASH,
	tNUM, tNUM, tNUM, tNUM, tNUM, tNUM, tNUM, tNUM,
	tNUM, tNUM, tCOLON, tSEMI, tLT, tASSIGN, tGT, tMSC,
	tMSC, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN,
	tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN,
	tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN,
	tIDN, tIDN, tIDN, tOBRACK, tMSC, tCBRACK, tCARET, tIDN,
	tMSC, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN,
	tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN,
	tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN, tIDN,
	tIDN, tIDN, tIDN, tOBRACE, tPIPE, tCBRACE, tNOT, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
	tINV, tINV, tINV, tINV, tINV, tINV, tINV, tINV,
};

i32 unhex(u32 ch) {
	if ((ch >= '0') && (ch <= '9')) {
		return ch - '0';
	}
	if ((ch >= 'a') && (ch <= 'f')) {
		return ch - 'a' + 10;
	}
	if ((ch >= 'A') && (ch <= 'F')) {
		return ch - 'A' + 10;
	}
	return -1;
}

u32 scan() {
	while (ctx.ionext == ctx.iolast) {
		if (ctx.fd < 0) {
			ctx.cc = 0;
			return ctx.cc;
		}
		int r = read(ctx.fd, ctx.iobuffer, sizeof(ctx.iobuffer));
		if (r <= 0) {
			ctx.fd = -1;
		} else {
			ctx.iolast = r;
			ctx.ionext = 0;
		}
	}
	ctx.cc = ctx.iobuffer[ctx.ionext];
	ctx.ionext++;
	ctx.byteoffset++;
	return ctx.cc;
}

u32 unescape(u32 n) {
	if (n == 'n') {
		return 10;
	} else if (n == 'r') {
		return 13;
	} else if (n == 't') {
		return 9;
	} else if (n == '"') {
		return '"';
	} else if (n == '\'') {
		return '\'';
	} else if (n == '\\') {
		return '\\';
	} else if (n == 'x') {
		int x0 = unhex(scan());
		int x1 = unhex(scan());
		if ((x0 < 0) || (x1 < 0)) {
			error("invalid hex escape");
		}
		return (x0 << 4) | x1;
	} else {
		error("invalid escape 0x%02x", n);
		return 0;
	}
}

token_t scan_string(u32 cc, u32 nc) {
	u32 n = 0;
	while (true) {
		if (nc == '"') {
			nc = scan();
			break;
		} else if (nc == 0) {
			error("unterminated string");
		} else if (nc == '\\') {
			ctx.tmp[n] = unescape(scan());
		} else {
			ctx.tmp[n] = nc;
		}
		nc = scan();
		n++;
		if (n == 255) {
			error("constant string too large");
		}
	}
	ctx.tmp[n] = 0;
	return tSTR;
}

token_t scan_keyword(u32 len) {
	ctx.tmp[len] = 0;
	String *idn = string_make(ctx.tmp, len);
	ctx.ident = idn;

	if (len == 2) {
		if (idn == ctx.idn_if) { return tIF; };
		if (idn == ctx.idn_fn) { return tFN; }
	} else if (len == 3) {
		if (idn == ctx.idn_for) { return tFOR; }
		if (idn == ctx.idn_var) { return tVAR; }
		if (idn == ctx.idn_nil) { return tNIL; }
		if (idn == ctx.idn_new) { return tNEW; }
	} else if (len == 4) {
		if (idn == ctx.idn_case) { return tCASE; }
		if (idn == ctx.idn_else) { return tELSE; }
		if (idn == ctx.idn_enum) { return tENUM; }
		if (idn == ctx.idn_true) { return tTRUE; }
	} else if (len == 5) {
		if (idn == ctx.idn_break) { return tBREAK; }
		if (idn == ctx.idn_while) { return tWHILE; }
		if (idn == ctx.idn_false) { return tFALSE; }
	} else if (len == 6) {
		if (idn == ctx.idn_switch) { return tSWITCH; }
		if (idn == ctx.idn_struct) { return tSTRUCT; }
		if (idn == ctx.idn_return) { return tRETURN; }
	} else if (len == 8) {
		if (idn == ctx.idn_continue) { return tCONTINUE; }
	}
	return tIDN;
}

token_t scan_number(u32 cc, u32 nc) {
	u32 n = 1;
	u32 val = cc - '0';

	if ((cc == '0') && (nc == 'b')) { // binary
		nc = scan();
		while ((nc == '0') || (nc == '1')) {
			val = (val << 1) | (nc - '0');
			nc = scan();
			n++;
			if (n == 34) {
				error("binary constant too large");
			}
		}
	} else if ((cc == '0') && (nc == 'x')) { // hex
		nc = scan();
		while (true) {
			int tmp = unhex(nc);
			if (tmp == -1) {
				break;
			}
			val = (val << 4) | tmp;
			nc = scan();
			n++;
			if (n == 10) {
				error("hex constant too large");
			}
		}
	} else { // decimal
		while (lextab[nc] == tNUM) {
			u32 tmp = (val * 10) + (nc - '0');
			if (tmp <= val) {
				error("decimal constant too large");
			}
			val = tmp;
			nc = scan();
			n++;
		}
	}
	ctx.num = val;
	return tNUM;
}

token_t scan_ident(u32 cc, u32 nc) {
	ctx.tmp[0] = cc;
	u32 n = 1;

	while (true) {
		u32 tok = lextab[nc];
		if ((tok == tIDN) || (tok == tNUM)) {
			ctx.tmp[n] = nc;
			n++;
			if (n == 32) { error("identifier too large"); }
			nc = scan();
		} else {
			break;
		}
	}
	return scan_keyword(n);
}

token_t _next() {
	u8 nc = ctx.cc;
	while (true) {
		u8 cc = nc;
		nc = scan();
		u32 tok = lextab[cc];
		if (tok == tNUM) { // 0..9
			return scan_number(cc, nc);
		} else if (tok == tIDN) { // _ A..Z a..z
			return scan_ident(cc, nc);
		} else if (tok == tDQT) { // "
			return scan_string(cc, nc);
		} else if (tok == tSQT) { // '
			ctx.num = nc;
			if (nc == '\\') {
				ctx.num = unescape(scan());
			}
			nc = scan();
			if (nc != '\'') {
				error("unterminated character constant");
			}
			nc = scan();
			return tNUM;
		} else if (tok == tPLUS) {
			if (nc == '+') { tok = tINC; nc = scan(); }
		} else if (tok == tMINUS) {
			if (nc == '-') { tok = tDEC; nc = scan(); }
		} else if (tok == tAMP) {
			if (nc == '&') { tok = tAND; nc = scan(); }
		} else if (tok == tPIPE) {
			if (nc == '|') { tok = tOR; nc = scan(); }
		} else if (tok == tGT) {
			if (nc == '=') { tok = tGE; nc = scan(); }
			else if (nc == '>') { tok = tRIGHT; nc = scan(); }
		} else if (tok == tLT) {
			if (nc == '=') { tok = tLE; nc = scan(); }
			else if (nc == '<') { tok = tLEFT; nc = scan(); }
		} else if (tok == tASSIGN) {
			if (nc == '=') { tok = tEQ; nc = scan(); }
		} else if (tok == tBANG) {
			if (nc == '=') { tok = tNE; nc = scan(); }
		} else if (tok == tSLASH) {
			if (nc == '/') {
				// comment -- consume until EOL or EOF
				while ((nc != '\n') && (nc != 0)) {
					nc = scan();
				}
				continue;
			}
		} else if (tok == tEOL) {
			ctx.linenumber++;
			ctx.lineoffset = ctx.byteoffset;
			if (ctx.flags & cfVisibleEOL) {
				return tEOL;
			}
			continue;
		} else if (tok == tSPC) {
			continue;
		} else if ((tok == tMSC) || (tok == tINV)) {
			error("unknown character 0x%02x", cc);
		}

		// if we're an AddOp or MulOp, followed by an '='
		if (((tok & 0xF0) == 0x20) && (nc == '=')) {
			nc = scan();
			// transform us to a XEQ operation
			tok = tok + 0x10;
		}

		return tok;
	}
}

void token_printstr(FILE *fp) {
	u32 n = 0;
	printf("\"");
	while (n < 256) {
		u32 ch = ctx.tmp[n];
		if (ch == 0) {
			break;
		} else if ((ch < ' ') || (ch > '~')) {
			fprintf(fp, "\\x%02x", ch);
		} else if ((ch == '"') || (ch == '\\')) {
			fprintf(fp, "\\%c", ch);
		} else {
			fprintf(fp, "%c", ch);
		}
		n++;
	}
	printf("\"");
}

void token_print(FILE *fp) {
	if (ctx.tok == tNUM) {
		fprintf(fp, "#%u ", ctx.num);
	} else if (ctx.tok == tIDN) {
		fprintf(fp, "@%s ", ctx.tmp);
	} else if (ctx.tok == tEOL) {
		fprintf(fp, "\n");
	} else if (ctx.tok == tSTR) {
		token_printstr(fp);
	} else {
		fprintf(fp, "%s ", tnames[ctx.tok]);
	}
}

token_t next() {
#if 1
	return (ctx.tok = _next());
#else
	ctx.tok = _next();
	token_print(stderr);
	fprintf(stderr,"\n");
	return ctx.tok;
#endif
}

void expected(const char* what) {
	error("expected %s, found %s", what, tnames[ctx.tok]);
}

void expect(token_t tok) {
	if (ctx.tok != tok) {
		error("expected %s, found %s", tnames[tok], tnames[ctx.tok]);
	}
}

void require(token_t tok) {
	expect(tok);
	next();
}

String *parse_name(const char* what) {
	if (ctx.tok != tIDN) {
		error("expected %s, found %s %u", what, tnames[ctx.tok], ctx.tok);
	}
	String *str = ctx.ident;
	next();
	return str;
}

void parse_expr(void);

int is_type(const char* typename) {
	String *name = ctx.ident;
	Symbol *sym = symbol_find(name);
	if (sym == nil) {
		error("undefined identifier '%s'", name->text);
	}
	return !strcmp(sym->type->name->text, typename);
}

// cheesy varargs for a few special purpose functions
void parse_va_call(const char* fn) {
	emit_impl("({ int fd = fn_%s_begin();", fn);
	while (ctx.tok != tCPAREN) {
		if (ctx.tok == tSTR) {
			emit_impl(" fn_writes(fd,");
		} else if (ctx.tok == tIDN) {
			emit_impl(" fn_write%s(fd,", is_type("str") ? "s" : "x");
		} else {
			emit_impl(" fn_writex(fd,");
		}
		parse_expr();
		emit_impl(");");
		if (ctx.tok != tCPAREN) {
			require(tCOMMA);
		}
	}
	next();
	emit_impl(" fn_%s_end(); })", fn);
}

void parse_ident(void) {
	String *name = ctx.ident;
	Symbol *sym = symbol_find(name);
	next();

	if ((sym == nil) && (ctx.tok != tOPAREN)) {
		error("undefined identifier '%s'", name->text);
	}

	if (ctx.tok == tOPAREN) {
		// function call
		next();
		if (!strcmp(name->text, "error")) {
			parse_va_call("error");
			return;
		}
		emit_impl("fn_%s(", name->text);
		while (ctx.tok != tCPAREN) {
			parse_expr();
			if (ctx.tok != tCPAREN) {
				require(tCOMMA);
				emit_impl(", ");
			}
		}
		next();
		emit_impl(")");
	} else {
		// variable access
		if (sym->kind == SYMBOL_DEF) {
			emit_impl("c$%s", sym->name->text);
		} else {
			emit_impl("$%s", sym->name->text);
		}
	}

	while (1) {
		if (ctx.tok == tDOT) {
			// field access
			next();
			String *fieldname = parse_name("field name");
			emit_impl("->%s", fieldname->text);
		} else if (ctx.tok == tOBRACK) {
			// array access
			next();
			emit_impl("[");
			parse_expr();
			emit_impl("]");
			require(tCBRACK);
		} else {
			return;
		}
	}
}

void parse_primary_expr(void) {
	if (ctx.tok == tNUM) {
		emit_impl("0x%x", ctx.num);
	} else if (ctx.tok == tSTR) {
		emit_impl_str();
	} else if (ctx.tok == tTRUE) {
		emit_impl("1");
	} else if (ctx.tok == tFALSE) {
		emit_impl("0");
	} else if (ctx.tok == tNIL) {
		emit_impl("0");
	} else if (ctx.tok == tOPAREN) {
		next();
		parse_expr();
		require(tCPAREN);
		return;
	} else if (ctx.tok == tNEW) {
		next();
		require(tOPAREN);
		String *typename = parse_name("type name");
		require(tCPAREN);
		emit_impl("calloc(1,sizeof(t$%s))", typename->text);
		return;
	} else if (ctx.tok == tIDN) {
		parse_ident();
		return;
	} else {
		error("invalid expression");
	}
	next();
}

void parse_unary_expr(void) {
	u32 op = ctx.tok;
	if (op == tPLUS) {
		next();
		parse_unary_expr();
	} else if (op == tMINUS) {
		emit_impl("(-");
		next();
		parse_unary_expr();
		emit_impl(")");
	} else if (op == tBANG) {
		emit_impl("(!");
		next();
		parse_unary_expr();
		emit_impl(")");
	} else if (op == tNOT) {
		emit_impl("(~");
		next();
		parse_unary_expr();
		emit_impl(")");
	} else if (op == tAMP) {
		error("dereference not supported");
		next();
		parse_unary_expr();
	} else {
		return parse_primary_expr();
	}
}

void parse_mul_expr(void) {
	unsigned x = emit_impl_oparen();
	parse_unary_expr();
	while ((ctx.tok & tcMASK) == tcMULOP) {
		emit_impl(" %s ", tnames[ctx.tok]);
		next();
		parse_unary_expr();
		x |= KEEP_PARENS;
	}
	emit_impl_cparen(x);
}

void parse_add_expr(void) {
	unsigned x = emit_impl_oparen();
	parse_mul_expr();
	while ((ctx.tok & tcMASK) == tcADDOP) {
		emit_impl(" %s ", tnames[ctx.tok]);
		next();
		parse_mul_expr();
		x |= KEEP_PARENS;
	}
	emit_impl_cparen(x);
}

void parse_rel_expr(void) {
	unsigned x = emit_impl_oparen();
	parse_add_expr();
	if ((ctx.tok & tcMASK) == tcRELOP) {
		emit_impl(" %s ", tnames[ctx.tok]);
		next();
		parse_add_expr();
		x |= KEEP_PARENS;
	}
	emit_impl_cparen(x);
}

void parse_and_expr(void) {
	unsigned x = emit_impl_oparen();
	parse_rel_expr();
	if (ctx.tok == tAND) {
		while (ctx.tok == tAND) {
			emit_impl(" && ");
			next();
			parse_rel_expr();
		}
		x |= KEEP_PARENS;
	}
	emit_impl_cparen(x);
}

void parse_expr(void) {
	unsigned x = emit_impl_oparen();
	parse_and_expr();
	if (ctx.tok == tOR) {
		while (ctx.tok == tOR) {
			emit_impl(" || ");
			next();
			parse_and_expr();
		}
		x |= KEEP_PARENS;
	}
	emit_impl_cparen(x);
}

// fwd_ref_ok indicates that an undefined typename
// may be treated as a forward reference.  This is
// only used for pointers (because their size does
// not depend on their target).
Type *parse_type(bool fwd_ref_ok);

Type *parse_struct_type(String *name) {
	Type *rectype = type_find(name);

	if (rectype) {
		if (rectype->kind == TYPE_UNDEFINED) {
			// resolve forward ref
			rectype->kind = TYPE_STRUCT;
		} else {
			error("Cannot redefine struct '%s'", name->text);
		}
	} else {
		rectype = type_make(name, TYPE_STRUCT, nil, nil, 0);
	};
	scope_push(SCOPE_STRUCT);
	require(tOBRACE);
	emit_type("typedef struct t$%s t$%s;\n", name->text, name->text);
	emit_decl("struct t$%s {\n", name->text);
	while (true) {
		if (ctx.tok == tCBRACE) {
			next();
			break;
		}
		String *fname = parse_name("field name");
		bool ptr = (ctx.tok == tSTAR);
		if (ptr) next();
		Type *type = parse_type(true);
		emit_decl("    t$%s %s%s;\n", type->name->text, ptr ? "*" : "", fname->text);
		Symbol *sym = symbol_make(fname, type);
		sym->kind = ptr ? SYMBOL_PTR : SYMBOL_FLD;
		if (ctx.tok != tCBRACE) {
			require(tCOMMA);
		}
	}
	emit_decl("};\n"); // xxx was _type
	rectype->fields = scope_pop()->first;
	return rectype;
}

Type *parse_array_type(void) {
	Type *type;
	u32 nelem = 0;
	char tmp[256];
	if (ctx.tok == tCBRACK) {
		next();
		type = type_make(nil, TYPE_ARRAY, parse_type(false), nil, 0);
	} else {
		if (ctx.tok != tNUM) {
			error("array size must be numeric");
		}
		nelem = ctx.num;
		next();
		require(tCBRACK);
		type = type_make(nil, TYPE_ARRAY, parse_type(false), nil, nelem);
	}
	sprintf(tmp, "%s$%u", type->of->name->text, nelem);
	type->name = string_make(tmp, strlen(tmp));
	if (nelem == 0) {
		emit_type("typedef t$%s t$%s[];\n", type->of->name->text, type->name->text);
	} else {
		emit_type("typedef t$%s t$%s[%u];\n", type->of->name->text, type->name->text, nelem);
	}
	return type;
}

Type *parse_type(bool fwd_ref_ok) {
	if (ctx.tok == tSTAR) { // pointer-to
		error("pointer types not supported");
		//next();
		//return type_make(nil, TYPE_POINTER, parse_type(true), nil, 0);
	} else if (ctx.tok == tOBRACK) { // array-of
		next();
		return parse_array_type();
	} else if (ctx.tok == tFN) {
		error("func types not supported");
		//next();
		//return parse_func_type();
	} else if (ctx.tok == tSTRUCT) {
		error ("anonymous struct types not supported");
		//next();
		//return parse_struct_type(nil);
	} else if (ctx.tok == tIDN) {
		String *name = ctx.ident;
		next();
		Type *type = type_find(name);
		if (type == nil) {
			if (fwd_ref_ok) {
				type = type_make(name, TYPE_UNDEFINED, nil, nil, 0);
			} else {
				error("undefined type '%s' not usable here", name->text);
			}
		}
		return type;
	} else {
		expected("type");
	}
	return nil;
}

void parse_block(void);

void parse_while(void) {
	emit_impl("while (");
	parse_expr();
	require(tOBRACE);
	scope_push(SCOPE_LOOP);
	emit_impl(") {\n");
	parse_block();
	scope_pop();
	emit_impl("}\n");
}

void parse_if(void) {
	// if expr { block }
	emit_impl("if (");
	parse_expr();
	emit_impl(") {\n");
	require(tOBRACE);
	scope_push(SCOPE_BLOCK);
	parse_block();
	scope_pop();
	while (ctx.tok == tELSE) {
		next();
		// ... else ...
		if (ctx.tok == tIF) {
			// ... if expr { block }
			emit_impl("} else if ");
			next();
			parse_expr();
			require(tOBRACE);
			emit_impl(" {\n");
			scope_push(SCOPE_BLOCK);
			parse_block();
			scope_pop();
		} else {
			// ... { block }
			emit_impl("} else {\n");
			require(tOBRACE);
			scope_push(SCOPE_BLOCK);
			parse_block();
			scope_pop();
			break;
		}
	}
	emit_impl("}\n");
}

void parse_return(void) {
	if (ctx.tok == tSEMI) {
		//	error("function requires return type");
		next();
		emit_impl("return;\n");
	} else {
		//	error("return types do not match");
		emit_impl("return ");
		parse_expr();
		emit_impl(";\n");
		require(tSEMI);
	}
}

void parse_break(void) {
	// XXX break-to-labeled-loop support
	Scope *scope = scope_find(SCOPE_LOOP);
	if (scope == nil) {
		error("break must be used from inside a looping construct");
	}
	require(tSEMI);
	emit_impl("break;\n");
}

void parse_continue(void) {
	// XXX continue-to-labeled-loop support
	Scope *scope = scope_find(SCOPE_LOOP);
	if (scope == nil) {
		error("continue must be used from inside a looping construct");
	}
	require(tSEMI);
	emit_impl("continue;\n");
}

void parse_struct_init(Symbol *var) {
	while (true) {
		if (ctx.tok == tCBRACE) {
			next();
			break;
		}
		String *name = parse_name("field name");
		Symbol *field = var->type->fields;
		while (true) {
			if (field == nil) {
				error("structure has no '%s' field", name->text);
			}
			if (field->name == name) {
				break;
			}
			field = field->next;
		}
		require(tCOLON);
		if (ctx.tok == tOBRACE) {
			next();
			emit_impl("{");
			parse_struct_init(field);
			emit_impl("}");
		} else {
			parse_expr();
			//emit_impl( "0x%x", ctx.num);
		}
		emit_impl( ",");
		if (ctx.tok != tCBRACE) {
			require(tCOMMA);
		}
	}
}

void parse_array_init(Symbol *var) {
	while (true) {
		if (ctx.tok == tCBRACE) {
			next();
			break;
		}
		parse_expr();
		emit_impl(",");
		if (ctx.tok != tCBRACE) {
			require(tCOMMA);
		}
	}
}

void parse_var(void) {
	String *name = parse_name("variable name");
	Type *type = parse_type(false);
	Symbol *var = symbol_make(name, type);

	if (ctx.tok == tASSIGN) {
		next();
		if (ctx.tok == tOBRACE) {
			next();
			if (type->kind == TYPE_STRUCT) {
				emit_impl("t$%s $$%s = {\n", type->name->text, name->text);
				parse_struct_init(var);
				emit_impl("\n};\nt$%s *$%s = &$$%s;\n",
					type->name->text, name->text, name->text);
			} else if (type->kind == TYPE_ARRAY) {
				emit_impl("t$%s $%s = {\n", type->name->text, name->text);
				parse_array_init(var);
				emit_impl("\n};\n");
			} else {
				error("type %s cannot be initialized with {} expr", type->name->text);
			}
		} else {
			emit_impl("t$%s %s$%s = ", type->name->text,
				(type->kind == TYPE_STRUCT) ? "*" : "",
				name->text);
			parse_expr();
			emit_impl(";\n");
		}
	} else {
		if (type->kind == TYPE_ARRAY) {
			emit_impl("t$%s $%s = { 0, };\n", type->name->text, name->text);
		} else {
			emit_impl("t$%s %s$%s = 0;\n", type->name->text,
				(type->kind == TYPE_STRUCT) ? "*" : "",
				name->text);
		}
	}
	require(tSEMI);

}

void parse_expr_statement(void) {
	parse_expr();
	if (ctx.tok == tASSIGN) {
		emit_impl(" = ");
		next();
		parse_expr();
	} else if ((ctx.tok & tcMASK) == tcAEQOP) {
		emit_impl(" %s ", tnames[ctx.tok]);
		next();
		parse_expr();
	} else if ((ctx.tok & tcMASK) == tcMEQOP) {
		emit_impl(" %s ", tnames[ctx.tok]);
		next();
		parse_expr();
	} else if ((ctx.tok == tINC) || (ctx.tok == tDEC)) {
		emit_impl(" %s", tnames[ctx.tok]);
		next();
	}
	require(tSEMI);
	emit_impl(";\n");
}

void parse_block(void) {
	while (true) {
		if (ctx.tok == tCBRACE) {
			next();
			break;
		} else if (ctx.tok == tRETURN) {
			next();
			parse_return();
		} else if (ctx.tok == tBREAK) {
			next();
			parse_break();
		} else if (ctx.tok == tCONTINUE) {
			next();
			parse_continue();
		} else if (ctx.tok == tWHILE) {
			next();
			parse_while();
		} else if (ctx.tok == tIF) {
			next();
			parse_if();
		} else if (ctx.tok == tVAR) {
			next();
			parse_var();
		} else if (ctx.tok == tSEMI) {
			next();
			// empty statement
			continue;
		} else {
			parse_expr_statement();
		}
	}
}

Symbol *parse_param(String *fname) {
	String *pname = parse_name("parameter name");
	Type *ptype = parse_type(false);

	// arrays and structs are always passed as reference parameters
	//if ((ptype->kind == TYPE_ARRAY) || (ptype->kind == TYPE_RECORD)) {
	//	ptype = type_make_ptr(ptype);
	//}

	if (symbol_find(pname)) {
		error("duplicate parameter name '%s'", pname->text);
	}

	return symbol_make(pname, ptype);
}

void parse_function(void) {
	String *fname = parse_name("function name");
	Type *rtype = ctx.type_void;

	scope_push(SCOPE_FUNC);

	require(tOPAREN);
	if (ctx.tok != tCPAREN) {
		parse_param(fname);
		while (ctx.tok == tCOMMA) {
			next();
			parse_param(fname);
		}
	}
	require(tCPAREN);

	if (ctx.tok != tOBRACE) {
		rtype = parse_type(false);
	}

	emit_decl("t$%s%s fn_%s(", rtype->name->text, 
		rtype->kind == TYPE_STRUCT ? "*" : "", fname->text);
	emit_impl("t$%s%s fn_%s(", rtype->name->text,
		rtype->kind == TYPE_STRUCT ? "*" : "", fname->text);
	for (Symbol *s = ctx.scope->first; s != nil; s = s->next) {
		emit_decl("t$%s %s$%s%s",
			s->type->name->text,
			s->type->kind == TYPE_STRUCT ? "*" : "",
			s->name->text, s->next ? ", " : "");
		emit_impl("t$%s %s$%s%s",
			s->type->name->text,
			s->type->kind == TYPE_STRUCT ? "*" : "",
			s->name->text, s->next ? ", " : "");
	}
	emit_decl("%s);\n", ctx.scope->first ? "" : "t$void");
	emit_impl("%s) {\n", ctx.scope->first ? "" : "t$void");

	// TODO: more complete type if needed...
	Symbol *sym = symbol_make_global(fname, rtype);
	sym->kind = SYMBOL_FN;

	require(tOBRACE);

	scope_push(SCOPE_BLOCK);
	parse_block();
	scope_pop();

	emit_impl("}\n");

	scope_pop();
}

void parse_enum_def(void) {
	if (ctx.tok == tIDN) {
		String *name = parse_name("enum name");
		type_make(name, TYPE_ENUM, nil, nil, 0);
		emit_type("typedef t$u32 t$%s; // enum\n", name->text);
	}

	require(tOBRACE);
	u32 val = 0;
	while (ctx.tok != tCBRACE) {
		String *name = parse_name("enum tag name");
		Symbol *sym = symbol_find(name);
		if (sym != nil) {
			error("cannot redefine %s as enum tag\n", name->text);
		}
		symbol_make_global(name, ctx.type_u32)->kind = SYMBOL_DEF;
		emit_impl("#define c$%s ", name->text, val);
		if (ctx.tok == tASSIGN) {
			next();
			parse_expr();
			emit_impl("\n");
		} else {
			emit_impl("0x%x\n", val);
			val++;
		}
		require(tCOMMA);
	}
	require(tCBRACE);
	require(tSEMI);
}

void parse_program() {
	emit_impl("\n#include <library.impl.h>\n");
	next();
	for (;;) {
		if (ctx.tok == tENUM) {
			next();
			parse_enum_def();
		} else if (ctx.tok == tSTRUCT) {
			next();
			String *name = parse_name("struct name");
			parse_struct_type(name);
			require(tSEMI);
		} else if (ctx.tok == tFN) {
			next();
			parse_function();
		} else if (ctx.tok == tVAR) {
			next();
			parse_var();
		} else if (ctx.tok == tEOF) {
			emit_impl("\n#include <library.impl.c>\n");
			return;
		} else {
			expected("function, variable, or type definition");
		}
	}

}

// ================================================================

int main(int argc, char **argv) {
	char *srcname = nil;
	bool scan_only = false;

	ctx_init();
	ctx.filename = "<commandline>";
	ctx.outname = nil;

	while (argc > 1) {
		if (!strcmp(argv[1],"-o")) {
			if (argc < 2) {
				error("option -o requires argument");
			}
			ctx.outname = argv[2];
			argc--;
			argv++;
		} else if (!strcmp(argv[1], "-s")) {
			scan_only = true;
		} else if (!strcmp(argv[1], "-A")) {
			ctx.flags |= cfAbortOnError;
		} else if (argv[1][0] == '-') {
			error("unknown option: %s", argv[1]);
		} else {
			if (srcname != nil) {
				error("multiple source files disallowed");
			} else {
				srcname = argv[1];
			}
		}
		argc--;
		argv++;
	}

	if (srcname == nil) {
		printf(
"usage:    compiler [ <option> | <sourcefilename> ]*\n"
"\n"
"options:  -o <filename>    output base name (default source name)\n"
"          -s               scan only\n"
"          -A               abort on error\n");
		return 0;
	}
	ctx.filename = srcname;
	if (ctx.outname == nil) {
		ctx.outname = srcname;
	}

	ctx_open_source(srcname);
	ctx.linenumber = 1;
	ctx.lineoffset = 0;

	ctx_open_output();
	// prime the lexer
	scan();

	if (scan_only) {
		ctx.flags |= 1;
		while (true) {
			next();
			token_print(stdout);
			if (ctx.tok == tEOF) {
				printf("\n");
				return 0;
			}
		}
	}

	parse_program();
	return 0;
}
