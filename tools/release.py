#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def firmware(destination):
    full = ROOT / 'build/FoloToy-AI-Passport-full.bin'
    full_hash = hashlib.sha256(full.read_bytes()).hexdigest()
    archive = ROOT / 'build/firmware' / full_hash
    subprocess.run(['python3', str(ROOT / 'tools/archive_firmware.py'), 'verify', str(archive)], check=True)
    destination.mkdir(parents=True, exist_ok=True)
    for name in ['FoloToy-AI-Passport-full.bin', 'FoloToy-AI-Passport.bin', 'manifest.json', 'flash_args',
                 'bootloader/bootloader.bin', 'partition_table/partition-table.bin']:
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(archive / name, target)


def mac_resources(application, source):
    manifest = json.loads((source / 'manifest.json').read_text())
    if (manifest.get('schema_version') != 1 or manifest.get('target') != 'esp32c3'
            or manifest.get('flash_size_bytes') != 8 * 1024 * 1024
            or not re.fullmatch(r'[0-9a-f]{64}', manifest.get('full_bin_sha256', ''))
            or manifest.get('image_offsets', {}).get('FoloToy-AI-Passport.bin') != 0x10000):
        raise ValueError('Unsupported firmware release manifest')
    required = {'FoloToy-AI-Passport-full.bin', 'FoloToy-AI-Passport.bin', 'flash_args',
                'bootloader/bootloader.bin', 'partition_table/partition-table.bin'}
    if not required.issubset(manifest['files']):
        raise ValueError('Incomplete firmware release manifest')
    for name, record in manifest['files'].items():
        if Path(name).is_absolute() or '..' in Path(name).parts or '\\' in name:
            raise ValueError('Unsafe firmware release path')
        path = source / name
        if name.endswith(('.elf', '.map')):
            continue
        if (not path.resolve().is_relative_to(source.resolve()) or path.is_symlink()
                or not path.is_file()):
            raise ValueError('Missing firmware release input')
        data = path.read_bytes()
        if len(data) != record['size'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError('Firmware release checksum mismatch')
    full = (source / 'FoloToy-AI-Passport-full.bin').read_bytes()
    app = (source / 'FoloToy-AI-Passport.bin').read_bytes()
    if (hashlib.sha256(full).hexdigest() != manifest['full_bin_sha256']
            or not app or app[0] != 0xe9 or len(app) + 0x10000 > 0x350000
            or full[0x10000:0x10000 + len(app)] != app):
        raise ValueError('Firmware release image identity mismatch')
    destination = application / 'Contents/Resources/Passport'
    archive = destination / 'build/firmware' / manifest['full_bin_sha256']
    archive.mkdir(parents=True)
    shutil.copy2(source / 'manifest.json', archive / 'manifest.json')
    shutil.copy2(source / 'FoloToy-AI-Passport.bin', archive / 'FoloToy-AI-Passport.bin')
    shutil.copy2(source / 'FoloToy-AI-Passport-full.bin', destination / 'build/FoloToy-AI-Passport-full.bin')
    (destination / 'tools').mkdir()
    shutil.copy2(ROOT / 'tools/flash-app.sh', destination / 'tools/flash-app.sh')


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    bundle = commands.add_parser('firmware')
    bundle.add_argument('destination', type=Path)
    mac = commands.add_parser('mac-resources')
    mac.add_argument('application', type=Path)
    mac.add_argument('firmware', type=Path)
    args = parser.parse_args()
    if args.command == 'firmware':
        firmware(args.destination.resolve())
    else:
        mac_resources(args.application.resolve(), args.firmware.resolve())


if __name__ == '__main__':
    main()
