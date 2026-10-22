# Instalador temporal del C6 desde el P4

Host ESP-Hosted 3.0.9 / esp_wifi_remote 1.6.5 con ESP-IDF v6.1. Usa SDIO y GPIO54;
no necesita una red Wi-Fi. El proyecto por defecto solo consulta el coprocesador.

```sh
python scripts/build_jc8012_installer.py          # consulta sin escrituras C6
python scripts/build_jc8012_installer.py --ota    # incluye la aplicación C6
```

Las dos configuraciones usan directorios separados dentro de `build/` y no comparten
sdkconfig. El segundo comando compila primero la radio. El binario combinado P4 se carga
a 0x0; `pokechanger-c6-app.bin` es una aplicación OTA, sin bootloader ni particiones C6.
Nunca enviar por OTA el combinado `pokechanger-c6.bin` destinado a UART.

El instalador verifica chip, descriptor y SHA256 de la aplicación y espera 120 segundos
la línea `INSTALL <sha256>` por UART0 a 115200, GPIO37/38. Solo entonces llama a OTA_BEGIN,
OTA_WRITE y OTA_END. ESP-Hosted anterior a 2.6 activa desde OTA_END; versiones nuevas usan
OTA_ACTIVATE. No se modifica el bootloader ni la tabla de particiones del C6.

Una vez compilados también los firmwares normales, el controlador de PC guarda los sectores
P4 afectados, instala el auxiliar, envía la orden con el hash verificado, restaura el monitor
P4 incluso si falla la OTA, comprueba HELLO/BENCH y confirma el arranque del C6:

```sh
python tools/ldn/jc8012_ota.py --port PUERTO_USB_P4 \
  --installer dist/jc8012p4/pokechanger-installer-ota.bin \
  --monitor dist/jc8012p4/pokechanger-p4.bin \
  --c6-app dist/jc8012p4/pokechanger-c6-app.bin \
  --backup-dir /ruta/local/nueva/copia-migracion
```

Requiere pyserial y esptool en el Python usado. La copia es de la región P4 alterada, no
de los 16 MB completos; conservarla localmente. Una interrupción de alimentación o del
proceso de PC puede requerir volver a cargar el monitor P4 mediante esptool.

La migración desde ESP-Hosted 2.1.10, la vuelta temporizada y la segunda instalación con
confirmación se han probado en la unidad P4 v1.3. La herramienta usa 115200 para flashear;
`--flash-baud 230400` también pasó en esa unidad. La velocidad del instalador en ejecución
sigue siendo 115200.

La radio C6 busca y verifica una aplicación anterior en otra partición. Si la encuentra,
arma una prueba de 120 segundos y limita a ocho los arranques no confirmados. Al caducar
vuelve a esa aplicación. `--leave-trial` permite probar esa vuelta sin confirmar.
STATUS muestra `ota_pending` y `ota_fallback`. La confirmación usa comando 0x10 + `PKLD`;
0x11 + `PKLD` vuelve explícitamente a la aplicación anterior. Estos comandos se reservan
en el transporte de esta placa, sin alterar el firmware genérico del upstream.
Una radio cargada por UART en la partición factory no activa esta prueba OTA.

Una imagen idéntica ya confirmada conserva esa confirmación al reinstalarla; no abre otra
prueba temporizada.

Tras confirmar, no volver a ejecutar ESP-Hosted directamente sobre la radio PKLD: primero
hay que pedir 0x11 y dejar arrancar la aplicación ESP-Hosted conservada. La recuperación
por UART/CN5 sigue disponible para una unidad sin ESP-Hosted OTA compatible.
