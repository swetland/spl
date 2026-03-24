.PRECIOUS: out/%.impl.c out/%.type.h out/%.decl.h

all: out/asm out/emu out/iremu out/compiler1 out/compiler2a.bin out/compiler2b.bin out/compiler3a.bin

test: out/test/summary.txt

freeze1: out/compiler1
	@mkdir -p frozen/
	cp -f out/compiler1 frozen/compiler1

freeze2a: out/compiler2a.bin
	@mkdir -p frozen/
	cp -f out/compiler2a.bin frozen/compiler2a.bin

freeze:: freeze1 freeze2a

out/gen/ir.h: compiler/ir-instructions.spl
	@mkdir -p out/gen
	sed -e '/iop_name/,$$d' $< > $@

out/iremu: iremu/iremu.c out/gen/ir.h
	@mkdir -p out
	gcc -g -O2 -Wall -Iout/gen -o $@ iremu/iremu.c

# compiler0: bootstrap SPL->C transpiler
#
out/compiler0: bootstrap/compiler0.c
	@echo ''
	@echo '### BUILDING STAGE 0 BOOTSTRAP TRANSPILER ###'
	@mkdir -p out
	gcc -g -O0 -Wall -o out/compiler0 bootstrap/compiler0.c

# front end and common code shared among stage1 and later
FRONTEND_SRC := compiler/stdlib.spl compiler/types.spl compiler/io.spl compiler/tools.spl
FRONTEND_SRC += compiler/lexer.spl compiler/constexpr.spl compiler/parser.spl

# sr32 compiler backend used by stage1 and stage2
GEN_SR32_SRC += compiler/gen-global-data.spl compiler/gen-sr32-abi0.spl

# full compiler backend used by stage3
GEN_IR_SRC := compiler/gen-global-data.spl compiler/ir-types-sr32.spl
GEN_IR_SRC += compiler/ir-instructions.spl compiler/ir-blocks.spl compiler/ir-gen.spl

# stage1 which is built by the transpiler
COMPILER1_SRC := build/stdlib-stub.spl $(FRONTEND_SRC) $(GEN_SR32_SRC) compiler/main.spl

# stage2 which is built by stage1
COMPILER2_SRC := build/stdlib-abi0.spl $(FRONTEND_SRC) $(GEN_SR32_SRC) compiler/main.spl

# stage3 which is built by stage2
COMPILER3_SRC := build/stdlib-abi0.spl $(FRONTEND_SRC) $(GEN_IR_SRC) compiler/main.spl

# compiler1: SPL compiler written in SPL
#
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

# compiler3a: SPL compiler written in SPL, compiled by compiler2a
#
out/compiler3a.s32: out/compiler2b.bin out/emu $(COMPILER3_SRC)
	@echo ''
	@echo '### BUILDING STAGE 3A COMPILER USING STAGE 2B COMPILER ###'
	./out/emu -q ./out/compiler2b.bin -stats -ast out/compiler3a.ast -out $@ $(COMPILER3_SRC)

COMPILER3A_ASM := build/stdlib-abi0.s32 build/syscall-abi0.s32 out/compiler3a.s32
out/compiler3a.bin: ./out/asm $(COMPILER3A_ASM)
	./out/asm -o $@ $(COMPILER3A_ASM)


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

TESTDEPS0 := out/compiler0 build/runtest build/compile0
TESTDEPS0 += $(wildcard bootstrap/inc/*.h) $(wildcard bootstrap/inc/*.c)
TESTDEPSX := out/asm out/emu build/runtest build/stdlib-abi0.spl build/stdlib-abi0.s32
TESTDEPS1 := $(TESTDEPSX) out/compiler1
TESTDEPS2 := $(TESTDEPSX) out/compiler2a.bin
TESTDEPS3 := $(TESTDEPSX) out/compiler3a.bin out/iremu

SRCTESTS := $(sort $(wildcard test/*.spl))

# have to have two rules here otherwise tests without .log files
# fail to be compiled by the rule that depends on spl+log *or*
# we fail to depend on the .log for tests with both...

define mktestrule
$(eval ALLTESTS$1 := $(patsubst test/%.spl,out/test$1/%.txt,$(SRCTESTS)))

runtests$1:: out/test$1/summary.txt

out/test$1/%.txt: test/%.spl test/%.log
	@mkdir -p out/test$1
	@rm -f $$@
	@build/runtest$1 $$< $$@

out/test$1/%.txt: test/%.spl
	@mkdir -p out/test$1
	@rm -f $$@
	@build/runtest$1 $$< $$@

$(ALLTESTS$1) : build/runtest$1 $(TESTDEPS$1)

out/test$1/summary.txt: $(ALLTESTS$1)
	@cat $$(ALLTESTS$1) > $$@
endef

# generate a rule, but only if it's a current goal
define mkrule
ifneq (,$(filter $(1),$(MAKECMDGOALS)))
$(1): $(2)
$(2): __FORCE__
endif
endef

# generate implicit rules and runtest rules for running
# all the tests against the various compiler stages
$(foreach n,0 1 2 3,$(eval $(call mktestrule,$n)))

# generate shortcut runtest rules for the tests
# for example, test/1024-fibonnaci.spl gets the shortcut
# rules 1025.0, 1025.1, 1025.2, and 1025.3 defined to run
# that test against stage0, stage1, etc compilers
$(foreach n,0 1 2 3,$(foreach x,$(ALLTESTS$n),$(eval $(call mkrule,$(firstword $(subst -, ,$(patsubst out/test$n/%,%,$(x)))).$n,$(x)))))

__FORCE__: ;

TOP := softrisc32/
BIN := out/
GEN := out/gen/

include $(TOP)Makefile
