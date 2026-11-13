#!/usr/bin/env python3
import argparse
from pathlib import Path
import shutil


ROOT = Path(__file__).resolve().parents[1]
LEGACY_HOSTNAME = 'ai-passport-openclaw.codelife.me'


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'tools/legacy-site/index.html'
    for filename in ('index.html', '404.html'):
        shutil.copyfile(source, output / filename)
    (output / 'CNAME').write_text(f'{LEGACY_HOSTNAME}\n', encoding='utf-8')
    (output / '.nojekyll').write_text('', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description='Stage the static legacy-domain redirect site.')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/legacy-site')
    arguments = parser.parse_args()
    build(arguments.output)
    print(f'Legacy redirect site: {arguments.output}')


if __name__ == '__main__':
    main()
