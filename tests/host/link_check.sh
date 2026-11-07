#!/usr/bin/env bash
# Approximate link check for the kernel: compile every kernel C file with the REAL build flags
# (from kernel/meson.build), then make sure every symbol some object needs is defined by another
# object or by an assembly file. It would have caught "undefined reference to ycfs_write_chunk"
# before a real build.
#
# Compiler: x86_64-elf-gcc (the one the real build uses) when it is installed, so the answer is exact.
# Otherwise the host gcc is used, and compiler-runtime helpers (memcpy, memset, ...) are NOT reported:
# whether code needs them depends on the compiler version, and only the real compiler can say. Missing
# project functions are reported either way.
#   bash tests/host/link_check.sh          (exits 1 if something the kernel needs is undefined)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); cd "$ROOT"
W=$(mktemp -d /tmp/youos_link.XXXXXX); trap 'rm -rf "$W"' EXIT
if command -v x86_64-elf-gcc >/dev/null 2>&1; then CC=x86_64-elf-gcc; EXACT=1; else CC=gcc; EXACT=0; fi
FLAGS="-ffreestanding -fno-exceptions -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -mcmodel=kernel -O2 -fno-pic -fno-stack-protector -I include -w"
bad=0
for f in $(find kernel -name '*.c' | sort); do
  $CC -c $FLAGS "$f" -o "$W/$(echo "$f" | tr '/' '_').o" 2>"$W/err" || { echo "COMPILE FAIL: $f"; head -3 "$W/err"; bad=1; }
done
nm --defined-only "$W"/*.o | awk '$2 ~ /^[TDBRWVtdbr]$/ {print $3}' | sort -u > "$W/def_c"
grep -rhoE "^\s*(global|GLOBAL)\s+[A-Za-z0-9_, ]+" kernel --include=*.asm | sed -E 's/^\s*(global|GLOBAL)\s+//' | tr ',' '\n' | tr -d ' ' | sort -u > "$W/def_asm"
grep -oE "[A-Za-z_][A-Za-z0-9_]*\s*=" kernel/linker.ld | tr -d ' =' | sort -u > "$W/def_ld"      # symbols the linker script provides
nm -u "$W"/*.o | awk '/ U /{print $2}' | sort -u > "$W/needed"
sort -u "$W/def_c" "$W/def_asm" "$W/def_ld" > "$W/all"
# irq0..15 / isr0..N come from NASM macros, which this check cannot expand
comm -23 "$W/needed" "$W/all" | grep -Ev '^(irq|isr)[0-9]+$|^_GLOBAL_OFFSET_TABLE_$' > "$W/missing" || true
RUNTIME='^(mem(cpy|set|move|cmp)|str(len|cmp|cpy)|__.*)$'
if [ $EXACT -eq 0 ]; then
  grep -E "$RUNTIME" "$W/missing" > "$W/ignored" || true
  grep -Ev "$RUNTIME" "$W/missing" > "$W/m2" || true; mv "$W/m2" "$W/missing"
fi
if [ -s "$W/missing" ]; then echo "UNDEFINED in the kernel (the real link would fail):"; sed 's/^/   /' "$W/missing"; bad=1
else
  echo "kernel link check ($CC): every referenced symbol is defined ($(wc -l < "$W/needed") referenced, $(wc -l < "$W/all") defined)"
  if [ -s "$W/ignored" ]; then echo "  note: not judged with the host compiler (the cross compiler decides): $(tr '\n' ' ' < "$W/ignored")"; fi
fi
exit $bad
