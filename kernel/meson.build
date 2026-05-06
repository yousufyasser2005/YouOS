kernel_c_args = [
  '-ffreestanding',
  '-fno-exceptions',
  '-mno-red-zone',
  '-mno-mmx',
  '-mno-sse',
  '-mno-sse2',
  '-nostdlib',
  '-mcmodel=kernel',
  '-O2',
  '-Wall',
  '-Wextra',
  '-I' + meson.project_source_root() / 'include',
]

nasm = find_program('nasm')

kernel_asm_gen = generator(nasm,
  output    : '@BASENAME@.o',
  arguments : ['-f', 'elf64', '@INPUT@', '-o', '@OUTPUT@'],
)

kernel_asm_objects = kernel_asm_gen.process(
  'arch/x86_64/boot.asm',
  'arch/x86_64/gdt_flush.asm',
  'arch/x86_64/idt_stubs.asm',
  'arch/x86_64/idt_load.asm',
  'arch/x86_64/irq_stubs.asm',
)

executable('youos.elf',
  'arch/x86_64/kernel_main.c',
  'arch/x86_64/gdt.c',
  'arch/x86_64/idt.c',
  'arch/x86_64/irq.c',
  'arch/x86_64/pic.c',
  'drivers/vga.c',
  'mm/pmm.c',
  'mm/vmm.c',
  'mm/heap.c',
  objects   : kernel_asm_objects,
  c_args    : kernel_c_args,
  link_args : [
    '-T', meson.project_source_root() / 'kernel/linker.ld',
    '-nostdlib',
    '-lgcc',
  ],
  install   : false,
)
