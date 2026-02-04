.PRECIOUS: out/%.impl.c out/%.type.h out/%.decl.h

all: out/compiler0 out/compiler1 out/compiler2 out/asm out/emu

test: out/test/summary.txt

# compiler0: bootstrap SPL->C transpiler
#
out/compiler0: bootstrap/compiler0.c
	@echo '### BUILDING BOOSTRAP TRANSPILER ###'
	@mkdir -p out
	gcc -Wall -O0 -g -o out/compiler0 bootstrap/compiler0.c


# compiler1: SPL compiler written in SPL
#
COMPILER_SRC := compiler/stdlib.spl compiler/types.spl compiler/io.spl compiler/tools.spl compiler/lexer.spl compiler/parser.spl compiler/main.spl

out/compiler1: ./out/compiler0 $(COMPILER_SRC)
	@echo ''
	@echo '### BUILDING STAGE 1 COMPILER USING TRANSPILER ###'
	@mkdir -p out/ out/compiler
	./out/compiler0 -o out/compiler/compiler $(COMPILER_SRC)
	gcc -g -O0 -Wall -Wno-unused-variable -I. -Ibootstrap/inc -Iout -o $@ out/compiler/compiler.impl.c

# compiler2: SPL compiler written in SPL, compiled by compiler1
#
out/compiler2: ./out/compiler1 $(COMPILER_SRC)
	@echo ''
	@echo '### BUILDING STAGE 2 COMPILER USING STAGE 1 COMPILER ###'
	./out/compiler1 -ast out/compiler2.ast $(COMPILER_SRC) > out/compiler2

# rules for building out/.../foo.bin from .../foo.spl
#
out/%.impl.c out/%.type.h out/%.decl.h: %.spl ./out/compiler0
	@mkdir -p $(dir $(patsubst %.spl,out/%.impl.c,$<))
	./out/compiler0 -o $(patsubst %.spl,out/%,$<) $<

out/%.bin: out/%.impl.c out/%.type.h out/%.decl.h
	gcc -g -O0 -Wall -I. -Ibootstrap/inc -Iout -o $@ $<

clean::
	rm -rf bin out

# have to have two rules here otherwise tests without .log files
# fail to be compiled by the rule that depends on spl+log *or*
# we fail to depend on the .log for tests with both...

TESTDEPS := out/compiler0 build/runtest0 build/compile0
TESTDEPS += $(wildcard bootstrap/inc/*.h) $(wildcard bootstrap/inc/*.c)

out/test/%.txt: test/%.spl test/%.log $(TESTDEPS)
	@mkdir -p out/test
	@rm -f $@
	@build/runtest0 $< $@

out/test/%.txt: test/%.spl $(TESTDEPS)
	@mkdir -p out/test
	@rm -f $@
	@build/runtest0 $< $@


SRCTESTS := $(sort $(wildcard test/*.spl))
ALLTESTS := $(patsubst test/%.spl,out/test/%.txt,$(SRCTESTS))

out/test/summary.txt: $(ALLTESTS)
	@cat $(ALLTESTS) > $@

%: test/%.spl
	@$(MAKE) $(patsubst %.spl,out/%.txt,$<)

TOP := softrisc32/
BIN := out/
GEN := out/gen/

include $(TOP)Makefile
