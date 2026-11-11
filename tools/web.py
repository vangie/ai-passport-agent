#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys


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
        prompt = prompt.replace('macOS App / iOS App 只负责配置', '配置网页与 macOS App / iOS App 只负责配置')
        prompt = prompt.replace('The macOS App and optional iOS App only configure it; neither relays audio.', 'The website, macOS App and optional iOS App only configure it; none relays audio.')
        (public / f'setup-{language}.txt').write_text(prompt + '\n')


def stage_firmware(source):
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
        if kind == 'full':
            entries = [struct.unpack_from('<HBBII', data, address) for address in range(0x8000, 0x8c00, 32)]
            if (0x50aa, 0, 0, 0x10000, 0x340000) not in entries or not any(entry[0] == 0x50aa and entry[3:] == (0x350000, 0x10000) for entry in entries):
                raise ValueError('Unexpected Passport partition layout')
        images.append(filename)
        manifest[kind] = {'file': filename, 'offset': offset, 'size': len(data), 'sha256': digest}
    for filename in images:
        shutil.copy2(source / filename, destination / filename)
    (destination / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f"Staged {receipt['tag']}: same-origin firmware, no Release redirect at runtime")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('command', choices=['prepare', 'firmware', 'dev', 'build', 'serve', 'tunnel'])
    parser.add_argument('--from-release', type=Path)
    args = parser.parse_args()
    if args.command == 'tunnel':
        subprocess.run(['cloudflared', 'tunnel', '--url', 'http://127.0.0.1:4173', '--no-autoupdate'], check=True)
        return
    prepare()
    if args.from_release:
        stage_firmware(args.from_release.resolve())
    if args.command == 'firmware' and not args.from_release:
        parser.error('firmware requires --from-release')
    if args.command in {'prepare', 'firmware'}:
        return
    if not (WEB / 'node_modules/.package-lock.json').exists():
        subprocess.run(['npm', 'ci', '--ignore-scripts', '--no-audit', '--no-fund'], cwd=WEB, check=True)
    if args.command == 'dev':
        subprocess.run(['npm', 'run', 'dev'], cwd=WEB, check=True)
        return
    subprocess.run(['npm', 'run', 'build'], cwd=WEB, check=True)
    if args.command == 'serve':
        subprocess.run([sys.executable, '-m', 'http.server', '4173', '--bind', '127.0.0.1', '--directory', str(WEB / 'dist')], check=True)


if __name__ == '__main__':
    main()
