#!/usr/bin/env python3
import ipaddress
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlsplit


ROOT = Path(__file__).resolve().parents[1]
PATTERNS = {
    'private key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----'),
    'access token': re.compile(r'(?:gh[pousr]_[A-Za-z0-9_]{30,}|github_pat_[A-Za-z0-9_]{30,}|sk-(?:proj-)?[A-Za-z0-9_-]{24,}|AKIA[0-9A-Z]{16})'),
    'local home path': re.compile(r'(?:/Users|/home)/[A-Za-z0-9_.-]+/'),
    'Apple team identifier': re.compile(r'DEVELOPMENT_TEAM\s*[:=]\s*["\']?[A-Z0-9]{10}\b'),
    'deployment hostname': re.compile(r'\b[A-Za-z0-9-]+\.(?:online|local|trycloudflare\.com)\b', re.I),
}
SERVICE_HOSTS = {'github.com', 'docs.openclaw.ai', 'registry.npmjs.org', 'www.apple.com', 'www.w3.org'}


def findings(name, data):
    errors = []
    for label, pattern in PATTERNS.items():
        if pattern.search(data):
            errors.append(f'{name}: {label}; remove or replace with a placeholder')
    if (name.startswith(('app/', 'main/', 'mac/', 'ios/', 'shared/', 'tools/openclaw/', 'web/src/', 'web/public/', 'web/dist/')) or name == 'web/index.html') and not name.endswith('.md'):
        for value in re.findall(r'(?:https?|wss?)://[A-Za-z0-9._-]+(?::[0-9]+)?', data):
            host = urlsplit(value).hostname
            if not host or host in SERVICE_HOSTS or host in {'localhost', 'ai-passport', '192.168.4.1'} or host.endswith(('.example', '.invalid')):
                continue
            try:
                address = ipaddress.ip_address(host)
                if address.is_loopback:
                    continue
            except ValueError:
                pass
            errors.append(f'{name}: non-example runtime endpoint; review before publication')
        for value in re.findall(r'(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])', data):
            try:
                address = ipaddress.ip_address(value)
            except ValueError:
                continue
            if not address.is_loopback and not address.is_unspecified and value not in {'192.168.4.1', '192.0.2.1', '255.255.255.255'}:
                errors.append(f'{name}: non-placeholder runtime IP address; review before publication')
    return list(dict.fromkeys(errors))


def main():
    names = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT).decode().split('\0')
    errors = []
    count = 0
    for name in names:
        if not name:
            continue
        path = ROOT / name
        if path.is_symlink():
            errors.append(f'{name}: source symlink must not be published')
            continue
        if not path.is_file():
            continue
        if path.suffix in {'.p12', '.p8', '.key', '.pem', '.mobileprovision', '.log', '.pid'} or path.name in {'.env', '.dev.vars', '.DS_Store'}:
            errors.append(f'{name}: private/generated file must not be published')
        try:
            text = path.read_text()
        except UnicodeError:
            continue
        count += 1
        errors.extend(findings(name, text))
    for error in errors:
        print(error, file=sys.stderr)
    print(f'Public-source audit: {"FAIL" if errors else "PASS"} ({count} text files; no secret values printed)')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
