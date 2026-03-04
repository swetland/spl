.PRECIOUS: out/%.impl.c out/%.type.h out/%.decl.h

all: out/asm out/emu out/compiler1 out/compiler2a.bin out/compiler2b.bin

test: out/test/summary.txt

freeze1: out/compiler1
	@mkdir -p frozen/
	cp -f out/compiler1 frozen/compiler1

freeze2a: out/compiler2a.bin
	@mkdir -p frozen/
	cp -f out/compiler2a.bin frozen/compiler2a.bin

freeze:: freeze1 freeze2a

# compiler0: bootstrap SPL->C transpiler
#
out/compiler0: bootstrap/compiler0.c
	@echo ''
	@echo '### BUILDING STAGE 0 BOOTSTRAP TRANSPILER ###'
	@mkdir -p out
	gcc -g -O0 -Wall -o out/compiler0 bootstrap/compiler0.c


# compiler1: SPL compiler written in SPL
#
COMPILERX_SRC := compiler/stdlib.spl compiler/types.spl compiler/io.spl compiler/tools.spl
COMPILERX_SRC += compiler/lexer.spl compiler/constexpr.spl compiler/parser.spl
COMPILER1_SRC := build/stdlib-stub.spl $(COMPILERX_SRC) compiler/gen-sr32-abi0.spl compiler/main.spl
COMPILER2_SRC := build/stdlib-abi0.spl $(COMPILERX_SRC) compiler/gen-sr32-abi0.spl compiler/main.spl

ifneq ($(wildcard frozen/compiler2a.bin),)
out/compiler1: frozen/compiler1
	@mkdir -p out
	@echo ''
	@echo '### USING FROZEN STAGE 1 COMPILER ###'
	cp -f $< $@
else
out/compiler1: out/compiler0 $(COMPILER1_SRC)
	@echo ''
	@echo '### BUILDING STAGE 1 COMPILER USING TRANSPILER ###'
	@mkdir -p out/
	./out/compiler0 -o out/compiler1 $(COMPILER1_SRC)
	gcc -g -O0 -Wall -Wno-unused-variable -DQUIET -I. -Ibootstrap/inc -Iout -o $@ out/compiler1.impl.c
endif

ifneq ($(wildcard frozen/compiler2a.bin),)
out/compiler2a.bin: frozen/compiler2a.bin
	@mkdir -p out
	@echo ''
	@echo '### USING FROZEN STAGE 2A COMPILER ###'
	cp -f $< $@
else
# compiler2a: SPL compiler written in SPL, compiled by compiler1
#
out/compiler2a.s32: out/compiler1 $(COMPILER2_SRC)
	@echo ''
	@echo '### BUILDING STAGE 2A COMPILER USING STAGE 1 COMPILER ###'
	./out/compiler1 -ast out/compiler2a.ast -out $@ $(COMPILER2_SRC)

COMPILER2_ASM := build/stdlib-abi0.s32 build/syscall-abi0.s32 out/compiler2a.s32
out/compiler2a.bin: out/asm $(COMPILER2_ASM)
	@mkdir -p out/
	./out/asm -o $@ $(COMPILER2_ASM)
endif

# compiler2b: SPL compiler written in SPL, compiled by compiler2a
#
out/compiler2b.s32: out/compiler2a.bin out/emu $(COMPILER2_SRC)
	@echo ''
	@echo '### BUILDING STAGE 2B COMPILER USING STAGE 2A COMPILER ###'
	./out/emu -q ./out/compiler2a.bin -ast out/compiler2b.ast -out $@ $(COMPILER2_SRC)

COMPILER2B_ASM := build/stdlib-abi0.s32 build/syscall-abi0.s32 out/compiler2b.s32
out/compiler2b.bin: ./out/asm $(COMPILER2B_ASM)
	./out/asm -o $@ $(COMPILER2B_ASM)

# rules for building out/.../foo.bin from .../foo.spl
#
out/%.impl.c out/%.type.h out/%.decl.h: %.spl out/compiler0
	@mkdir -p $(dir $(patsubst %.spl,out/%.impl.c,$<))
	./out/compiler0 -o $(patsubst %.spl,out/%,$<) $<

out/%.bin: out/%.impl.c out/%.type.h out/%.decl.h
	gcc -g -O0 -Wall -I. -Ibootstrap/inc -Iout -o $@ $<

clean::
	rm -rf bin out

spotless::
	rm -rf bin out frozen

runtests0:: out/test0/summary.txt
runtests1:: out/test1/summary.txt
runtests2:: out/test2/summary.txt

# have to have two rules here otherwise tests without .log files
# fail to be compiled by the rule that depends on spl+log *or*
# we fail to depend on the .log for tests with both...

TESTDEPS0 := out/compiler0 build/runtest0 build/compile0
TESTDEPS0 += $(wildcard bootstrap/inc/*.h) $(wildcard bootstrap/inc/*.c)

TESTDEPS1 := out/compiler1 out/asm out/emu build/runtest build/runtest1
TESTDEPS1 += build/stdlib-abi0.spl build/stdlib-abi0.spl

TESTDEPS2 := out/compiler2a.bin out/asm out/emu build/runtest build/runtest2
TESTDEPS2 += build/stdlib-abi0.spl build/stdlib-abi0.spl

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

out/test2/%.txt: test/%.spl test/%.log
	@mkdir -p out/test2
	@rm -f $@
	@build/runtest2 $< $@

out/test2/%.txt: test/%.spl
	@mkdir -p out/test2
	@rm -f $@
	@build/runtest2 $< $@

SRCTESTS := $(sort $(wildcard test/*.spl))
ALLTESTS0 := $(patsubst test/%.spl,out/test0/%.txt,$(SRCTESTS))
ALLTESTS1 := $(patsubst test/%.spl,out/test1/%.txt,$(SRCTESTS))
ALLTESTS2 := $(patsubst test/%.spl,out/test2/%.txt,$(SRCTESTS))

$(ALLTESTS0) : $(TESTDEPS0)
$(ALLTESTS1) : $(TESTDEPS1)
$(ALLTESTS2) : $(TESTDEPS2)

out/test0/summary.txt: $(ALLTESTS0)
	@cat $(ALLTESTS0) > $@

out/test1/summary.txt: $(ALLTESTS1)
	@cat $(ALLTESTS1) > $@

out/test2/summary.txt: $(ALLTESTS2)
	@cat $(ALLTESTS2) > $@

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
$(foreach x,$(ALLTESTS2),$(eval $(call mkrule,$(firstword $(subst -, ,$(patsubst out/test2/%,%,$(x)))).2,$(x))))

TOP := softrisc32/
BIN := out/
GEN := out/gen/

include $(TOP)Makefile
