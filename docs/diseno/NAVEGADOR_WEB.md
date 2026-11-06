# ToolchainOS Web — arquitectura y límites

Implementación integrada sobre el kernel existente mediante ABI 6 y COM3.
Este documento describe los fuentes versionados. Los respaldos, ISO, paquetes,
capturas y registros locales de la implementación no forman parte del commit.
La validación de integración se resume en
[VALIDACION_TOOLCHAINOS_WEB.md](../guias/VALIDACION_TOOLCHAINOS_WEB.md).

## Decisión

App ELF64 PIE freestanding apps/navegador.c; ABI 6 añadido al final
de host_api_t; UART COM3 (0x3E8); puente Python de biblioteca estándar.
Se recompila el kernel existente para extender la API, sin sustituirlo.
RTL8139 conserva ARP/IPv4/ICMP/DNS: NO implementa TCP, HTTP ni TLS nativos.
HTTP, DNS del navegador y TLS se ejecutan en Ubuntu. RED INTERNET no demuestra
que se puedan descargar páginas.

~~~mermaid
flowchart LR
  U[URL / enlace / historial] --> A[navegador.elf: GUI y render]
  A --> K[ABI 6: start / poll / cancel]
  K --> C[COM3 TW/1]
  C --> B[web_bridge.py: política de destinos]
  B --> H[GET HTTP o HTTPS verificado]
  H --> P[HTMLParser: texto y enlaces acotados]
  P --> C
  C --> A
~~~

COM1=consola; COM2=TC/1 IA/Ollama; COM3=TW/1 web.
Con --web sin --ai, el lanzador reserva COM2 mediante -serial null.
Con ambos, COM2=IA y COM3=web. La GUI atiende también ai_poll().
Sin --web, no se inicia puente ni se crea socket web.

## ABI y eventos

Campos v1–v5 en su mismo orden; v6 añade web_start(const char*),
web_poll(void*,uint32_t), web_cancel() y escape_events(int).
Todas las apps se recompilan; el Makefile depende del header.
ESC se entrega a la app solo cuando lo solicita expresamente; así el navegador
sale de edición antes de cerrar, manteniendo las otras apps compatibles.

web_start encola y retorna. Cada poll tiene presupuestos máximos de 256 bytes TX
y 4096 RX, sin bucles de espera UART. Retorna 0 pendiente, longitud positiva
de documento, -1 sin puente, -2 timeout, -3 protocolo/tamaño inválido.
Se exige ACK en 2 segundos y fin en 15 segundos desde ACK.
La ventana atiende teclado, ratón, repintado y cierre durante carga.
Cancelar transmite de mejor esfuerzo sin esperar; los IDs descartan respuestas
tardías incluso cuando la cancelación no llega al puente.
Solo una app ELF a la vez, ring 0, sin aislamiento de usuario.

## TW/1

Cabecera little endian de 12 bytes: formato Python <2sBBIHH.

| Campo | Bytes | Valor |
|---|---:|---|
| magic | 2 | ASCII TW |
| versión | 1 | 1 |
| tipo | 1 | operación |
| ID | 4 | monotónico, no cero |
| longitud | 2 | 0–1024 |
| checksum | 2 | suma módulo 65536 de cabecera 0–9 y payload |

Tipos: 1 GET(URL UTF-8), 3 cancelar, 0x80 ACK vacío, 0x81 fragmento,
0x82 fin vacío. Se envían fragmentos de 512 bytes.
Hay lecturas parciales, concatenación, ruido y resincronización.
Checksum detecta corrupción accidental; no es autenticación criptográfica.
Socket privado y mismo usuario constituyen la frontera de confianza.

Documento máximo 24576 bytes: <HHHHH> = estado HTTP (0 si error del puente),
bytes URL final, bytes título, bytes mensaje, número de registros.
Siguen las tres cadenas UTF-8 sin terminadores. Cada registro <BHH> =
tipo (0 texto/lista, 1 encabezado, 2 enlace), bytes texto y bytes destino;
después ambas cadenas. La app valida el documento completo antes de sustituir
la página y comprueba límites antes de copiar.

## Recursos

| Recurso | Límite |
|---|---:|
| URL | 1023 bytes UTF-8 |
| descarga | 256 KiB, sin descompresión |
| documento hacia huésped | 24 KiB |
| registros / enlaces | 128 / 48 |
| historial | 8 direcciones |
| vista envuelta | 480 líneas, hasta 143 bytes cada una |
| redirecciones | 4 |
| presupuesto host | 10 s por navegación |
| conexión / lectura | hasta 3 s por operación |
| descarga | 1 worker y 1 petición pendiente |

