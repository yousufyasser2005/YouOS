#!/usr/bin/env python3
"""Make a host-compilable copy of kernel/drivers/ata.c: the inline port-I/O helpers are replaced by
declarations, which tests/host/ata_mock_test.c implements as a model of an IDE controller.
usage: gen_ata_mock.py REPO_ROOT OUT_DIR   (writes OUT_DIR/ata_mock_src.c)"""
import os, sys
root, out = sys.argv[1], sys.argv[2]
s = open(os.path.join(root, "kernel/drivers/ata.c")).read()
a = s.index("/* Port I/O helpers */"); b = s.index("/* 400ns delay")
mock = ('#include <stdint.h>\n'
        'uint8_t inb(uint16_t p); void outb(uint16_t p, uint8_t v);\n'
        'uint16_t inw(uint16_t p); void outw(uint16_t p, uint16_t v);\n'
        'uint32_t inl(uint16_t p); void outl(uint16_t p, uint32_t v);\n'
        'void insw(uint16_t p, void* a, uint32_t n); void outsw(uint16_t p, const void* a, uint32_t n);\n')
os.makedirs(out, exist_ok=True)
open(os.path.join(out, "ata_mock_src.c"), "w").write(s[:a] + mock + s[b:])
