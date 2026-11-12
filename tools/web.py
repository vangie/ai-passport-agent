#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile

import web_preview


ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'web'


def prepare():
    public = WEB / 'public'
    public.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'assets/images/passport-lobster.svg', public / 'lobster.svg')
    source = (ROOT / 'shared/PairPrompt.swift').read_text()
    for language, name in [('zh', 'chineseText'), ('en', 'englishText')]:
        match = re.search(rf'static let {name} = """\n(.*?)\n    """', source, re.S)
        if not match:
            raise ValueError('Setup prompt source is missing')
        prompt = '\n'.join(line[4:] if line.startswith('    ') else line for line in match[1].splitlines())
        (public / f'setup-{language}.txt').write_text(prompt + '\n')


def release_identity(source):
    image = (source / 'FoloToy-AI-Passport.bin').read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    receipt_path = source / 'release-manifest.json'
    if receipt_path.is_file():
        receipt = json.loads(receipt_path.read_text())
        tag = receipt.get('tag', '')
        if (receipt.get('schema_version') != 1
                or not re.fullmatch(r'v\d+\.\d+\.\d+', tag)
                or not re.fullmatch(r'[a-f0-9]{40}', receipt.get('commit_sha', ''))):
            raise ValueError('Unsupported release identity receipt')
        record = receipt['assets']['FoloToy-AI-Passport.bin']
        if len(image) != record['size'] or digest != record['sha256']:
            raise ValueError('Invalid release identity image')
    else:
        tag = source.name
        checksums = (source / 'SHA256SUMS.txt').read_text()
        matches = re.findall(r'^([a-f0-9]{64})  (?:\./)?FoloToy-AI-Passport\.bin$', checksums, re.M)
        if not re.fullmatch(r'v\d+\.\d+\.\d+', tag) or matches != [digest]:
            raise ValueError('Invalid legacy release identity checksum')
    if (len(image) < 288 or image[0] != 0xe9 or image[12:14] != b'\x05\x00'
            or image[32:36] != b'\x32\x54\xcd\xab'):
        raise ValueError('Invalid release identity image')
    return image[176:184].hex(), {'version': tag, 'channel': 'release'}


def stage_firmware(source, history=None):
    receipt = json.loads((source / 'release-manifest.json').read_text())
    if receipt.get('schema_version') != 1 or not re.fullmatch(r'v\d+\.\d+\.\d+(?:[-.\w]*)?', receipt.get('tag', '')) or not re.fullmatch(r'[a-f0-9]{40}', receipt.get('commit_sha', '')):
        raise ValueError('Unsupported release receipt')
    destination = WEB / 'public/firmware'
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {'schema': 1, 'version': receipt['tag'], 'commit': receipt['commit_sha']}
    images = []
    for kind, filename, offset in [('application', 'FoloToy-AI-Passport.bin', 0x10000), ('full', 'FoloToy-AI-Passport-full.bin', 0)]:
        expected = receipt['assets'][filename]
        data = (source / filename).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if len(data) != expected['size'] or digest != expected['sha256']:
            raise ValueError('Firmware release checksum mismatch')
        application = data if kind == 'application' else data[0x10000:]
        if len(application) < 288 or application[0] != 0xe9 or application[12:14] != b'\x05\x00' or application[32:36] != b'\x32\x54\xcd\xab' or len(application) > 0x340000:
            raise ValueError('Invalid ESP32-C3 application')
        build_id = application[176:184].hex()
        if kind == 'application':
            manifest['buildId'] = build_id
        elif build_id != manifest['buildId']:
            raise ValueError('Release images have different build identifiers')
        if kind == 'full':
            entries = [struct.unpack_from('<HBBII', data, address) for address in range(0x8000, 0x8c00, 32)]
            if (0x50aa, 0, 0, 0x10000, 0x340000) not in entries or not any(entry[0] == 0x50aa and entry[3:] == (0x350000, 0x10000) for entry in entries):
                raise ValueError('Unexpected Passport partition layout')
        images.append(filename)
        manifest[kind] = {'file': filename, 'offset': offset, 'size': len(data), 'sha256': digest}
    catalog = ROOT / 'web/release-builds.json'
    manifest['knownBuilds'] = json.loads(catalog.read_text()) if catalog.is_file() else {}
    for build_id, identity in manifest['knownBuilds'].items():
        if (not re.fullmatch(r'[a-f0-9]{16}', build_id)
                or set(identity) != {'version', 'channel'}
                or not re.fullmatch(r'v\d+\.\d+\.\d+', identity['version'])
                or identity['channel'] != 'release'):
            raise ValueError('Invalid retained release identity')
    if history:
        for directory in sorted(history.iterdir()):
            if not directory.is_dir():
                continue
            build_id, identity = release_identity(directory)
            previous = manifest['knownBuilds'].get(build_id)
            if previous and previous != identity:
                raise ValueError('Conflicting release build identities')
            manifest['knownBuilds'][build_id] = identity
    for filename in images:
        shutil.copy2(source / filename, destination / filename)
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f"Staged {receipt['tag']}: same-origin firmware, no Release redirect at runtime")