Buffers C estáticos en imagen ELF/PMM; no se aumenta ni consume el heap
compartido de 512 KiB para las páginas. Transporte: buffer estático 24 KiB.
El cargador libera imagen, pila y superficie al salir. La prueba compara
PMM USED y heap tras diez aperturas completas.
Se corrigió una fuga previa del escritorio: su doble buffer se asignaba
en cada sesión pero no se liberaba al cerrarla.

## Seguridad, DNS y HTML

Solo GET http/https, puertos 80/443; sin cookies, formularios, credenciales,
proxy del entorno ni shell. Se rechazan localhost, nombres locales y todas
las direcciones privadas, loopback, link-local, multicast, reservadas,
IPv4-mapped IPv6 y túneles IPv6. Se validan todos los resultados DNS y cada
redirección. La conexión se fija a una IP numérica validada. HTTPS conserva
hostname/SNI y usa ssl.create_default_context().
No hay segunda resolución del destino al conectar. Evita la carrera habitual
de rebinding dentro del proceso; no garantiza protección absoluta ante un
anfitrión comprometido o rutas/VPN maliciosas.

Se buscan registros A (IPv4). Literales IPv6 se validan, pero no se busca AAAA
para dominios. Debido a DNS WSL intermitente, se limita la espera del sistema
a 1 s con máximo dos threads. Si falla, se consulta DNS-over-HTTPS de Cloudflare,
IP fija 1.1.1.1 y hostname cloudflare-dns.com, TLS verificado y máximo 16 KiB.
Todas sus IP pasan la misma política. No se modifica DNS de Windows ni WSL.
Referencia: [Cloudflare DNS JSON](https://developers.cloudflare.com/1.1.1.1/encryption/dns-over-https/make-api-requests/dns-json/).

El lanzador usa umask 077, sockets del usuario, chmod 0600 y guarda dispositivo/
inodo. Al cerrar solo elimina el mismo socket creado para su QEMU.
El puente conecta a QEMU; no escucha TCP, no expone 0.0.0.0 y no borra sockets.
El mismo usuario puede alterar sus procesos; no se pretende aislarlo.

Solo text/html y text/plain; se rechazan binarios/PDF/multimedia y compresión.
HTMLParser interpreta encabezados, párrafos, listas y enlaces contra URL final.
Entidades y UTF-8 español se conservan. Script/style/noscript/svg/template
se omiten, nunca se ejecutan. Se muestra aviso de HTML básico y JavaScript
no ejecutado. Una página dinámica puede no ser utilizable.

## Alcance pendiente

NO IMPLEMENTADO: TCP/TLS nativos, CSS completo, JavaScript, formularios, login,
cookies, multimedia, extensiones, búsqueda, pestañas y descarga de archivos.
PARCIAL: semántica HTML aproximada; enlaces en líneas propias y estilos simples;
historial acotado vuelve a descargar, sin cache. Sin rueda en el ABI:
se usan flechas, Home/End y pista vertical.

Transporte nativo requerirá TCP con retransmisiones/ventanas/timeouts, sockets,
HTTP robusto, certificados, reloj fiable y TLS. La API permite sustituir
transporte conservando la app.

## Evidencia

Las pruebas generan resultados y capturas auténticas de QEMU en artifacts/.
Son salidas locales ignoradas por Git. Fixtures solo en test_web_qemu.py; no habilitan
localhost para navegación normal. Inferencia Ollama real se reporta separada
de su transporte simulado.

La fuente de contenido del navegador es la UI de 15 px existente en todas
las resoluciones (scale negativo del ABI 6). Evita solapar líneas al pasar
a 1920x1080; el resto del escritorio conserva sus métricas originales.
Se corrigió la cola lineal de teclado de 16 bytes: se sustituye por una
cola circular SPSC atómica de 128 bytes, con publicación de escalares UTF-8
completos. La prueba host cubre reutilización, wrap y saturación; GUI comprueba
que HELP seguido de escritura no pierde el final del comando ni Enter.

El presupuesto total COM3 es 30 s: hasta 10 s de red más ~16 s para 24 KiB
en una UART sondeada a 100 Hz con FIFO de 16 bytes. La GUI continúa atendiendo
eventos. El puente rechaza representaciones mayores de 24 KiB antes de enviarlas;
el cliente conserva su propia validación independiente de tamaño y framing.
