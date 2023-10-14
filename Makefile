

.NOTINTERMEDIATE: %.c %.h 

all: out/compiler0

test: out/test/summary.txt

out/compiler0: bootstrap/compiler0.c
	@mkdir -p out
	gcc -Wall -O0 -g -o out/compiler0 bootstrap/compiler0.c

out/compiler/compiler.bin: compiler/compiler.spl out/compiler0
	./build/compile0 compiler/compiler.spl

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
