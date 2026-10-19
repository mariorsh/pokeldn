---
title: JC8012 P4/C6 event monitor
parent: Hardware and setup
nav_order: 3
---

# PokeChanger — JC8012P4A1C_I_W_Y

Adaptación experimental de [Decryptu/pokeldn](https://github.com/Decryptu/pokeldn) para usar
la placa como comunicador USB con la Switch y mostrar los eventos en su pantalla de 10,1 pulgadas.
**Ambos firmwares compilan con ESP-IDF v6.1. USB y pantalla ya se han probado en una placa real.**
La radio C6 y la comunicación con una Switch siguen pendientes de validación: el C6 de la unidad
probada responde a SDIO, pero todavía no contiene el firmware de radio de esta adaptación.
Las pruebas de USB/pantalla no demuestran un intercambio ni una entrega de regalos por radio.

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
Los demás comandos llegan al C6. Cada registro SDIO contiene versión, tipo, longitud LE y payload.
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
de la placa del usuario. Flash P4: 16 MB; PSRAM a 200 MHz como en la demo publicada.

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

**Estado:** alternativa documentada por Espressif; todavía no probada en esta unidad. Hay que
consultar la versión y capacidades del coprocesador con un host ESP-Hosted compatible y comprobar
las particiones OTA, el espacio y las validaciones de imagen que aplique su firmware. No se debe
confundir disponer de estas APIs en la biblioteca del P4 con que el C6 instalado las implemente.
La instalación OTA requiere una aplicación C6 compatible con esas validaciones y un procedimiento
de recuperación/confirmación de arranque. No basta con añadir el componente esp_hosted al P4.

El archivo combinado `pokechanger-c6.bin` contiene bootloader y tabla de particiones y **no** es
el payload de una OTA de aplicación. La aplicación está en `radio/build/pokechanger_radio.bin`;
su uso por OTA todavía requiere validar el arranque con el bootloader y particiones del C6 instalado.
Hasta preparar y verificar esa migración, el procedimiento operativo de este fork sigue siendo UART.

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
python -m pytest tests/test_jc8012p4.py tests/test_esp32_screen.py tests/test_board_flash.py tests/test_esp32.py -q
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
- SDIO inicializa y lee los registros del C6; la firma leída es 00000000, en lugar de PKLD.
  El firmware P4 informa `bridge_stage=4 bridge_ready=0` y no inventa una identidad de radio.
- Una copia local verificada de los primeros 384 KiB (`0x0`–`0x5FFFF`) preserva toda la región
  alterada al instalar esta imagen; no es una copia de los 16 MB completos.
- Las lecturas largas de flash fallaron a 921600 y 460800. Para lectura/escritura de flash se usó
  115200, aunque el intercambio corto de comandos USB a 460800 sí pasó.

Repetir la prueba del P4, incluso sin el C6 instalado:

```sh
python tools/ldn/jc8012_probe.py --port PUERTO_USB_P4 --duration 20 --output /ruta/local/probe.json
```

El script espera a STATUS tras un posible reset del CH340, envía comandos de pantalla y prueba el
cambio de velocidad. Su resultado de radio queda separado del resultado del P4; añadir
`--require-radio` para exigir también HELLO del C6. STATUS incluye `p4_revision`, `psram_bytes`,
`lcd_frames`, `p4_wire_bad`, `p4_wire_dropped`, `bridge_stage` y `c6_signature`.
Las fases son 0: inicio; 1: bus SDIO; 2: tarjeta; 3: lectura de firma; 4: firmware C6 incompatible;
5: radio lista; 6: enlace perdido.

Pendiente: comprobar primero la migración C6 mediante ESP-Hosted OTA por SDIO; si no resulta
compatible, cargar el firmware por CN5. Después, probar
HELLO completo y BENCH, estabilidad SDIO bajo tráfico de radio, red LDN y una consola, un intercambio
y una entrega real de Mystery Gift. Comprobar además desconexión USB, reinicio conjunto P4/C6
y cierre de red por watchdog. El touch queda fuera de esta primera versión.

La licencia AGPL-3.0 y los créditos del upstream se conservan. La documentación original continúa
aplicándose a los juegos y a la app.
