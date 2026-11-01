# Ejecutar con GNU Make en Linux x86-64 o WSL Ubuntu x86-64.
# Opcional: make CROSS=x86_64-elf- (si ese toolchain esta instalado).
CROSS ?=
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJDUMP := $(CROSS)objdump
READELF := $(CROSS)readelf
NM := $(CROSS)nm
NASM := nasm
HOST_CC := cc
QEMU ?= qemu-system-x86_64
BUILD := build
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/toolchain-os.iso
CSRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(CSRC)) $(BUILD)/cpu.o $(BUILD)/interrupt_stubs.o $(BUILD)/context.o
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin \
 -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel \
 -mgeneral-regs-only -ffunction-sections -fdata-sections -MMD -MP -Iinclude -Ivendor/limine
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 --gc-sections -T linker.ld
LIBGCC := $(shell $(CC) -print-libgcc-file-name)
QFLAGS := -machine q35 -vga std -m 256M -smp 2 -cdrom $(ISO) -boot d -serial stdio -monitor none -no-reboot -no-shutdown
.PHONY: all iso run headless debug trace inspect check clean uefi
all: $(KERNEL)
iso: $(ISO)
$(BUILD):
	mkdir -p $@
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: src/%.asm | $(BUILD)
	$(NASM) -f elf64 -g -F dwarf $< -o $@
$(BUILD)/cpu.o: src/cpu.asm | $(BUILD)
	$(NASM) -f elf64 -g -F dwarf $< -o $@
$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -Map=$(BUILD)/kernel.map -o $@ $(OBJ) $(LIBGCC)
$(BUILD)/limine: vendor/limine/limine.c vendor/limine/limine-bios-hdd.h | $(BUILD)
	$(HOST_CC) -O2 -std=c99 $< -o $@
$(ISO): $(KERNEL) $(BUILD)/limine limine.conf vendor/limine/limine-bios.sys vendor/limine/limine-bios-cd.bin vendor/limine/limine-uefi-cd.bin vendor/limine/BOOTX64.EFI
	mkdir -p $(BUILD)/iso_root/boot/limine $(BUILD)/iso_root/EFI/BOOT
	cp $(KERNEL) $(BUILD)/iso_root/boot/kernel.elf
	cp limine.conf vendor/limine/limine-bios.sys vendor/limine/limine-bios-cd.bin vendor/limine/limine-uefi-cd.bin $(BUILD)/iso_root/boot/limine/
	cp vendor/limine/BOOTX64.EFI $(BUILD)/iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label $(BUILD)/iso_root -o $@
	$(BUILD)/limine bios-install $@
run: $(ISO)
	$(QEMU) $(QFLAGS)
headless: $(ISO)
	$(QEMU) $(QFLAGS) -display none
# GDB solo escucha en loopback; evita exponer el depurador a la red.
debug: $(ISO)
	$(QEMU) $(QFLAGS) -S -gdb tcp:127.0.0.1:1234
trace: $(ISO)
	$(QEMU) $(QFLAGS) -d int -D $(BUILD)/interrupts.log
# OVMF_CODE se puede sobrescribir si la distribucion usa otra ruta.
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
uefi: $(ISO)
	$(QEMU) $(QFLAGS) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,snapshot=on,file=$(OVMF_VARS)
inspect: $(KERNEL)
	$(READELF) -h -l -S $(KERNEL) > $(BUILD)/elf.txt
	$(OBJDUMP) -d -M intel $(KERNEL) > $(BUILD)/disassembly.txt
	$(NM) -n $(KERNEL) > $(BUILD)/symbols.txt
	@echo "Informes: build/elf.txt, disassembly.txt, symbols.txt y kernel.map"
check: $(KERNEL)
	python3 scripts/check_elf.py $(KERNEL)
clean:
	rm -rf $(BUILD)
-include $(OBJ:.o=.d)


.PHONY: smoke test-files test-core run-ata test-ata test-persistence run-ai test-ai-bridge test-ai-real test-uefi test-all disk
smoke: iso
	python3 scripts/smoke_test.py
test-files: iso
	python3 scripts/test_files_editor.py
test-core: iso
	python3 scripts/test_core.py
disk:
	python3 scripts/create_test_disk.py
run-ata: iso
	python3 scripts/run_vm.py --ata $(VM_ARGS)
test-ata test-persistence: iso
	python3 scripts/test_persistence.py
run-ai: iso
	python3 scripts/run_vm.py --ai $(VM_ARGS)
test-ai-bridge: iso
	python3 scripts/test_ai.py
test-ai-real: iso
	python3 scripts/test_ai_real.py
test-uefi: iso
	python3 scripts/test_uefi.py
test-all:
	python3 scripts/test_all.py

# V3.1: teclado PS/2 bilingue y texto UTF-8. Sin cambios en perfiles originales.
.PHONY: test-utf8-host test-keyboard test-utf8-persistence test-ai-utf8
test-utf8-host: | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Ivendor/limine scripts/test_utf8_host.c src/utf8.c src/keyboard.c src/glyph.c src/fs.c -o $(BUILD)/test-utf8-host
	$(BUILD)/test-utf8-host
test-keyboard: iso
	python3 scripts/test_utf8_keyboard.py
test-utf8-persistence: iso
	python3 scripts/test_utf8_persistence.py
test-ai-utf8: iso
	python3 scripts/test_ai_utf8.py

# Avance academico E56 sobre V3.1
.PHONY: test-memory-host test-scheduler
test-memory-host: | $(BUILD)
	$(HOST_CC) -std=c11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Ivendor/limine scripts/test_memory_host.c src/pmm.c src/heap.c -o $(BUILD)/test-memory-host
	$(BUILD)/test-memory-host
test-scheduler: iso
	python3 scripts/test_memory_scheduler.py

.PHONY: test-scheduler-host
test-scheduler-host: $(BUILD)/context.o
	$(HOST_CC) -std=c11 -O2 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -Iinclude -Ivendor/limine scripts/test_scheduler_host.c src/tasks.c src/pmm.c src/heap.c $(BUILD)/context.o -o $(BUILD)/test-scheduler-host
	$(BUILD)/test-scheduler-host
