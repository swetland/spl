
CFLAGS := -g -O2 -Wall -Isrc -Igen

all: bin/asm bin/emu

gen/instab.h: instab.txt bin/mkinstab
	@mkdir -p gen
	bin/mkinstab < instab.txt > $@

bin/mkinstab: src/mkinstab.c
	@mkdir -p bin
	gcc $(CFLAGS) -o $@ src/mkinstab.c

bin/asm: src/sr32asm.c src/sr32dis.c src/sr32.h gen/instab.h
	@mkdir -p bin
	gcc $(CFLAGS) -o $@ src/sr32asm.c src/sr32dis.c

bin/emu: src/sr32emu.c src/sr32cpu.c src/sr32emu.h src/sr32.h
	@mkdir -p bin
	gcc $(CFLAGS) -o $@ src/sr32emu.c src/sr32cpu.c

clean:
	rm -rf gen bin
