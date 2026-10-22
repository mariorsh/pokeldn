#!/usr/bin/env python3
"""Build the P4 ESP-Hosted probe, or an explicitly enabled C6 OTA installer. Never flashes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / 'firmware/jc8012p4'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ota', action='store_true', help='embed C6 app and enable serial INSTALL arming')
    args = parser.parse_args()
    if not os.environ.get('IDF_PATH'):
        parser.error('Load ESP-IDF v6.1 export.sh first')
    idf = Path(os.environ['IDF_PATH']) / 'tools/idf.py'
    project = BOARD / 'installer'
    build = project / 'build' / ('ota' if args.ota else 'probe')
    build.mkdir(parents=True, exist_ok=True)
    defaults = build / 'installer.defaults'
    defaults.write_text((project / 'sdkconfig.defaults').read_text() +
                        '\nCONFIG_POKECHANGER_C6_INSTALLER_WRITE=' + ('y' if args.ota else 'n') + '\n')
    run = lambda *cmd: subprocess.run([str(x) for x in cmd], check=True)
    if args.ota:
        run(sys.executable, idf, '-C', BOARD / 'radio', 'build')
    output = ROOT / 'dist/jc8012p4'
    output.mkdir(parents=True, exist_ok=True)
    name = 'pokechanger-installer-ota' if args.ota else 'pokechanger-hosted-probe'
    image = output / (name + '.bin')
    run(sys.executable, idf, '-C', project, '-B', build,
        '-D', 'SDKCONFIG=' + str(build / 'sdkconfig'),
        '-D', 'SDKCONFIG_DEFAULTS=' + str(defaults), 'merge-bin', '-o', image)
    record = {'target': 'esp32p4', 'flash_offset': '0x0', 'ota_enabled': args.ota,
              'sha256': hashlib.sha256(image.read_bytes()).hexdigest()}
    if args.ota:
        app = BOARD / 'radio/build/pokechanger_radio.bin'
        record['c6_app_sha256'] = hashlib.sha256(app.read_bytes()).hexdigest()
        record['c6_app_file'] = 'pokechanger-c6-app.bin'
        shutil.copyfile(app, output / record['c6_app_file'])
    (output / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
    print(f'Built {image}. No hardware was flashed.')


if __name__ == '__main__':
    main()
