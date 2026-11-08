#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


PLUGIN_ID = 'ai-passport-openclaw'


def valid_name(name):
    return isinstance(name, str) and name not in ('', '.', '..') and Path(name).name == name


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verified_receipt(dist):
    receipt_path = dist / 'passport-relay-patch.json'
    if not receipt_path.exists():
        return None
    receipt = json.loads(receipt_path.read_text())
    if not valid_name(receipt['handler']) or not re.fullmatch('[0-9a-f]{64}', receipt['original_sha256']):
        raise ValueError('Invalid legacy handler receipt')
    for name, key in [(receipt['handler'], 'patched_sha256'), ('passport-relay.mjs', 'module_sha256'),
                      ('passport-ima.mjs', 'codec_sha256')]:
        if digest(dist / name) != receipt[key]:
            raise ValueError('Legacy patch changed; refusing migration')
    original = dist / '.passport-relay-backups' / receipt['original_sha256'] / receipt['handler']
    if digest(original) != receipt['original_sha256']:
        raise ValueError('Legacy original checksum mismatch')
    return receipt


def inspect(args):
    receipt = verified_receipt(args.dist)
    report = {'status': 'ready', 'legacyPatch': bool(receipt), 'changesCoreAfterMigration': False}
    if receipt:
        report['originalHandlerSha'] = receipt['original_sha256']
    return report


def migrate(args):
    plugin = Path(__file__).resolve().parent
    subprocess.run(['node', str(plugin / 'doctor.mjs')], check=True)
    result = subprocess.run(['openclaw', 'gateway', 'call', 'passport.status', '--json', '--params', '{}'],
                            check=True, capture_output=True, text=True)
    raw = result.stdout
    info = None
    for index, char in enumerate(raw):
        if char == '{':
            try:
                candidate, _ = json.JSONDecoder().raw_decode(raw[index:])
                if candidate.get('protocol') == 1:
                    info = candidate
                    break
            except (ValueError, AttributeError):
                pass
    if not info or not info.get('providerRegistered') or not info.get('upstreamAvailable') or info.get('path') != '/passport/v1':
        raise ValueError('Install and load the Passport plugin before migration')
    if info.get('connections') != 0:
        raise ValueError('End active Passport connections before migration')
    receipt = verified_receipt(args.dist)
    if not receipt:
        return {'status': 'already-native', 'restartRequired': False}
    backup = Path(tempfile.mkdtemp(prefix='passport-plugin-rollback-', dir=args.backup_dir))
    os.chmod(backup, 0o700)
    names = [receipt['handler'], 'passport-relay.mjs', 'passport-ima.mjs', 'passport-relay-patch.json']
    for name in names:
        shutil.copy2(args.dist / name, backup / name)
    manifest = {'files': {name: digest(backup / name) for name in names},
                'originalHandlerSha': receipt['original_sha256'], 'handler': receipt['handler']}
    (backup / 'rollback.json').write_text(json.dumps(manifest, indent=2) + '\n')
    subprocess.run(['python3', str(plugin / 'install-passport-relay.py'), str(args.dist), '--restore'], check=True)
    if digest(args.dist / receipt['handler']) != receipt['original_sha256']:
        raise ValueError('Restored handler checksum mismatch')
    return {'status': 'migrated-core-restored', 'rollback': str(backup), 'restartRequired': True,
            'firmwareRequired': 'Passport protocol 1 /passport/v1 transport'}


def rollback(args):
    backup = args.backup.resolve()
    manifest = json.loads((backup / 'rollback.json').read_text())
    handler = manifest['handler']
    if not valid_name(handler) or digest(args.dist / handler) != manifest['originalHandlerSha']:
        raise ValueError('OpenClaw changed or upgraded; refusing to overwrite its handler')
    if set(manifest['files']) != {handler, 'passport-relay.mjs', 'passport-ima.mjs', 'passport-relay-patch.json'}:
        raise ValueError('Unexpected rollback file set')
    for name, expected in manifest['files'].items():
        if not valid_name(name) or digest(backup / name) != expected:
            raise ValueError('Rollback checksum mismatch')
        if name != handler and (args.dist / name).exists():
            raise ValueError('Rollback destination already exists')
    for name in manifest['files']:
        temporary = args.dist / (name + '.passport-rollback')
        shutil.copy2(backup / name, temporary)
        os.replace(temporary, args.dist / name)
    return {'status': 'legacy-patch-restored', 'restartRequired': True,
            'firmwareRequired': 'previous firmware using the native root path'}


def main():
    parser = argparse.ArgumentParser(description='Explicit migration from the legacy patch to the native Passport plugin')
    parser.add_argument('action', choices=['inspect', 'migrate', 'rollback'])
    parser.add_argument('--dist', required=True, type=Path)
    parser.add_argument('--backup-dir', type=Path, default=Path.home() / '.openclaw/backups')
    parser.add_argument('--backup', type=Path)
    args = parser.parse_args()
    if args.action == 'rollback' and not args.backup:
        parser.error('rollback requires --backup')
    if args.action == 'migrate':
        args.backup_dir.mkdir(parents=True, exist_ok=True)
    report = {'inspect': inspect, 'migrate': migrate, 'rollback': rollback}[args.action](args)
    print(json.dumps(report))


if __name__ == '__main__':
    main()
