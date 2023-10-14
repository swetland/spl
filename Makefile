

.NOTINTERMEDIATE: %.c %.h 

all: bin/compiler0

test: out/test/summary.txt

bin/compiler0: compiler0.c
	@mkdir -p bin
	gcc -Wall -O0 -g -o bin/compiler0 compiler0.c

clean::
	rm -rf bin out


# have to have two rules here otherwise tests without .log files
# fail to be compiled by the rule that depends on spl+log *or*
# we fail to depend on the .log for tests with both...

TESTDEPS := bin/compiler0 tools/runtest tools/compile
TESTDEPS += $(wildcard inc/*.h) $(wildcard inc/*.c)

out/test/%.txt: test/%.spl test/%.log $(TESTDEPS)
	@mkdir -p out/test
	@rm -f $@
	@tools/runtest $< $@

out/test/%.txt: test/%.spl $(TESTDEPS)
	@mkdir -p out/test
	@rm -f $@
	@tools/runtest $< $@


SRCTESTS := $(sort $(wildcard test/*.spl))
ALLTESTS := $(patsubst test/%.spl,out/test/%.txt,$(SRCTESTS))

out/test/summary.txt: $(ALLTESTS)
	@cat $(ALLTESTS) > $@

%: test/%.spl
	@$(MAKE) $(patsubst %.spl,out/%.txt,$<)
