/* Enlazado de aplicaciones ELF para ToolchainOS.
 * Base 0 + PIE: el cargador decide la direccion fisica y aplica
 * R_X86_64_RELATIVE. No hay libc ni enlazador dinamico. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(main)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0;
    .text : { *(.text .text.*) } :text
    . = ALIGN(0x1000);
    .rodata : { *(.rodata .rodata.*) } :text
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) *(.got*) } :data
    .bss : { *(COMMON) *(.bss .bss.*) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) *(.dynamic) *(.dynsym) *(.dynstr) }
}
