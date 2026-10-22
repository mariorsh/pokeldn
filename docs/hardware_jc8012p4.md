---
title: JC8012 P4/C6 event monitor
parent: Hardware and setup
nav_order: 3
---

# PokeChanger — JC8012P4A1C_I_W_Y

Adaptación experimental de [Decryptu/pokeldn](https://github.com/Decryptu/pokeldn) para usar
la placa como comunicador USB con la Switch y mostrar los eventos en su pantalla de 10,1 pulgadas.
**Ambos firmwares compilan con ESP-IDF v6.1. USB y pantalla ya se han probado en una placa real.**
La radio C6 ya se ha instalado mediante OTA por SDIO sin adaptador externo y responde a HELLO,
BENCH, SNIFF y AP_START. El 22 de octubre de 2026 se completó un intercambio FireRed/LeafGreen
desde la app del Mac por USB. En iPhone se verificaron Bluetooth y pantalla; el intercambio iOS
y la entrega real de regalos siguen pendientes.

## Funcionamiento

```text
App pokeldn en ordenador
       | USB-C / CH340C / UART0 (COBS + CRC32, protocolo original)
       v
ESP32-P4: puente USB, pantalla de eventos, escenas y sprites de la app
       | SDIO de 4 bits a 20 MHz, registros PKLD, protocolo interno v1
       v
ESP32-C6: firmware de radio pokeldn, claves CCMP y hooks Wi-Fi originales
       | LDN / Pia, 2,4 GHz
       v
Nintendo Switch
```

La app sigue ejecutando la autenticación LDN, la lógica de cada juego y la gestión de los datos.
El P4 no sustituye al ordenador. La radio necesita el firmware específico del C6: el firmware
ESP-Hosted de fábrica no contiene los hooks privados WPA que usa pokeldn. Instalar solamente el
firmware P4 no habilita la comunicación con la consola. No se utiliza esp_wifi remoto en el P4.

El monitor pasivo muestra conexión USB, disponibilidad del C6, canal, consolas conectadas,
tramas Ethernet recibidas y enviadas, errores de comandos y los ocho eventos más recientes.
La zona central reutiliza las escenas originales de intercambio, Pokémon y Mystery Gift de la app,
ampliadas a 640 × 320. Los eventos no guardan las claves ni los paquetes de juego.
TX cuenta comandos de envío remitidos, no confirmaciones por radio. Las horas son minutos/segundos
desde el arranque, no hora de calendario.

El protocolo USB mantiene sus créditos y cambio de baudrate en el P4; DISPLAY se ejecuta localmente.
Los demás comandos llegan al C6. La app limita esta placa a 460800 baudios, incluso si el ajuste
general pide 921600 o 1500000; la prueba real a 921600 perdió HELLO. Los demás modelos conservan
sus velocidades habituales y la búsqueda de puerto reconoce también una sesión dejada a 460800. Cada registro SDIO contiene versión, tipo, longitud LE y payload.
El C6 mantiene **un único envío SDIO pendiente** hasta que el P4 lo ha leído completo; esto conserva
la frontera de registros sobre ESSL, cuya API informa de los bytes disponibles. No debe aumentarse
el número de envíos simultáneos sin añadir un reensamblador de registros en el P4.
El C6 conserva el watchdog de la app: ALIVE cada segundo y cierre de red tras cinco segundos de silencio.

![Previsualización del monitor de eventos](../.github/assets/jc8012-preview.png)

*Ejemplo simulado con el renderer del firmware; no es una fotografía de la placa.*

## Hardware verificado contra las fuentes publicadas

Fuentes consultadas: [esquemas y demo de la placa](https://github.com/wegi1/ESP32P4-JC8012P4A1C-I_W_Y/tree/0c1bbcfdbaa87bbc4686cde98b7deb556711064d),
[SDIO de ESP-IDF](https://github.com/espressif/esp-idf/tree/v6.1/examples/peripherals/sdio)
y [controlador JD9365 de Espressif](https://components.espressif.com/components/espressif/esp_lcd_jd9365/versions/2.0.2).
La correspondencia se ha comprobado en los esquemas. Se ha observado comunicación UART/SDIO
y una imagen correcta en la unidad probada, sin medir señales con osciloscopio.

| Señal | GPIO P4 | GPIO C6 |
|---|---:|---:|
| SDIO CLK | 18 | 19 |
| SDIO CMD | 19 | 18 |
| SDIO D0 | 14 | 20 |
| SDIO D1 | 15 | 21 |
| SDIO D2 | 16 | 22 |
| SDIO D3 | 17 | 23 |
| C6 EN / reset | 54 | EN |
| UART0 hacia CH340C | TX 37, RX 38 | — |
| LCD reset | 27 | — |
| Retroiluminación PWM | 23 | — |
| BOOT | 35 | — |
| Touch GSL3680 | SDA 7, SCL 8, INT 21, RST 22 | — |

La pantalla es JD9365, MIPI-DSI de dos carriles, RGB565, 800 × 1280 en orientación vertical.
El driver usa LDO3 a 2500 mV y los registros y timings de la demo publicada: reloj de píxel
48 MHz, H 20/20/40 y V 8/4/20 (back porch / pulse / front porch). La demo utiliza 63 MHz;
se ha reducido a 48 MHz durante la prueba real para dar margen al acceso a PSRAM.
`main/panel_registers.h` transcribe los valores de inicialización de esa demo.
El lector táctil GSL3680 queda pendiente; el monitor actual no requiere interacción táctil.
BOOT despierta la escena y la retroiluminación baja tras un minuto sin actividad y se apaga tras diez.
El renderer dibuja en dos framebuffers de scanout en PSRAM y cambia de búfer al finalizar
las transferencias, sin copiar un canvas adicional. Espera dos finalizaciones por cambio para
cubrir la carrera entre la selección del búfer y el ISR. Se habilita ejecución de código y rodata
desde PSRAM (XIP). La primera prueba con 63 MHz, copia por CPU y sin XIP mostró azul uniforme;
la combinación actual mostró el monitor y la tarjeta de prueba correctamente. No se ha añadido LVGL.

## Compilar

Se requiere **ESP-IDF v6.1**; los hooks de radio del upstream dependen de su ABI privada.
El SDK v5.2 de la demo del fabricante no es compatible con esos hooks.

```sh
. /ruta/a/esp-idf/export.sh
python scripts/build_jc8012.py
```

Genera `dist/jc8012p4/pokechanger-p4.bin`, `pokechanger-c6.bin`, `manifest.json` y `SHA256SUMS`.
Son imágenes combinadas con bootloader y tabla de particiones, destinadas a flashearse a **0x0**
en el chip correspondiente. El script no busca puertos ni escribe en una placa.
Los proyectos separados son `firmware/jc8012p4` (P4) y `firmware/jc8012p4/radio` (C6).
Se fijan las dependencias JD9365 2.0.2 y ESSL 1.1.0; CMake declara para ESSL las dependencias de
drivers y el include de heap que ya no recibe de forma transitiva en IDF 6.1.

**Revisión del P4:** la configuración por defecto sigue la demo de la placa y admite silicio
v0.1–v1.x (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y`, revisión mínima 0.1). ESP-IDF 6.1 separa
esas revisiones de v3.x. Consultar primero `esptool --chip esp32p4 --port PUERTO chip-id`.
Para una placa con P4 v3.x hay que cambiar la revisión en `idf.py -C firmware/jc8012p4 menuconfig`
y recompilar; la imagen por defecto no sirve para ambas familias. No se ha confirmado la revisión
de otras unidades. La placa probada tiene revisión v1.3. Flash P4: 16 MB; PSRAM a 200 MHz como en la demo publicada.

Previsualización sin placa ni ESP-IDF (compilador C local; Pillow opcional para PNG):

```sh
python scripts/build_jc8012.py --preview-only
```

Renderiza un ejemplo de regalo con el mismo `dashboard.c` y `scene.c` del firmware en
`dist/jc8012p4/preview.ppm`. Si está instalado Pillow, crea también `preview.png`.
Es una simulación del contenido de pantalla, no una captura de la placa.

## Alternativa sin adaptador: ESP-Hosted Transport OTA

Espressif documenta la [actualización del coprocesador a través del host](https://components.espressif.com/components/espressif/esp_hosted/versions/2.9.7/examples/host_performs_slave_ota?language=en).
El P4 puede enviar una aplicación nueva al C6 por el propio SDIO, sin UART externo ni conexión
a una red Wi-Fi: el archivo puede estar en la flash del P4. Por tanto, **CN5 no es necesariamente
la única forma de instalar la radio**. La afirmación inicial de que un adaptador era obligatorio
no había considerado esta vía.

Que el firmware anterior pudiera configurar una red Wi-Fi es coherente con el uso del C6 mediante
ESP-Hosted. Eso no expone por sí solo los hooks WPA privados de pokeldn, pero ofrece un posible
camino para instalar una aplicación de radio que sí los contenga. La firma `00000000` observada
solo demuestra que el C6 no habla todavía el protocolo PKLD; no demuestra que carezca de OTA.

**Probado en esta unidad:** el host temporal ESP-Hosted 3.0.9 / esp_wifi_remote 1.6.5
con ESP-IDF v6.1 consultó el C6 instalado, versión **2.1.10**, y completó OTA_BEGIN,
705488 bytes de OTA_WRITE y OTA_END con ESP_OK. La nueva aplicación arrancó, publicó la
firma PKLD y respondió a HELLO/STATUS a través del monitor P4 restaurado.
No fue necesario cambiar de USB-C ni conectar un adaptador a CN5.

El instalador por defecto solo consulta el coprocesador. Para habilitar la migración hay
que compilar con `--ota`; sigue requiriendo una línea INSTALL con el SHA256 exacto de la
aplicación embebida. Se configura UART0 explícitamente y se descarta la entrada parcial
del arranque antes de procesar esa línea.

```sh
python scripts/build_jc8012.py
python scripts/build_jc8012_installer.py --ota
python tools/ldn/jc8012_ota.py --port PUERTO_USB_P4 \
  --installer dist/jc8012p4/pokechanger-installer-ota.bin \
  --monitor dist/jc8012p4/pokechanger-p4.bin \
  --c6-app dist/jc8012p4/pokechanger-c6-app.bin \
  --backup-dir /ruta/local/nueva/copia-migracion
```

El controlador guarda los sectores P4 afectados, instala el auxiliar, verifica el hash,
ejecuta la OTA y restaura el monitor P4 incluso si falla. Después exige HELLO, una aplicación
anterior recuperable y BENCH de 64 KiB sin pérdidas antes de confirmar el C6. El primer arranque
OTA tiene 120 segundos de prueba y ocho arranques como máximo sin confirmación; conserva la
aplicación anterior en otra partición. `--leave-trial` deja vencer esa prueba.
[Detalles del instalador y recuperación](../firmware/jc8012p4/installer/README.md).

La vuelta automática se probó dejando vencer los 120 segundos: el siguiente instalador volvió
a consultar ESP-Hosted 2.1.10. Se repitió la OTA, pasó BENCH de 64 KiB y se confirmó el arranque;
tras reiniciar, STATUS mantuvo `ota_pending=0` y la radio siguió respondiendo.

La OTA cambia solamente la aplicación C6; conserva su bootloader y tabla de particiones.
El combinado `pokechanger-c6.bin`, con bootloader y particiones, sirve para UART, **no para OTA**.
El payload OTA es `pokechanger-c6-app.bin` (copia de `radio/build/pokechanger_radio.bin`).
La compatibilidad observada corresponde a esta unidad y su ESP-Hosted 2.1.10; otras versiones
pueden aplicar validaciones diferentes o carecer de OTA.

## iPhone por Bluetooth

La radio C6 incluye el mismo transporte BLE de pokeldn: servicio `POKELDN-RADIO`,
COBS, CRC y créditos. El iPhone ejecuta la app completa y el C6 mantiene el enlace Wi-Fi con
la Switch. La placa necesita alimentación USB; el cable no transporta los datos del iPhone.
Instalar la [app iOS](../examples/ios/README.md), permitir Bluetooth y configurar las claves
propias de la consola en la carpeta local de la app.

El P4 conserva el monitor: muestra BLE como transporte, observa los comandos de radio y
recibe las escenas DISPLAY por SDIO. Cada DISPLAY devuelve al iPhone la confirmación del P4.
Los paquetes masivos no se duplican en la pantalla. Solo puede controlar la radio un host a la vez:
con Bluetooth conectado, los comandos USB de radio se rechazan. Al cambiar de transporte se
cierra la sesión anterior. Para volver al Mac, cerrar la app del iPhone y reconectar por USB.

Verificado el 22 de octubre de 2026 en iPhone 16 Pro Max con iOS 27.0.1:

- App compilada, firmada e instalada; claves locales copiadas con autorización del propietario.
- AES y zstd correctos; PKHeX cargó las 386 especies de FRLG.
- 200 HELLO por BLE, media de 32,0 ms e intervalo BLE de 15 ms.
- Tarjeta «iPhone conectado» confirmada por el P4 y BENCH de 16384 bytes sin pérdidas ni rechazos.
- Nueva radio C6 confirmada tras BENCH USB de 65536 bytes sin pérdidas.

El intercambio desde iOS se probará después; estas comprobaciones no lo dan por completado.
El C6 publica SDIO antes de inicializar Bluetooth y el P4 espera hasta diez segundos por la
firma PKLD al arrancar. Para instalar esta versión se amplió la copia local del P4 a los primeros
`0x19e000` bytes; se conserva también la aplicación anterior del C6 para recuperación OTA.

## Primera instalación por UART (vía directa)

1. Comprobar la revisión del P4, la placa y el pinout de CN5 contra los esquemas.
2. Cargar el C6 por su cabecera **CN5** usando un adaptador UART de lógica **3,3 V**.
   El CH340C del USB-C programa el P4; no programa automáticamente el C6.
3. Cargar el P4 por el USB-C de programación.
4. Reiniciar toda la placa. El P4 reinicia también el C6 por GPIO54 e inicia SDIO.
5. En la app, seleccionar el puerto CH340 de la placa y utilizar las herramientas de juegos habituales.
   Los datos y claves que pide la app se configuran como en el upstream.

CN5 según el esquema `5_ESP32-C6.png`: 1 = 3V3, 2 = GND, 3 = C6 TXD,
4 = C6 RXD, 5 = C6 IO9 (BOOT), 6 = C6 EN. Cruzar TX del adaptador con RX del C6 y RX con TX,
y compartir GND. Mantener IO9 a GND durante el reset del C6 para entrar en el cargador ROM;
soltar IO9 para el arranque normal. No conectar el pin de 5 V de un adaptador a CN5.
Al programar el C6, el P4 no debe reiniciarlo durante la transferencia: mantener el P4 en su ROM
loader (BOOT+RESET del P4) o usar el procedimiento de aislamiento del fabricante.
La alimentación se mantiene por USB; no se presupone alimentación adicional desde el adaptador.

Con ESP-IDF cargado y el chip correspondiente en su bootloader:

```sh
idf.py -C firmware/jc8012p4/radio -p PUERTO_UART_C6 flash
idf.py -C firmware/jc8012p4 -p PUERTO_USB_P4 flash
```

Para las imágenes combinadas, una vez comprobado el chip y su revisión:

```sh
esptool --chip esp32c6 --port PUERTO_UART_C6 write-flash 0x0 dist/jc8012p4/pokechanger-c6.bin
esptool --chip esp32p4 --port PUERTO_USB_P4 write-flash 0x0 dist/jc8012p4/pokechanger-p4.bin
```

La app reconoce la identidad ESP32-P4, conserva las MAC del C6 para LDN y no ofrece actualizaciones
con el firmware genérico del C6. La pantalla Board de la app no instala aún este par de imágenes;
el flasheador de la app indica el procedimiento específico al detectar un P4.
El texto INFO usa `version=1.6.3` como versión de capacidades compatible con el upstream y
`board=JC8012P4A1C_I_W_Y radio=esp32c6` como identificación de esta adaptación experimental.

## Validación

Pruebas de registros SDIO (tamaños 0–1600, truncado, versión incompatible, overflow), estado del
monitor, errores, desconexión, watchdog con rollover, historial acotado, límites del framebuffer y selección de firmware:

```sh
python -m pytest tests/test_jc8012p4.py tests/test_esp32_screen.py tests/test_board_flash.py tests/test_esp32.py tests/test_jc8012_ota.py -q
```

Validación local del port inicial: ambos firmwares compilados y combinados; 117 pruebas de la
selección anterior pasaron. Las pruebas nativas se ejecutan con AddressSanitizer y UBSan.

### Prueba de placa real — 19 de octubre de 2026

- P4 revisión v1.3, flash de 16 MB y PSRAM de 32 MB detectados.
- Imagen P4 cargada y verificada mediante hash por esptool.
- Monitor y tarjeta «Prueba USB» confirmados visualmente por el usuario después de corregir el azul uniforme.
- DISPLAY repetido, rechazo de un DISPLAY malformado, cambio a 460800 baudios y vuelta a 115200 probados.
- Carga de 20 segundos: 1722 comandos DISPLAY aceptados, cero respuestas fallidas; el contador de
  imágenes avanzó y el P4 reportó cero `p4_wire_bad` y cero `p4_wire_dropped`.
- Con el C6 original, SDIO devolvía firma 00000000 y `bridge_stage=4 bridge_ready=0`.
  Tras la OTA devuelve PKLD (`444c4b50`), `bridge_stage=5 bridge_ready=1` y la identidad P4/C6.
- Los búferes SDIO del P4 usan RAM interna DMA y alineación de 64 bytes. La alineación inicial
  de cuatro bytes fallaba al pasar a transferencias por bloques; BENCH de 256, 508, 512, 768,
  1024 y 1400 bytes pasó después de la corrección, sin pérdidas ni CRC rechazados.
- BENCH: 65536 bytes a 115200; 134400 bytes a 460800 y otros 134400 a 230400, sin pérdidas
  ni CRC rechazados. Un comando de 1600 bytes en sentido PC → C6 recibió el rechazo esperado
  del C6 para un tipo desconocido, verificando también el transporte largo de subida.
- SNIFF recibió 482, 667 y 680 tramas Wi-Fi reales en los canales 1, 6 y 11, respectivamente.
  AP_START y STOP pasaron. Esto no demuestra asociación con una Switch ni un intercambio LDN.
- La recuperación temporizada devolvió ESP-Hosted 2.1.10; la segunda OTA quedó confirmada y
  persistió tras reiniciar. El usuario confirmó en pantalla la tarjeta «Radio C6 lista».
- La conexión real con el ajuste general de la app a 921600 usa ahora 460800 automáticamente.
- La copia local original se amplió antes de cada instalador hasta los primeros 1272 KiB
  (`0x0`–`0x13DFFF`), cubriendo los sectores P4 alterados. No es una copia de los 16 MB completos.
- Las lecturas largas de flash fallaron a 921600 y 460800. Para lectura/escritura de flash se usó
  115200 inicialmente; las últimas copias y escrituras del instalador también pasaron a 230400.
  Los comandos y BENCH USB de la app a 460800 pasaron; HELLO a 921600 falló.

Repetir la prueba del P4, incluso sin el C6 instalado:

```sh
python tools/ldn/jc8012_probe.py --port PUERTO_USB_P4 --duration 20 --output /ruta/local/probe.json
```

El script espera a STATUS tras un posible reset del CH340, envía comandos de pantalla y prueba el
cambio de velocidad. Su resultado de radio queda separado del resultado del P4; añadir
`--require-radio` para exigir también HELLO del C6. STATUS incluye `p4_revision`, `psram_bytes`,
`lcd_frames`, `p4_wire_bad`, `p4_wire_dropped`, `bridge_stage` y `c6_signature`.
Las fases son 0: inicio; 1: bus SDIO; 2: tarjeta; 3: lectura de firma; 4: firmware C6 incompatible;
5: radio lista; 6: enlace perdido. `sdio_error` conserva el error de recepción que causó la fase 6.
La radio C6 añade `ota_pending` y la dirección `ota_fallback` de la aplicación anterior verificada.

Pendiente: estabilidad prolongada bajo tráfico de radio, intercambio desde iOS
y una entrega real de Mystery Gift. Comprobar además desconexión USB, reinicio conjunto P4/C6
y cierre de red por watchdog. El touch queda fuera de esta primera versión.

La licencia AGPL-3.0 y los créditos del upstream se conservan. La documentación original continúa
aplicándose a los juegos y a la app.
