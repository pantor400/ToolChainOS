# Ejecutar con GNU Make en Linux x86-64 o WSL Ubuntu x86-64.
# Opcional: make CROSS=x86_64-elf- (si ese toolchain esta instalado).
CROSS ?=
CC := $(CROSS)gcc
LD := $(CROSS)ld
OBJDUMP := $(CROSS)objdump
OBJCOPY := $(CROSS)objcopy
READELF := $(CROSS)readelf
NM := $(CROSS)nm
NASM := nasm
HOST_CC := cc
# Sanitizers del anfitrion. En distribuciones sin libasan/libubsan se puede
# desactivar con "make SAN= test-memory-host"; por defecto quedan activos.
SAN ?= address,undefined
SANFLAGS := $(if $(strip $(SAN)),-fsanitize=$(SAN))
QEMU ?= qemu-system-x86_64
# Resolucion solicitada a Limine. 1280x720 cabe en cualquier pantalla con la
# ventana del emulador a escala 1:1; los perfiles 1920x1080 y 1280x800 siguen
# disponibles con `make run RES=1920x1080` y `make run RES=1280x800`.
RES ?= 1280x720
# Ventana del emulador del tamano exacto del modo del huesped: sin zoom, QEMU
# abre 1280x720 pixeles reales y no reescala. Para llenar la pantalla del
# anfitrion: make run DISPLAY_QEMU=gtk,zoom-to-fit=on
DISPLAY_QEMU ?= gtk,zoom-to-fit=off
# Pantalla completa: make run FULLSCREEN=1 (util con 1920x1080).
FULLSCREEN ?= 0
BUILD := build
KERNEL := $(BUILD)/kernel.elf
ISO := $(BUILD)/toolchain-os.iso
# Cargador UEFI que se copia a la ISO. secureboot-iso lo cambia por la copia
# firmada que genera scripts/secure_boot.py; el arranque normal no cambia.
EFI_BOOT ?= vendor/limine/BOOTX64.EFI
SB_DIR := $(BUILD)/secureboot
SB_EFI := $(SB_DIR)/BOOTX64.EFI
SB_ISO := $(BUILD)/toolchain-os-secureboot.iso
CSRC := $(wildcard src/*.c)
# Imagenes del escritorio: el anfitrion convierte images/*.jpg y *.png a un
# blob RGB565 y objcopy lo enlaza como datos de solo lectura del kernel.
IMAGES_BIN := $(BUILD)/assets/images.bin
IMAGES_OBJ := $(BUILD)/images_blob.o
OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(CSRC)) $(BUILD)/cpu.o $(BUILD)/interrupt_stubs.o $(BUILD)/context.o $(BUILD)/elf_enter.o $(BUILD)/linux_enter.o $(IMAGES_OBJ)
APPS := $(patsubst apps/%.c,$(BUILD)/apps/%.elf,$(wildcard apps/*.c))
# Opciones de xorriso de la ISO.
XORRISO := xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label
CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin \
 -fno-stack-protector -fno-pic -fno-pie -m64 -mno-red-zone -mcmodel=kernel \
 -mgeneral-regs-only -ffunction-sections -fdata-sections -MMD -MP -Iinclude -Ivendor/limine
LDFLAGS := -nostdlib -static -z max-page-size=0x1000 --gc-sections -T linker.ld
LIBGCC := $(shell $(CC) -print-libgcc-file-name)
# Aceleracion: KVM da velocidad nativa y evita el parpadeo del escritorio; si
# no esta disponible se usa TCG. Forzar con: make run ACCEL=tcg
ACCEL ?= $(if $(wildcard /dev/kvm),kvm,tcg)
CPU ?= $(if $(filter kvm,$(ACCEL)),host,max)
# Memoria de video del dispositivo VGA estandar (32 MiB cubre 1920x1080x32).
VGAMEM ?= 32
QFLAGS := -machine q35 -accel $(ACCEL) -cpu $(CPU) -m 256M -smp 2 \
 -vga std -global VGA.vgamem_mb=$(VGAMEM) \
 -cdrom $(ISO) -boot d -serial stdio -monitor none -no-reboot -no-shutdown \
 -rtc base=localtime,clock=host \
 -netdev user,id=net0 -device rtl8139,netdev=net0 \
 $(if $(filter 1,$(FULLSCREEN)),-full-screen)
.PHONY: all iso run headless debug trace inspect check clean uefi apps fonts images images-check
.PHONY: test-usb usb-lista secureboot-claves secureboot-iso test-secureboot
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
$(IMAGES_BIN): $(wildcard images/*) scripts/build_images.py | $(BUILD)
	python3 scripts/build_images.py --size $(RES) --output $@
# La seccion .rodata.images entra en el segmento de solo lectura de linker.ld.
# objcopy deriva los simbolos de la ruta de entrada: se ejecuta desde $(BUILD)
# con una ruta relativa para que no dependan de donde este el arbol de build.
$(IMAGES_OBJ): $(IMAGES_BIN)
	cd $(BUILD) && $(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 \
	 --rename-section .data=.rodata.images,alloc,load,readonly,data,contents \
	 assets/images.bin images_blob.o
$(KERNEL): $(OBJ) linker.ld
	$(LD) $(LDFLAGS) -Map=$(BUILD)/kernel.map -o $@ $(OBJ) $(LIBGCC)
$(BUILD)/limine: vendor/limine/limine.c vendor/limine/limine-bios-hdd.h | $(BUILD)
	$(HOST_CC) -O2 -std=c99 $< -o $@
# Sella la resolucion pedida: el sello solo cambia cuando RES cambia, asi que
# la ISO se reconstruye al cambiar de perfil y no en cada invocacion.
RES_STAMP := $(BUILD)/resolution.stamp
# Con -n no se toca el sello: un ensayo no debe alterar el estado del build.
ifneq (,$(filter n,$(MAKEFLAGS)))
RES_SYNC :=
else
RES_SYNC := $(shell mkdir -p $(BUILD); echo '$(RES)' | cmp -s - $(RES_STAMP) || echo '$(RES)' > $(RES_STAMP))
endif
$(ISO): $(KERNEL) $(APPS) $(BUILD)/limine $(RES_STAMP) limine.conf vendor/limine/limine-bios.sys vendor/limine/limine-bios-cd.bin vendor/limine/limine-uefi-cd.bin $(EFI_BOOT)
	mkdir -p $(BUILD)/iso_root/boot/limine $(BUILD)/iso_root/EFI/BOOT $(BUILD)/iso_root/boot/apps
	# Limpia modulos de compilaciones anteriores (apps renombradas o retiradas).
	rm -f $(BUILD)/iso_root/boot/apps/*.elf
	rm -rf $(BUILD)/iso_root/boot/linux
	cp $(KERNEL) $(BUILD)/iso_root/boot/kernel.elf
	cp $(APPS) $(BUILD)/iso_root/boot/apps/
	cp limine.conf vendor/limine/limine-bios.sys vendor/limine/limine-bios-cd.bin vendor/limine/limine-uefi-cd.bin $(BUILD)/iso_root/boot/limine/
	sed -i 's/^\([[:space:]]*resolution:\).*/\1 $(RES)x32/' $(BUILD)/iso_root/boot/limine/limine.conf
	cp $(EFI_BOOT) $(BUILD)/iso_root/EFI/BOOT/BOOTX64.EFI
	$(XORRISO) $(BUILD)/iso_root -o $@
	$(BUILD)/limine bios-install $@
run: $(ISO)
	@echo "QEMU: ACCEL=$(ACCEL) CPU=$(CPU) RES=$(RES) FULLSCREEN=$(FULLSCREEN)"
	$(QEMU) $(QFLAGS) -display $(DISPLAY_QEMU)
headless: $(ISO)
	$(QEMU) $(QFLAGS) -display none
# GDB solo escucha en loopback; evita exponer el depurador a la red.
debug: $(ISO)
	$(QEMU) $(QFLAGS) -display $(DISPLAY_QEMU) -S -gdb tcp:127.0.0.1:1234
trace: $(ISO)
	$(QEMU) $(QFLAGS) -display $(DISPLAY_QEMU) -d int -D $(BUILD)/interrupts.log
# OVMF_CODE se puede sobrescribir si la distribucion usa otra ruta.
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS_4M.fd
uefi: $(ISO)
	$(QEMU) $(QFLAGS) -display $(DISPLAY_QEMU) -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) -drive if=pflash,format=raw,snapshot=on,file=$(OVMF_VARS)

