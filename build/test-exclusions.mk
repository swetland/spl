
# list of tests to skip for various compiler stages

SKIPALL := test/2011-err-array-oob-const.spl

SKIPANON := test/1071-anon-sort.spl
SKIPANON += test/2070-err-anon-1.spl test/2071-err-anon-2.spl

# bootstrap transpiler
SKIP0 := $(SKIPALL) $(SKIPANON)
SKIP0 += test/1060-fn-ptr.spl
SKIP0 += test/1049-arrays-and-structs.spl
SKIP0 += test/3100-sequentialize.spl
SKIP0 += test/2050-err-return-from-void.spl
SKIP0 += test/2051-err-return-void.spl
SKIP0 += test/2080-err-need-cast-i32-arg.spl
SKIP0 += test/2081-err-need-cast-u32-arg.spl
SKIP0 += test/2082-err-need-cast-i32-var.spl
SKIP0 += test/2083-err-need-cast-u32-var.spl

# stage1 compiler
SKIP1 := $(SKIPALL)
SKIP1 += test/1040-structs.spl

# stage2 compiler
SKIP2 := $(SKIPALL)
SKIP2 += test/1040-structs.spl

# stage3 compiler
SKIP3 := $(SKIPALL) $(SKIPANON)
SKIP3 += test/1040-structs.spl

ifneq (,$(filter noskip,$(MAKECMDGOALS)))
SKIP0 :=
SKIP1 :=
SKIP2 :=
SKIP3 :=
endif
noskip:

