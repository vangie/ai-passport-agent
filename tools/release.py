#!/usr/bin/env python3
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
HERMES_FILES = {'__init__.py', 'adapter.py', 'passport_auth.py', 'passport_ima.py',
                'passport_protocol.py', 'passport_speech.py', 'passport_talk.py',
                'plugin.yaml', 'README.md', 'README.zh_CN.md'}


def check_version(tag=None):
    version = json.loads((ROOT / 'tools/openclaw/package.json').read_text())['version']
    if not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version):
        raise ValueError('Release version must be a stable semantic version')
    if tag is not None and tag != f'v{version}':
        raise ValueError('Release tag does not match the product version')
    lock = json.loads((ROOT / 'tools/openclaw/package-lock.json').read_text())
    if lock['version'] != version or lock['packages']['']['version'] != version:
        raise ValueError('Plugin lockfile version mismatch')
    web = json.loads((ROOT / 'web/package.json').read_text())
    web_lock = json.loads((ROOT / 'web/package-lock.json').read_text())
    if any(value != version for value in [web['version'], web_lock['version'],
                                          web_lock['packages']['']['version']]):
        raise ValueError('Web version mismatch')
    hermes_versions = re.findall(r'^version: (\S+)$',
                                (ROOT / 'tools/hermes/plugin.yaml').read_text(), re.MULTILINE)
    if hermes_versions != [version]:
        raise ValueError('Hermes plugin version mismatch')
    for name in ['index.mjs', 'doctor.mjs']:
        versions = re.findall(r"pluginVersion: '([^']+)'", (ROOT / 'tools/openclaw' / name).read_text())
        if versions != [version]:
            raise ValueError(f'Plugin runtime version mismatch: {name}')
    for name in ['notes.md', 'notes.zh_CN.md']:
        if f'# AI Passport Agent v{version}' not in (ROOT / 'docs/release' / name).read_text().splitlines():
            raise ValueError(f'Release notes version mismatch: {name}')
    for name in ['CHANGELOG.md', 'CHANGELOG.zh_CN.md']:
        if not re.search(rf'^## v{re.escape(version)} - \d{{4}}-\d{{2}}-\d{{2}}$',
                         (ROOT / 'docs' / name).read_text(), re.MULTILINE):
            raise ValueError(f'Release changelog entry missing: {name}')
    return version


def asset_names(version):
    return {f'ai-passport-openclaw-{version}.tgz', f'ai-passport-hermes-{version}.tar.gz',
            'ai-passport-agent-firmware.tar.gz',
            'FoloToy-AI-Passport-full.bin',
            'FoloToy-AI-Passport.bin', 'LICENSE.txt'}


def prepare_assets(source, destination, tag, commit):
    version = check_version(tag)
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Release source commit must be a full Git SHA')
    inputs = {
        f'ai-passport-openclaw-{version}.tgz': source / 'release-plugin' / f'ai-passport-openclaw-{version}.tgz',
        f'ai-passport-hermes-{version}.tar.gz': source / 'release-hermes' / f'ai-passport-hermes-{version}.tar.gz',
        'ai-passport-agent-firmware.tar.gz': source / 'release-firmware/ai-passport-agent-firmware.tar.gz',
        'FoloToy-AI-Passport-full.bin': source / 'release-firmware/firmware/FoloToy-AI-Passport-full.bin',
        'FoloToy-AI-Passport.bin': source / 'release-firmware/firmware/FoloToy-AI-Passport.bin',
        'LICENSE.txt': ROOT / 'LICENSE',
    }
    for name, path in inputs.items():
        base = ROOT if name == 'LICENSE.txt' else source
        if (not path.is_file() or path.stat().st_size == 0 or path.is_symlink()
                or not path.resolve().is_relative_to(base.resolve())
                or base.is_symlink()
                or any((base / parent).is_symlink() for parent in path.relative_to(base).parents)):
            raise ValueError(f'Missing, empty or unsafe release input: {name}')
    if destination.exists() and (destination.is_symlink() or any(destination.iterdir())):
        raise ValueError('Release destination must be an empty directory')
    destination.mkdir(parents=True, exist_ok=True)
    records = {}
    for name, path in sorted(inputs.items()):
        shutil.copy2(path, destination / name)
        records[name] = {'size': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    manifest = {'schema_version': 1, 'tag': tag, 'version': version, 'commit_sha': commit, 'assets': records}
    (destination / 'release-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    checksums = [f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n'
                 for path in sorted(destination.iterdir())]
    (destination / 'SHA256SUMS.txt').write_text(''.join(checksums))
    verify_assets(destination, tag, commit)


def verify_assets(directory, tag, commit):
    version = check_version(tag)
    expected = asset_names(version) | {'release-manifest.json', 'SHA256SUMS.txt'}
    if {path.name for path in directory.iterdir()} != expected:
        raise ValueError('Release assets are missing or unexpected files are present')
    for path in directory.iterdir():
        if not path.is_file() or path.is_symlink() or path.stat().st_size == 0:
            raise ValueError('Release contains an empty or unsafe asset')
    manifest = json.loads((directory / 'release-manifest.json').read_text())
    if (manifest.get('schema_version') != 1 or manifest.get('tag') != tag
            or manifest.get('version') != version or manifest.get('commit_sha') != commit
            or not re.fullmatch(r'[0-9a-f]{40}', commit)
            or set(manifest.get('assets', {})) != asset_names(version)):
        raise ValueError('Release provenance mismatch')
    checksums = {}
    for line in (directory / 'SHA256SUMS.txt').read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9_.-]+)', line)
        if not match or match[2] in checksums:
            raise ValueError('Malformed or duplicate release checksum')
        checksums[match[2]] = match[1]
    if set(checksums) != expected - {'SHA256SUMS.txt'}:
        raise ValueError('Incomplete release checksums')
    for name, digest in checksums.items():
        path = directory / name
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Release checksum mismatch: {name}')
        if name in manifest['assets'] and manifest['assets'][name] != {'size': path.stat().st_size, 'sha256': digest}:
            raise ValueError(f'Release manifest mismatch: {name}')


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
                       and relative.suffix in {'.swift', '.plist', '.json', '.png', '.strings'})
                   or (relative.is_relative_to('ios/PassportUITests') and relative.suffix == '.swift')
                   or (relative.is_relative_to('shared') and relative.suffix in {'.swift', '.strings'}))
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
                'ios/Passport/Info.plist', 'shared/PairPrompt.swift', 'shared/AppLocalization.swift',
                'shared/en.lproj/Localizable.strings', 'shared/zh-Hans.lproj/Localizable.strings',
                'ios/Passport/en.lproj/InfoPlist.strings', 'ios/Passport/zh-Hans.lproj/InfoPlist.strings',
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
            archive.writestr(f'ai-passport-agent-ios-source/{name}', content)