# Arranque desde un pendrive: la ISO es hibrida (El Torito BIOS + UEFI), de
# modo que la misma imagen sirve para dd. "usb-lista" solo enumera unidades;
# escribir exige confirmacion explicita del usuario.
usb-lista: $(ISO)
	python3 scripts/escribir_usb.py listar
secureboot-claves:
	python3 scripts/secure_boot.py claves
$(SB_EFI): scripts/secure_boot.py vendor/limine/BOOTX64.EFI
	mkdir -p $(SB_DIR)
	python3 scripts/secure_boot.py firmar
# Reconstruye la ISO con el cargador firmado, guarda la copia y deja de nuevo
# la ISO normal para que el resto de objetivos siga igual.
secureboot-iso: $(SB_EFI)
	rm -f $(ISO)
	$(MAKE) --no-print-directory EFI_BOOT=$(SB_EFI) $(ISO)
	cp $(ISO) $(SB_ISO)
	rm -f $(ISO)
	$(MAKE) --no-print-directory $(ISO)
test-secureboot: iso
	python3 scripts/test_secureboot.py
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


.PHONY: smoke test-files test-core run-ata test-ata test-persistence run-ai test-ai-bridge test-ai-real test-agent test-uefi test-all disk
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
test-agent: iso
	python3 scripts/test_agent.py
test-ai-real: iso
	python3 scripts/test_ai_real.py
test-uefi: iso
	python3 scripts/test_uefi.py
