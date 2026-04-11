
BIN ?= bin/
GEN ?= gen/

CFLAGS_SR32 := -g -O2 -Wall -I$(TOP)src -I$(GEN)

ifeq ($(strip $(TOP)),)
all: $(BIN)asm $(BIN)emu

clean:
	rm -rf $(GEN) $(BIN)
endif

$(GEN)instab-sr32.h: $(TOP)instab.txt $(BIN)mkinstab
	@mkdir -p $(GEN)
	$(BIN)mkinstab < $(TOP)instab.txt > $@

$(BIN)mkinstab: $(TOP)src/mkinstab.c
	@mkdir -p $(BIN)
	gcc $(CFLAGS_SR32) -o $@ $(TOP)src/mkinstab.c

ASMSRC := src/assemble-sr32.c src/disassemble-sr32.c src/sr32.h
ASMSRC := $(addprefix $(TOP),$(ASMSRC)) $(GEN)instab-sr32.h

$(BIN)asm: $(ASMSRC)
	@mkdir -p $(BIN)
	gcc $(CFLAGS_SR32) -o $@ $(filter-out %.h,$(ASMSRC))

EMUSRC := src/emulator-sr32.c src/cpu-sr32.c src/emulator-sr32.h src/sr32.h
EMUSRC += src/memory.c src/syscalls-abi0.c
EMUSRC := $(addprefix $(TOP),$(EMUSRC)) $(GEN)instab-sr32.h

$(BIN)emu: $(EMUSRC)
	@mkdir -p $(BIN)
	gcc $(CFLAGS_SR32) -o $@ $(filter-out %.h,$(EMUSRC))

