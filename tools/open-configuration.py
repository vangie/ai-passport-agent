#!/usr/bin/env python3
"""Import an OpenClaw setup code into the app without printing credentials."""

import argparse
import json
import shlex
import subprocess
import sys
from urllib.parse import urlencode, urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ssh-host', help='Generate a limited pairing code on this SSH host')
    parser.add_argument('--gateway', help='Existing ws:// or wss:// gateway URL')
    parser.add_argument('--ssid', help='Optional network selection; keeps the app password cache')
    parser.add_argument('--name', help='Optional OpenClaw configuration name')
    args = parser.parse_args()
    try:
        if args.ssh_host:
            if args.ssh_host.startswith('-') or not args.gateway:
                parser.error('--ssh-host requires --gateway and a valid SSH host')
            if urlsplit(args.gateway).scheme not in ('ws', 'wss') or not urlsplit(args.gateway).hostname:
                parser.error('--gateway must be an existing ws:// or wss:// address')
            command = ('export PATH=/opt/homebrew/bin:/usr/local/bin:$PATH; '
                       'openclaw qr --json --no-ascii --limited --url ' + shlex.quote(args.gateway))
            result = subprocess.run(['ssh', '-T', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=12',
                                     args.ssh_host, command], capture_output=True, text=True, timeout=60)
            if result.returncode:
                print('The remote pairing command failed; no configuration was imported.', file=sys.stderr)
                return 1
            raw = result.stdout.strip()
        else:
            raw = sys.stdin.read().strip()
        # CLI plugins can write a preamble before the JSON result.
        obj = None
        for index, char in enumerate(raw):
            if char != '{':
                continue
            try:
                candidate, _ = json.JSONDecoder().raw_decode(raw[index:])
            except ValueError:
                continue
            if isinstance(candidate, dict) and (candidate.get('setupCode') or candidate.get('code')):
                obj = candidate
                break
        code = (obj.get('setupCode') or obj.get('code')) if obj else raw
        address = (obj.get('gatewayUrl') or obj.get('url')) if obj else args.gateway
        if not code or not isinstance(code, str):
            raise ValueError('missing setup code')
        fields = {'code': code}
        if address:
            fields['url'] = address
        if args.ssid:
            fields['ssid'] = args.ssid
        if args.name:
            fields['name'] = args.name
        deep_link = 'ai-passport-agent://pair?' + urlencode(fields)
        subprocess.run(['/usr/bin/open', deep_link], check=True, capture_output=True)
        print('Pairing configuration sent to AI Passport Agent.')
        return 0
    except (ValueError, subprocess.SubprocessError, OSError):
        print('Could not obtain or open the pairing configuration; no credentials were printed.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