test-usb: iso
	python3 scripts/test_usb.py
test-all:
	python3 scripts/test_all.py

# V3.1: teclado PS/2 bilingue y texto UTF-8. Sin cambios en perfiles originales.
.PHONY: test-utf8-host test-keyboard test-utf8-persistence test-ai-utf8
test-utf8-host: | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(SANFLAGS) -Iinclude -Ivendor/limine scripts/test_utf8_host.c src/utf8.c src/keyboard.c src/glyph.c src/fs.c -o $(BUILD)/test-utf8-host
	$(BUILD)/test-utf8-host
test-keyboard: iso
	python3 scripts/test_utf8_keyboard.py
test-utf8-persistence: iso
	python3 scripts/test_utf8_persistence.py
test-ai-utf8: iso
	python3 scripts/test_ai_utf8.py

# Avance academico E56 sobre V3.1
.PHONY: test-gui test-memory-host test-scheduler
test-gui: iso
	python3 scripts/test_gui.py

test-memory-host: | $(BUILD)
	$(HOST_CC) -std=c11 -g -Wall -Wextra -Werror $(SANFLAGS) -Iinclude -Ivendor/limine scripts/test_memory_host.c src/pmm.c src/heap.c -o $(BUILD)/test-memory-host
	$(BUILD)/test-memory-host
test-scheduler: iso
	python3 scripts/test_memory_scheduler.py
# Apagado nativo: APAGAR cierra la GUI y corta la energia por ACPI S5.
test-apagar: iso
	python3 scripts/test_apagar.py

.PHONY: test-scheduler-host test-apagar
test-scheduler-host: $(BUILD)/context.o
	$(HOST_CC) -std=c11 -O2 -g -Wall -Wextra -Werror -fno-omit-frame-pointer -Iinclude -Ivendor/limine scripts/test_scheduler_host.c src/tasks.c src/pmm.c src/heap.c $(BUILD)/context.o -o $(BUILD)/test-scheduler-host
	$(BUILD)/test-scheduler-host

# Aplicaciones ELF: PIE estatico sin libc; el cargador del kernel las reubica
# con R_X86_64_RELATIVE. Se copian a la ISO como modulos Limine.
APP_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -fno-builtin \
 -fno-stack-protector -fpie -fno-plt -mno-red-zone -mgeneral-regs-only \
 -fno-asynchronous-unwind-tables -Iinclude
$(BUILD)/apps/%.elf: apps/%.c apps/app.ld | $(BUILD)
	mkdir -p $(BUILD)/apps
	$(CC) $(APP_CFLAGS) -nostdlib -static-pie -Wl,-e,main -Wl,--no-dynamic-linker \
	 -Wl,--build-id=none -T apps/app.ld -o $@ $<
apps: $(APPS)
	@echo "Aplicaciones: $(APPS)"
# Regenera las tipografias horneadas (Pillow + fuente del sistema).
fonts:
	python3 scripts/build_fonts.py
# Regenera el blob RGB565 de images/ que usan el fondo y el visor.
images: $(IMAGES_BIN)
	@echo "Imagenes: $(IMAGES_BIN)"
# Comprueba que Pillow y images/ estan disponibles para el build.
images-check:
	python3 scripts/build_images.py --check

# Personalidad Linux: ejecuta el BusyBox de Alpine descargado con
# scripts/linux_bin.py. El binario viaja como modulo Limine en una ISO
# aparte, de modo que la ISO entregada no cambia si falta.
LINUX_BIN := vendor/linux-bin/busybox.static
LINUX_ISO := $(BUILD)/toolchain-os-busybox.iso
$(LINUX_ISO): $(ISO) $(LINUX_BIN)
	@test -f $(LINUX_BIN) || { echo "Falta el binario: ejecuta make linux-bin"; exit 1; }
	rm -rf $(BUILD)/iso_bin
	cp -a $(BUILD)/iso_root $(BUILD)/iso_bin
	mkdir -p $(BUILD)/iso_bin/boot/bin
	cp $(LINUX_BIN) $(BUILD)/iso_bin/boot/bin/busybox.static
	awk '{print} /module_path: boot\(\):\/boot\/apps\/terminal.elf/ \
	 {print "    module_path: boot():/boot/bin/busybox.static"}' \
	 $(BUILD)/iso_bin/boot/limine/limine.conf > $(BUILD)/iso_bin/limine.conf
	mv $(BUILD)/iso_bin/limine.conf $(BUILD)/iso_bin/boot/limine/limine.conf
	$(XORRISO) $(BUILD)/iso_bin -o $@
	$(BUILD)/limine bios-install $@
linux-bin:
	python3 scripts/linux_bin.py
linux-bin-check:
	@python3 scripts/linux_bin.py --check
run-linux: $(LINUX_ISO)
	python3 scripts/run_vm.py --linux-bin $(VM_ARGS)
test-linux-bin: $(LINUX_ISO)
	python3 scripts/test_linux_bin.py
.PHONY: linux-bin linux-bin-check run-linux test-linux-bin
