#!/usr/bin/env python3
"""Build both JC8012 images; never discovers ports or flashes hardware."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "firmware/jc8012p4"


def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def preview(folder):
    cc = shutil.which("cc")
    if not cc:
        raise SystemExit("The software preview needs a host C compiler (cc).")
    binary = folder / "preview-renderer"
    run(cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", BOARD / "main", "-I", BOARD / "common",
        "-I", ROOT / "firmware/esp32/main", ROOT / "tools/ldn/jc8012_preview.c",
        BOARD / "main/dashboard.c", BOARD / "main/events.c",
        ROOT / "firmware/esp32/main/screen.c", ROOT / "firmware/esp32/main/scene.c", "-o", binary)
    with (folder / "preview.ppm").open("wb") as output:
        run(binary, stdout=output)
    try:
        from PIL import Image
    except ImportError:
        print(f"Preview: {folder / 'preview.ppm'} (Pillow adds a PNG copy)")
    else:
        Image.open(folder / "preview.ppm").save(folder / "preview.png")
        print(f"Preview: {folder / 'preview.png'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", action="store_true", help="also render a sample event monitor")
    parser.add_argument("--preview-only", action="store_true", help="render without ESP-IDF or a board")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/jc8012p4")
    args = parser.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    if args.preview or args.preview_only:
        preview(folder)
    if args.preview_only:
        return
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise SystemExit("Load ESP-IDF v6.1 with its export.sh before running this script.")
    idf = Path(idf_path) / "tools/idf.py"
    images = []
    for project, target, name in ((BOARD / "radio", "esp32c6", "pokechanger-c6.bin"),
                                   (BOARD, "esp32p4", "pokechanger-p4.bin")):
        run(sys.executable, idf, "-C", project, "build")
        image = folder / name
        run(sys.executable, idf, "-C", project, "merge-bin", "-o", image)
        metadata = json.loads((project / "build/project_description.json").read_text())
        images.append({"file": name, "target": target, "flash_offset": "0x0",
                       "version": metadata["project_version"],
                       "sha256": hashlib.sha256(image.read_bytes()).hexdigest()})
    manifest = {"board": "JC8012P4A1C_I_W_Y", "hardware_tested": False,
                "p4_silicon": "v0.1-v1.x (default build; v3.x requires a separate configuration)",
                "images": images}
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (folder / "SHA256SUMS").write_text("".join(f"{i['sha256']}  {i['file']}\n" for i in images))
    print(f"Built both images in {folder}. No hardware was flashed.")


if __name__ == "__main__":
    main()
