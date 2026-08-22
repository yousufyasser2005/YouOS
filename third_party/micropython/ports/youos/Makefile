# Include the core environment definitions; this will set $(TOP).
include ../../py/mkenv.mk

# Include py core make definitions.
include $(TOP)/py/py.mk

MICROPY_ROM_TEXT_COMPRESSION ?= 0

# Real freestanding cross toolchain, matching user/Makefile exactly.
CROSS_COMPILE = x86_64-elf-
LD = x86_64-elf-ld

CFLAGS += -I. -Iinclude -I$(TOP) -I$(BUILD) -I$(TOP)/../../user/lib
CFLAGS += -Wall -std=gnu99
CFLAGS += -ffreestanding -fno-stack-protector -mno-red-zone \
          -mno-mmx -mno-sse -mno-sse2 -O2 -fno-pic -mcmodel=large

SRC_C += main.c gccollect.c libc_shim.c
SRC_C += shared/runtime/gchelper_generic.c
SRC_C += extmod/modhashlib.c

# py.mk's default SRC_QSTR only scans py/ core files (see py/py.mk:254),
# not this port's own SRC_C -- any new MP_QSTR_ identifier introduced in
# main.c (e.g. a custom object's type name) needs to be listed here too,
# or qstr auto-extraction silently never sees it and the build fails at
# the final compile step with "undeclared" instead of at qstr-gen time.
# (Found the hard way in the host sandbox before this ever touched the
# real build.)
SRC_QSTR += main.c
SRC_QSTR += extmod/modhashlib.c

OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

all: $(BUILD)/mpycore.o

# Partial link: combine everything into one relocatable object, so
# user/Makefile's existing "$(LD) $(LDFLAGS) $(CRT0) $< -o $@" pattern
# (built for a single input .o) keeps working with zero changes to how
# the other four programs link.
$(BUILD)/mpycore.o: $(OBJ)
	$(ECHO) "LD -r -> $@"
	$(Q)$(LD) -r -o $@ $^

include $(TOP)/py/mkrules.mk