def licenses(destination):
    sources = {'MIT.txt': 'LICENSE', 'Noto-SIL-OFL.txt': 'assets/fonts/OFL.txt',
               'Monocypher.txt': 'components/passport_crypto/LICENSE',
               'ESP-WebSocket-Client.txt': 'components/passport_transport/LICENSE',
               'WebRTC-VAD.txt': 'components/passport_vad/LICENSE',
               'WebRTC-VAD-PATENTS.txt': 'components/passport_vad/PATENTS',
               'WebRTC-VAD-AUTHORS.txt': 'components/passport_vad/AUTHORS',
               'Hermes-icon-MIT.txt': 'assets/images/HERMES-LICENSE.txt'}
    destination.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        shutil.copy2(ROOT / source, destination / name)


def hermes_package(destination):
    version = check_version()
    directory = ROOT / 'tools/hermes'
    sources = {name: directory / name for name in HERMES_FILES}
    sources['LICENSE'] = ROOT / 'LICENSE'
    for name, source in sources.items():
        if (not source.is_file() or source.is_symlink() or source.stat().st_size == 0
                or not source.resolve().is_relative_to(ROOT.resolve())
                or any((ROOT / parent).is_symlink() for parent in source.relative_to(ROOT).parents)):
            raise ValueError(f'Missing, empty or unsafe Hermes package input: {name}')
    contents = io.BytesIO()
    with gzip.GzipFile(fileobj=contents, mode='wb', filename='', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w') as archive:
            for name, source in sorted(sources.items()):
                data = source.read_bytes()
                record = tarfile.TarInfo(f'ai-passport-hermes/{name}')
                record.size = len(data)
                record.mode = 0o644
                archive.addfile(record, io.BytesIO(data))
    destination.mkdir(parents=True, exist_ok=True)
    target = destination / f'ai-passport-hermes-{version}.tar.gz'
    with target.open('xb') as output:
        output.write(contents.getvalue())
    print(target)


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
    hermes = commands.add_parser('hermes-package')
    hermes.add_argument('destination', type=Path)
    mac = commands.add_parser('mac-resources')
    mac.add_argument('application', type=Path)
    mac.add_argument('firmware', type=Path)
    ios = commands.add_parser('ios-source')
    ios.add_argument('destination', type=Path)
    version = commands.add_parser('check-version')
    version.add_argument('--tag')
    assets = commands.add_parser('prepare-assets')
    assets.add_argument('source', type=Path)
    assets.add_argument('destination', type=Path)
    verify = commands.add_parser('verify-assets')
    verify.add_argument('directory', type=Path)
    for command in [assets, verify]:
        command.add_argument('--tag', required=True)
        command.add_argument('--commit', required=True)
    args = parser.parse_args()
    if args.command == 'firmware':
        firmware(args.destination.resolve())
    elif args.command == 'hermes-package':
        hermes_package(args.destination.absolute())
    elif args.command == 'mac-resources':
        mac_resources(args.application.resolve(), args.firmware.resolve())
    elif args.command == 'ios-source':
        ios_source(args.destination.resolve())
    elif args.command == 'check-version':
        print(check_version(args.tag))
    elif args.command == 'prepare-assets':
        prepare_assets(args.source.absolute(), args.destination.absolute(), args.tag, args.commit)
    else:
        verify_assets(args.directory.absolute(), args.tag, args.commit)


if __name__ == '__main__':
    main()
