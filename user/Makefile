CC      = x86_64-elf-gcc
NASM    = nasm
LD      = x86_64-elf-ld

CFLAGS  = -ffreestanding -fno-stack-protector -mno-red-zone \
          -mno-mmx -mno-sse -mno-sse2 -O2 -nostdlib \
          -fno-pic -mcmodel=large

LDFLAGS = -T user.ld -nostdlib

CRT0    = lib/crt0.o
PROGS   = hello cat

all: $(PROGS:%=bin/%)

bin/:
	mkdir -p bin

lib/crt0.o: lib/crt0.asm
	$(NASM) -f elf64 $< -o $@

bin/hello: src/hello.o $(CRT0) | bin/
	$(LD) $(LDFLAGS) $(CRT0) $< -o $@

src/hello.o: src/hello.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f lib/*.o src/*.o bin/*

bin/cat: src/cat.o $(CRT0) | bin/
	$(LD) $(LDFLAGS) $(CRT0) $< -o $@

src/cat.o: src/cat.c
	$(CC) $(CFLAGS) -c $< -o $@
