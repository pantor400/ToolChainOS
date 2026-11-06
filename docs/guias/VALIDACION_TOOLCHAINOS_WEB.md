# Validación de integración: ToolchainOS Web

Fecha: 6 de noviembre de 2026.
Base revisada: 5ea25fd9ca3360732e4daaff3678e3b0f9a47cd1 (main del repositorio oficial).

La base publicada coincidía con los fuentes previos del avance funcional.
Se integraron exclusivamente los cambios del navegador y sus regresiones,
conservando los demás archivos del equipo. No se detectaron conflictos.

## Comprobaciones ejecutadas en el clon

| Grupo | Resultado | Tiempo |
|---|---|---:|
| build | PASSED | 8.15 s |
| host-sanitizers | PASSED | 6.45 s |
| python-syntax | PASSED | 0.17 s |
| web-host | PASSED | 0.38 s |
| ai-protocol-host | PASSED | 0.22 s |
| web-qemu-fixture | PASSED | 50.02 s |
| gui-apps | PASSED | 30.85 s |
| ai-transport-qemu | PASSED | 9.05 s |
| web-launcher | PASSED | 15.68 s |
| web-real | PASSED | 15.99 s |

- Build con -Wall -Wextra -Werror y make check; navegador.elf integrado en Limine.
- Sanitizers: PMM/heap, UTF-8/glifos/FS, planificador, parser app (20000 entradas),
  cliente COM3 (framing/límites/IDs/timeouts) y cola circular PS/2 de 128 bytes.
- Puente: 9 casos de host; esquemas/credenciales, IP internas, TLS, HTML/UTF-8,
  redirección pública permitida y bloqueo de redirección interna.
- QEMU TCG: ayuda offline sin puente, apertura/cierre x10 con recuperación PMM/heap,
  enlaces relativos, historial, recarga, scroll, 404, timeout, TLS inválido simulado,
  contenido incompatible, exceso de tamaño, cancelación y reconexión del puente.
- GUI: editor abierto/guardado/solo lectura, terminal HELP/crear/leer y hoja CSV/formulas.
- IA: protocolo TC/1 y transporte COM2 con backend de prueba; convivencia COM2/COM3.
- Lanzador: sin web, --web y --ai --web; sockets 0600 y procesos hijos cerrados.
- Web real: HTTPS CERN (200), enlace HTTP al primer sitio (200), Atrás y HTTP example.com (200).

Las pruebas se ejecutaron secuencialmente: una sola VM, disco/NIC virtuales,
sin modificar Windows ni usar discos físicos. El puente tiene dependencia de
Internet del anfitrión y no demuestra TCP/TLS nativos del huésped.

## Límites de la verificación

- Inferencia Ollama real no verificada: no se configuró OLLAMA_MODEL ni se descargaron modelos.
- DNS externo del kernel dependió del relay QEMU/WSL; el test GUI comprueba envío de consulta
  y ping real a la puerta. No se declara éxito de DNS externo si el relay no responde.
- Perfiles 1280x720, 1280x800 y 1920x1080 y entrega válida de 23540 bytes ya se validaron
  en el avance de origen; el runtime se copió sin cambios y no se repitieron esos casos aquí.
- GitHub Actions no se ha ejecutado para esta rama mientras no esté publicada;
  se verificaron localmente los comandos de host añadidos al workflow.

Los registros y capturas generados quedan locales en build/publicacion/ y artifacts/.
No se versionan ISO, imágenes de prueba, sockets, respaldos, paquetes ni credenciales.
Reproducir con los comandos de [USO_NAVEGADOR.md](USO_NAVEGADOR.md).
