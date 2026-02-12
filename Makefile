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
COMPILERX_SRC := compiler/stdlib.spl compiler/types.spl compiler/io.spl compiler/tools.spl compiler/lexer.spl compiler/parser.spl
COMPILER1_SRC := $(COMPILERX_SRC) compiler/gen-sr32-abi0.spl compiler/main.spl

out/compiler1: ./out/compiler0 out/asm $(COMPILER1_SRC)
	@echo ''
	@echo '### BUILDING STAGE 1 COMPILER USING TRANSPILER ###'
	@mkdir -p out/ out/compiler
	./out/compiler0 -o out/compiler/compiler $(COMPILER1_SRC)
	gcc -g -O0 -Wall -Wno-unused-variable -I. -Ibootstrap/inc -Iout -o $@ out/compiler/compiler.impl.c

# compiler2: SPL compiler written in SPL, compiled by compiler1
#
out/compiler2: ./out/compiler1 $(COMPILER1_SRC)
	@echo ''
	@echo '### BUILDING STAGE 2 COMPILER USING STAGE 1 COMPILER ###'
	./out/compiler1 -ast out/compiler2.ast $(COMPILER1_SRC) > out/compiler2

# rules for building out/.../foo.bin from .../foo.spl
#
out/%.impl.c out/%.type.h out/%.decl.h: %.spl ./out/compiler0
	@mkdir -p $(dir $(patsubst %.spl,out/%.impl.c,$<))
	./out/compiler0 -o $(patsubst %.spl,out/%,$<) $<

out/%.bin: out/%.impl.c out/%.type.h out/%.decl.h
	gcc -g -O0 -Wall -I. -Ibootstrap/inc -Iout -o $@ $<

clean::
	rm -rf bin out

runtests0:: out/test0/summary.txt
runtests1:: out/test1/summary.txt

# have to have two rules here otherwise tests without .log files
# fail to be compiled by the rule that depends on spl+log *or*
# we fail to depend on the .log for tests with both...

TESTDEPS0 := out/compiler0 build/runtest0 build/compile0
TESTDEPS0 += $(wildcard bootstrap/inc/*.h) $(wildcard bootstrap/inc/*.c)

TESTDEPS1 := out/compiler1 out/asm out/emu build/runtest build/runtest1
TESTDEPS1 += build/stdlib-abi0.spl build/stdlib-abi0.spl

out/test0/%.txt: test/%.spl test/%.log
	@mkdir -p out/test0
	@rm -f $@
	@build/runtest0 $< $@

out/test0/%.txt: test/%.spl
	@mkdir -p out/test0
	@rm -f $@
	@build/runtest0 $< $@

out/test1/%.txt: test/%.spl test/%.log
	@mkdir -p out/test1
	@rm -f $@
	@build/runtest1 $< $@

out/test1/%.txt: test/%.spl
	@mkdir -p out/test1
	@rm -f $@
	@build/runtest1 $< $@

SRCTESTS := $(sort $(wildcard test/*.spl))
ALLTESTS0 := $(patsubst test/%.spl,out/test0/%.txt,$(SRCTESTS))
ALLTESTS1 := $(patsubst test/%.spl,out/test1/%.txt,$(SRCTESTS))

$(ALLTESTS0) : $(TESTDEPS0)
$(ALLTESTS1) : $(TESTDEPS1)

out/test0/summary.txt: $(ALLTESTS0)
	@cat $(ALLTESTS0) > $@

out/test1/summary.txt: $(ALLTESTS1)
	@cat $(ALLTESTS1) > $@

# generate a rule, but only if it's a current goal
define mkrule
ifneq (,$(filter $(1),$(MAKECMDGOALS)))
$(1): $(2)
$(2): __FORCE__
endif
endef

__FORCE__: ;

# generate shortcut runtest rules for the tests
$(foreach x,$(ALLTESTS0),$(eval $(call mkrule,$(firstword $(subst -, ,$(patsubst out/test0/%,%,$(x)))).0,$(x))))
$(foreach x,$(ALLTESTS1),$(eval $(call mkrule,$(firstword $(subst -, ,$(patsubst out/test1/%,%,$(x)))).1,$(x))))

TOP := softrisc32/
BIN := out/
GEN := out/gen/

include $(TOP)Makefile
