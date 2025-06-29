
# softrisc32

A "toy" 32bit RISC architecture inspired by RISC-V.

It's designed to be a very similar "shape" to the RV32I ISA,
but is tuned for maximum friendliness toward simple emulators,
toolchains, etc rather than tweaked for most optimal hardware
implementation, expandability, etc.

In particular:
- it only concerns itself with a 32bit ISA
- it optimizes instruction encoding for simplicity of software
implementation and at-a-glace comprehensibility by humans
- it attempts to allow for a trivial conversion of generated
code or code generators to RV32I, provided the RV32I immediate
size constraints are used instead of the slightly more generous SR32 ones.

Instruction Set Summary and Encoding: [softrisc32.txt](softrisc32.txt)
