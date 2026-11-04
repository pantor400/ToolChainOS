#include "power.h"
#include <stddef.h>
#include "console.h"
#include "gui.h"
#include "io.h"
#include "storage.h"

/* El apagado usa el registro PM1a_CNT del bloque ACPI fijo: SLP_TYP ocupa los
 * bits 12:10 y SLP_EN el 13. En un PC el tipo 5 es el estado S5 (apagado) y en
 * QEMU el 0; cada firmware acepta uno de los dos, asi que se prueban ambos.
 *
 * Leer la FADT para obtener el puerto real exige acceder a la memoria
 * reservada baja (0xF0000 y alrededores), que el HHDM de Limine no mapea; por
 * eso se usan los puertos de los firmwares que emula la demo, iguales en QEMU
 * q35/i440fx y en Bochs. */
#define PM1_SLP_EN (1u << 13)
#define PM1_CNT_S5 ((uint16_t)((5u << 10) | PM1_SLP_EN))
#define PM1_CNT_QEMU ((uint16_t)PM1_SLP_EN)
#define PORT_QEMU_PM1A 0x604
#define PORT_BOCHS_PM1A 0xB004

/* Definida en cpu.asm; cli + hlt sin retorno. */
extern void asm_detener(void) __attribute__((noreturn));

void power_off(const char *reason) {
    console_write("POWER: APAGADO SOLICITADO");
    if (reason && *reason) {
        console_write(" (");
        console_write(reason);
        console_write(")");
    }
    console_putc('\n');
    /* Primero se devuelve la pantalla a la consola: el escritorio no debe
     * quedar dibujado con la energia a punto de cortarse. */
    if (gui_active()) {
        console_write("POWER: CERRANDO EL ESCRITORIO\n");
        gui_shutdown();
    }
    storage_sync();
    /* El orden importa: el firmware que atiende una escritura apaga la maquina
     * en el acto, asi que las siguientes no llegan a ejecutarse. */
    console_write("POWER: S5 ENVIADO (SLP_TYP=5 SLP_EN=1; QEMU CON SLP_TYP=0)\n");
    outw(PORT_QEMU_PM1A, PM1_CNT_S5);
    outw(PORT_QEMU_PM1A, PM1_CNT_QEMU);
    outw(PORT_BOCHS_PM1A, PM1_CNT_S5);
    outw(PORT_BOCHS_PM1A, PM1_CNT_QEMU);
    /* Con -no-shutdown QEMU detiene la maquina (evento QMP SHUTDOWN) sin
     * terminar el proceso; en hardware real la alimentacion ya se corto. Si
     * el firmware ignora S5, la CPU queda detenida en lugar de girar. */
    console_write("POWER: EL EQUIPO NO SE APAGO; CPU DETENIDA\n");
    asm_detener();
}
