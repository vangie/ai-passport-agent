#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
version=8.30.1
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)
        platform=darwin_arm64
        checksum=b40ab0ae55c505963e365f271a8d3846efbc170aa17f2607f13df610a9aeb6a5 ;;
    Darwin-x86_64)
        platform=darwin_x64
        checksum=dfe101a4db2255fc85120ac7f3d25e4342c3c20cf749f2c20a18081af1952709 ;;
    Linux-x86_64)
        platform=linux_x64
        checksum=551f6fc83ea457d62a0d98237cbad105af8d557003051f41f3e7ca7b3f2470eb ;;
    *) echo 'Unsupported secret-scan platform' >&2; exit 1 ;;
esac
directory="$(mktemp -d)"
trap 'rm -rf "$directory"' EXIT
curl --fail --silent --show-error --location --retry 3 \
    "https://github.com/gitleaks/gitleaks/releases/download/v${version}/gitleaks_${version}_${platform}.tar.gz" \
    -o "$directory/gitleaks.tar.gz"
printf '%s  %s\n' "$checksum" "$directory/gitleaks.tar.gz" | shasum -a 256 -c -
tar -xzf "$directory/gitleaks.tar.gz" -C "$directory" gitleaks
mkdir "$directory/source"
git ls-files --cached --others --exclude-standard -z |
    while IFS= read -r -d '' path; do
        if [[ -e "$path" || -L "$path" ]]; then printf '%s\0' "$path"; fi
    done | tar --null -T - -cf - | tar -xf - -C "$directory/source"
if ! "$directory/gitleaks" dir "$directory/source" --redact --no-banner \
    --report-format json --report-path "$directory/report.json"; then
    python3 - "$directory/report.json" "$directory/source/" <<'PY'
import json
import sys
for finding in json.load(open(sys.argv[1])):
    print(f"{finding['File'].removeprefix(sys.argv[2])}:{finding['StartLine']}: {finding['RuleID']}")
PY
    exit 1
fi