def stage_local_build_catalog():
    manifest_path = WEB / 'public/firmware/manifest.json'
    if not manifest_path.is_file():
        return
    manifest = json.loads(manifest_path.read_text())
    builds = {build: record for build, record in manifest.get('knownBuilds', {}).items()
              if record.get('channel') == 'release'}
    for receipt_path in sorted((ROOT / 'build/firmware').glob('*/manifest.json')):
        try:
            receipt = json.loads(receipt_path.read_text())
            record = receipt['files']['FoloToy-AI-Passport.bin']
            image = (receipt_path.parent / 'FoloToy-AI-Passport.bin').read_bytes()
            if (len(image) < 288 or len(image) != record['size']
                    or hashlib.sha256(image).hexdigest() != record['sha256']
                    or image[0] != 0xe9 or image[12:14] != b'\x05\x00'
                    or image[32:36] != b'\x32\x54\xcd\xab'
                    or image[176:208].hex() != receipt['app_elf_sha256']):
                continue
            raw_version = image[48:80].split(b'\0', 1)[0].decode('ascii')
            version = re.fullmatch(r'(v\d+\.\d+\.\d+)(?:-\d+-g[a-f0-9]+)?(?:-dirty)?', raw_version)
            builds.setdefault(image[176:184].hex(), {
                'version': version[1] if version else '', 'channel': 'development',
            })
        except (OSError, ValueError, KeyError, TypeError):
            continue
    manifest['knownBuilds'] = builds
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['prepare', 'firmware', 'build', 'serve'])
    parser.add_argument('--from-release', type=Path)
    parser.add_argument('--release-history', type=Path)
    args = parser.parse_args()
    if args.command == 'firmware' and not args.from_release:
        parser.error('firmware requires --from-release')
    with web_preview.build_lock():
        prepare()
        if args.from_release:
            stage_firmware(args.from_release.resolve(), args.release_history.resolve() if args.release_history else None)
        if args.command in {'prepare', 'firmware'}:
            return
        if not (WEB / 'node_modules/.package-lock.json').exists():
            subprocess.run(['npm', 'ci', '--ignore-scripts', '--no-audit', '--no-fund'], cwd=WEB, check=True)
        if args.command == 'serve':
            stage_local_build_catalog()
            with tempfile.TemporaryDirectory(prefix='web-build-', dir=ROOT / 'build') as directory:
                output = Path(directory) / 'site'
                subprocess.run(['npm', 'run', 'build', '--', '--outDir', str(output), '--emptyOutDir'], cwd=WEB, check=True)
                web_preview.publish(output, WEB / 'dist')
        else:
            subprocess.run(['npm', 'run', 'build'], cwd=WEB, check=True)
    if args.command == 'serve':
        web_preview.serve(WEB / 'dist')


def terminate(_signal, _frame):
    raise SystemExit(0)


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, terminate)
    try:
        main()
    except KeyboardInterrupt:
        print('\nPreview stopped.')
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
