
CFLAGS := -g -O2 -Wall -Isrc -Igen

all: bin/asm bin/emu

gen/instab.h: instab.txt bin/mkinstab
	@mkdir -p gen
	bin/mkinstab < instab.txt > $@

bin/mkinstab: src/mkinstab.c
	@mkdir -p bin
	gcc $(CFLAGS) -o $@ src/mkinstab.c

bin/asm: src/assemble-sr32.c src/disassemble-sr32.c src/sr32.h gen/instab.h
	@mkdir -p bin
	gcc $(CFLAGS) -o $@ src/assemble-sr32.c src/disassemble-sr32.c

bin/emu: src/emulator-sr32.c src/cpu-sr32.c src/emulator-sr32.h src/sr32.h
	@mkdir -p bin
	gcc $(CFLAGS) -o $@ src/emulator-sr32.c src/cpu-sr32.c

clean:
	rm -rf gen bin
