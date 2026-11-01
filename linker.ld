/* Limine x86-64: nucleo higher-half, NO forzado a 0x100000 fisico.
 * Limine decide su ubicacion fisica; el comando INFO la consulta. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)
PHDRS {
    requests PT_LOAD FLAGS(6); /* lectura/escritura */
    text PT_LOAD FLAGS(5);     /* lectura/ejecucion */
    rodata PT_LOAD FLAGS(4);   /* solo lectura */
    data PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0xffffffff80000000;
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :requests
    . = ALIGN(0x1000); /* 0x1000 = 4096 bytes = 4 KiB */
    .text : { *(.text .text.*) } :text
    . = ALIGN(0x1000);
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .bss : { *(COMMON) *(.bss .bss.*) } :data
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
