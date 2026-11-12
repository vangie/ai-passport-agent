#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

from check_public import PATTERNS, findings


def audit_site(directory, require_firmware=False):
    errors = []
    allowed = {'index.html', 'lobster.svg', 'setup-zh.txt', 'setup-en.txt',
               'firmware/manifest.json', 'firmware/FoloToy-AI-Passport.bin',
               'firmware/FoloToy-AI-Passport-full.bin'}
    for path in sorted(directory.rglob('*')):
        name = path.relative_to(directory).as_posix()
        if path.is_symlink():
            errors.append(f'{name}: symlink must not be published')
            continue
        if not path.is_file():
            continue
        if name not in allowed and not (path.parent == directory / 'assets'
                                        and path.suffix in {'.js', '.css', '.png'}):
            errors.append(f'{name}: unexpected public artifact')
        data = path.read_bytes()
        if path.suffix == '.bin':
            text = data.decode('latin-1')
            for label, pattern in PATTERNS.items():
                if label != 'private key' and pattern.search(text):
                    errors.append(f'{name}: {label}')
            if re.search(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\s+[A-Za-z0-9+/=\s]{32,}-----END (?:RSA |EC |OPENSSH )?PRIVATE KEY-----', text):
                errors.append(f'{name}: embedded private key')
            for endpoint in re.findall(r'(?:https?|wss?)://[A-Za-z0-9._-]+(?::[0-9]+)?', text):
                errors.extend(findings('web/dist/' + name, endpoint))
        elif path.suffix != '.png':
            try:
                errors.extend(findings('web/dist/' + name, data.decode('utf-8')))
            except UnicodeError:
                errors.append(f'{name}: invalid text encoding')
    if not (directory / 'index.html').is_file():
        errors.append('index.html: missing website')
    manifest_path = directory / 'firmware/manifest.json'
    if require_firmware or manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text())
            if any(record.get('channel') != 'release' for record in manifest.get('knownBuilds', {}).values()):
                errors.append('firmware/manifest.json: local development catalog')
            for kind, filename in [('application', 'FoloToy-AI-Passport.bin'), ('full', 'FoloToy-AI-Passport-full.bin')]:
                entry = manifest[kind]
                data = (directory / 'firmware' / filename).read_bytes()
                if entry['file'] != filename or len(data) != entry['size'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
                    errors.append(f'firmware/{filename}: release checksum mismatch')
                if kind == 'full':
                    if len(data) < 0x10000:
                        raise ValueError('Incomplete partition table')
                    for address in range(0x8000, 0x8c00, 32):
                        magic, partition_type, _subtype, offset, size = struct.unpack_from('<HBBII', data, address)
                        if magic == 0x50aa and partition_type == 1:
                            stored = data[offset:offset + size]
                            if any(value != 255 for value in stored):
                                errors.append(f'firmware/{filename}: stored device data must not be published')
        except (OSError, ValueError, KeyError, TypeError, struct.error):
            errors.append('firmware: missing or invalid release artifacts')
    return list(dict.fromkeys(errors))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    parser.add_argument('--require-firmware', action='store_true')
    args = parser.parse_args()
    errors = audit_site(args.directory, args.require_firmware)
    for error in errors:
        print(error, file=sys.stderr)
    print(f'Public website audit: {"FAIL" if errors else "PASS"} (no secret values printed)')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
