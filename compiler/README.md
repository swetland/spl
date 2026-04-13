
Some of the compiler code must be able to be compiled by the bootstrap
transpiler, and thus needs to avoid language features not supported there.

- [stdlib.spl](stdlib.spl): minimal standard library
- [io.spl](io.spl): formatted io libary
- [types.spl](types.spl): struts and enums for core compiler types
- [tools.spl](tools.spl): methods and helpers for core compiler types
- [lexer.spl](lexer.spl): frontend: the lexer
- [parser.spl](parser.spl): frontend: the parser
- [constexpr.spl](constexpr.spl): frontend: helper for constant expressions
- [gen-global-data.spl](gen-global-data.spl): manager for globals
- [gen-sr32-abi0.spl](gen-sr32-abi0.spl): non-optmized straigtline sr32 backend

The rest of the compiler code is built by the stage1 or later compilers
and thus can use any language feature in the full compiler:

- [bits.spl](bits.spl): bitsets library
- [sequentialize.spl](sequentialize.spl): copy sequentalization library
- [ir-types-sr32.spl](ir-types-sr32.spl)
- [ir-instructions.spl](ir-instructions.spl): backend: IR Instruction types, methods, and helpers
- [ir-blocks.spl](ir-blocks.spl): backend: basic block and instruction stream management
- [ir-gen.spl](ir-gen.spl): backend: generate IR from AST

Features not available in the bootstrap transpiler:

- type inference for variable declarations
- ability to call static methods in expressions

