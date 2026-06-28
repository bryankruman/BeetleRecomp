# Build the recomp patches (C compiled to MIPS) into patches.elf.
# Adapted from Zelda64Recompiled/patches/Makefile. LATER-phase: not needed for the
# first recompile-and-boot experiment.
TARGET := patches.elf
CC     ?= clang
LD     ?= ld.lld

# Point these at the BAR decomp's headers (the ELF's source of truth). Default is the
# lib/bar-decomp submodule. Reading headers on NTFS is fine; do NOT build the decomp
# from here (see README -> "Relationship to the decomp").
DECOMP_INCLUDE ?= ../lib/bar-decomp/include
DECOMP_SRC     ?= ../lib/bar-decomp/src

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg \
            -mno-check-zero-division -fomit-frame-pointer -ffast-math \
            -fno-unsafe-math-optimizations -fno-builtin-memset
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -I dummy_headers \
            -I $(DECOMP_INCLUDE) -I $(DECOMP_SRC) -I ../lib/rt64/include
LDFLAGS  := -nostdlib -T patches.ld -T syms.ld -Map patches.map \
            --unresolved-symbols=ignore-all --emit-relocs

C_SRCS  := $(wildcard *.c)
O_FILES := $(C_SRCS:.c=.o)

$(TARGET): $(O_FILES)
	$(LD) $(LDFLAGS) -o $@ $(O_FILES)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -f $(O_FILES) $(TARGET) patches.map patches.bin

.PHONY: clean
