# Systems Programming Language

A small compiled language borrowing ideas from C, Go, Rust, Zig.

Very much a work in progress.

The next phase of https://github.com/swetland/compiler/

## Organization

- [bootstrap/...](bootstrap/) - an SPL to C transpiler, written in C (compiler0)
- [compiler/...](compiler/) - an SPL compiler, written in SPL (compiler1)
- [build/...](build/) - build scripts
- [test/...](test/) - automated tests (SPL source and "golden" output)
- [demo/...](demo/) - programs to exercise the compiler
- [vim/...](vim/) - SPL syntax highlighting (install in `~/.vim/pack/plugins/start/spl`)

