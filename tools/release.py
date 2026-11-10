#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def ios_source(destination):
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--', 'ios/Passport', 'ios/PassportUITests',
         'ios/project.yml', 'shared', 'LICENSE', 'docs/release/ios-install.md',
         'docs/release/ios-install.zh_CN.md'], cwd=ROOT).decode().split('\0')
    files = {}
    for name in filter(None, names):
        relative = Path(name)
        allowed = (name in {'ios/project.yml', 'LICENSE', 'docs/release/ios-install.md',
                            'docs/release/ios-install.zh_CN.md'}
                   or (relative.is_relative_to('ios/Passport')
                       and relative.suffix in {'.swift', '.plist', '.json', '.png'})
                   or (relative.is_relative_to('ios/PassportUITests') and relative.suffix == '.swift')
                   or (relative.is_relative_to('shared') and relative.suffix == '.swift'))
        if (not allowed or relative.is_absolute() or '..' in relative.parts
                or any(part.startswith('.') or part == 'xcuserdata' for part in relative.parts)):
            continue
        source = ROOT / relative
        if (not source.is_file() or source.is_symlink()
                or not source.resolve().is_relative_to(ROOT.resolve())
                or any((ROOT / parent).is_symlink() for parent in relative.parents)):
            raise ValueError('Unsafe iPhone source input')
        target = {'docs/release/ios-install.md': 'README.md',
                  'docs/release/ios-install.zh_CN.md': 'README.zh_CN.md'}.get(name, name)
        files[target] = source
    required = {'ios/project.yml', 'ios/Passport/PassportApp.swift',
                'ios/Passport/Info.plist', 'shared/PairPrompt.swift',
                'README.md', 'README.zh_CN.md', 'LICENSE'}
    if not required.issubset(files):
        raise ValueError('Incomplete iPhone source input; commit release inputs first')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, source in sorted(files.items()):
            content = source.read_bytes()
            if name in {'README.md', 'README.zh_CN.md'}:
                content = content.replace(b'(ios-install.md)', b'(README.md)').replace(
                    b'(ios-install.zh_CN.md)', b'(README.zh_CN.md)')
            archive.writestr(f'ai-passport-openclaw-ios-source/{name}', content)


def licenses(destination):
    sources = {'MIT.txt': 'LICENSE', 'Noto-SIL-OFL.txt': 'assets/fonts/OFL.txt',
               'Monocypher.txt': 'components/passport_crypto/LICENSE',
               'ESP-WebSocket-Client.txt': 'components/passport_transport/LICENSE'}
    destination.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        shutil.copy2(ROOT / source, destination / name)


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
    licenses(destination / 'licenses')


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
    licenses(destination / 'licenses')


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest='command', required=True)
    bundle = commands.add_parser('firmware')
    bundle.add_argument('destination', type=Path)
    mac = commands.add_parser('mac-resources')
    mac.add_argument('application', type=Path)
    mac.add_argument('firmware', type=Path)
    ios = commands.add_parser('ios-source')
    ios.add_argument('destination', type=Path)
    args = parser.parse_args()
    if args.command == 'firmware':
        firmware(args.destination.resolve())
    elif args.command == 'mac-resources':
        mac_resources(args.application.resolve(), args.firmware.resolve())
    else:
        ios_source(args.destination.resolve())


if __name__ == '__main__':
    main()
