#!/usr/bin/env python3
"""Migrate an ESP-Hosted C6 through P4 SDIO, then restore and verify the P4 monitor.

Requires the merged P4 installer, merged P4 monitor and raw C6 application.
Makes a local backup of every P4 flash sector the installer will overwrite.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from pokeldn.ldn import esp32


def arm(port, digest, log):
    link = serial.Serial(baudrate=115200, timeout=.1, write_timeout=3)
    link.dtr = link.rts = False
    link.port = port
    link.open()
    try:
        link.rts = True
        time.sleep(.1)
        link.rts = False
        data = bytearray()
        armed = False
        until = time.monotonic() + 180
        while time.monotonic() < until:
            chunk = link.read(link.in_waiting or 1)
            if not chunk:
                continue
            log.write(chunk)
            log.flush()
            data.extend(chunk)
            if not armed and b'READY_FOR_INSTALL' in data:
                if ('sha256=' + digest).encode() not in data:
                    raise RuntimeError('Embedded C6 image differs from --c6-app; no INSTALL sent')
                # Discard any partial line sampled while routing UART pins at boot.
                link.write(('\nINSTALL ' + digest + '\n').encode())
                link.flush()
                armed = True
                print('C6 image checksum verified; OTA requested.', flush=True)
            if b'INSTALLER_STOPPED' in data:
                raise RuntimeError('Installer rejected migration; inspect installer.log')
            if b'OTA_ACTIVATED' in data:
                print('C6 accepted and activated the application.', flush=True)
                return
        raise TimeoutError('Installer did not activate OTA; inspect installer.log')
    finally:
        link.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--installer', required=True, type=Path)
    parser.add_argument('--monitor', required=True, type=Path)
    parser.add_argument('--c6-app', required=True, type=Path)
    parser.add_argument('--backup-dir', required=True, type=Path, help='new local directory; never commit it')
    parser.add_argument('--flash-baud', type=int, choices=(115200, 230400), default=115200)
    parser.add_argument('--leave-trial', action='store_true', help='test timed rollback instead of confirming C6')
    args = parser.parse_args()
    # All inputs must exist before the first hardware operation.
    installer = args.installer.read_bytes()
    monitor = args.monitor.read_bytes()
    app = args.c6_app.read_bytes()
    def merged_p4(image):
        return len(image) >= 0x10000 and image[0x2000] == 0xe9 and int.from_bytes(image[0x200c:0x200e], 'little') == 18
    if not merged_p4(installer) or not merged_p4(monitor) or len(app) < 512 or app[0] != 0xe9 or int.from_bytes(app[12:14], 'little') != 13:
        parser.error('Expected merged P4 images and a raw ESP32-C6 application')
    args.backup_dir.mkdir(parents=True, exist_ok=False)
    digest = hashlib.sha256(app).hexdigest()
    (args.backup_dir / 'c6-app.sha256').write_text(digest + '\n')
    def flash_tool(*command, after='no-reset'):
        subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32p4',
                        '--port', args.port, '--baud', str(args.flash_baud), '--after', after,
                        *map(str, command)], check=True)
    sectors = (len(installer) + 4095) // 4096 * 4096
    flash_tool('read-flash', '0', hex(sectors), args.backup_dir / 'p4-before.bin')
    try:
        flash_tool('write-flash', '0', args.installer)
        with (args.backup_dir / 'installer.log').open('wb') as log:
            arm(args.port, digest, log)
    finally:
        # Restore even if OTA or the serial arming failed.
        flash_tool('write-flash', '0', args.monitor, after='hard-reset')
    radio = esp32.Radio.open_serial(args.port, fast_baud=0)
    try:
        print(radio.info.text, flush=True)
        status = radio.status()
        print(status, flush=True)
        trial = 'ota_pending=1' in status
        if not re.search(r'ota_fallback=0x[1-9a-fA-F][0-9a-fA-F]*', status) or not (trial or 'ota_pending=0' in status):
            raise RuntimeError('Expected C6 OTA guard and a recoverable previous application; not confirming')
        bench = radio.bench(65536, size=1024, timeout=20)
        print('BENCH:', bench, flush=True)
        if bench['bytes'] != 65536 or bench['missing'] or bench['rejected']:
            raise RuntimeError('C6 data test failed; leave trial unconfirmed for rollback')
        if not trial:
            print('This exact C6 image was already confirmed earlier. P4 monitor restored.', flush=True)
        elif not args.leave_trial:
            radio.request(0x10, b'PKLD', esp32.MSG_RESULT)
            status = radio.status()
            if 'ota_pending=0' not in status:
                raise RuntimeError('C6 did not confirm the trial')
            print('C6 trial confirmed. P4 monitor restored.', flush=True)
        else:
            print('Trial unconfirmed: C6 returns to previous firmware after 120 seconds from boot.', flush=True)
    finally:
        radio.close()


if __name__ == '__main__':
    main()
