<p align="right"><a href="CI-build-and-release.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# CI and Release

This repository releases one product, AI Passport OpenClaw. Tags use `v<semver>`
(for example `v0.4.0`); the release title includes the product name. The current
Mac, iPhone and plugin marketing versions are aligned. Inspect package manifests
and app bundle versions instead of assuming a firmware protocol version changed.

## Pull requests and main

- `static-checks.yml`: repository rules, private endpoint audit, pinned Gitleaks
  scan, actionlint, C/Python/Swift host tests and an unsigned Mac build on macOS.
- `ios.yml`: unsigned physical-device and Simulator builds, Simulator UI tests
  for layout/provisioning with synthetic data, an allowlisted source kit and a
  Simulator ZIP. Selects an installed iPhone runtime rather than hardcoding a
  model name. Uses the explicit `macos-15-intel` runner to avoid the shared
  ARM runner capacity queue. Runs for relevant PR/main changes and is reused
  by Release.
- `firmware-checks.yml`: ESP-IDF 5.5.3 build and verified merged firmware.
- `openclaw-plugin.yml`: Node 22/24/26 locked dependency install, plugin tests and
  actual npm tarball inspection.

No job reads a personal machine or needs gateway, model, Wi-Fi, tunnel or Apple
credentials. Passing CI is not on-device acceptance or a live-provider test.

## Tag release

Before tagging, review both changelogs and release notes, source/privacy checks,
and the intended hardware compatibility. Never upload private NVS, configuration,
pairing material, local logs or older locally built firmware.

`.github/workflows/release.yml` builds from the pushed tag:

1. Repository, private endpoint, secret and workflow audit.
2. Full host checks via the reusable static workflow.
3. Fresh ESP-IDF build and layout/hash verification. Package firmware from its
   verified content-addressed archive; exclude ELF/MAP from public artifacts.
4. Locked plugin package build.
5. Universal Intel/Apple Silicon Mac build. Bundle that release's verified
   firmware and flash helper, then ad-hoc sign and verify the complete bundle.
6. Reuse the iPhone workflow: build for physical devices and Simulator without
   signing credentials, run Debug UI tests with synthetic state, and package a
   Release Simulator app plus the iPhone source kit. No unsigned IPA is published.
7. Only after all dependencies, including iPhone checks, pass, create the GitHub
   Release and upload firmware, plugin, Mac ZIP, iPhone ZIPs and `SHA256SUMS.txt`.

`tools/release.py ios-source` packages only tracked iPhone sources/assets,
UI tests, `shared/*.swift`, the XcodeGen specification, MIT license and bilingual
installation instructions. It excludes generated Xcode projects, signing files,
local settings, build products and other repository files. Review staged sources
before committing; the allowlist is not a substitute for secret scanning.

All Actions are pinned to full commit SHAs. Default permissions are read-only;
only the final publishing job receives `contents: write`. Manual dispatch runs
the same builds but deliberately does not publish a Release. There are no remote
deployment steps and no automatic changes to someone's OpenClaw installation.

## Signing and installation limits

The default Mac artifact is **ad-hoc signed, not Apple notarized**. This pipeline
does not contain or require a developer certificate. A future notarized channel
must use GitHub encrypted secrets and an isolated temporary keychain; never put
certificates or signing passwords in the source tree. A paid account alone does
not notarize existing artifacts.

The iPhone source kit requires local Xcode signing with the recipient's own
team, including a free Personal Team where supported. There is no App Store,
TestFlight or downloadable signed physical-device app in this workflow. The
unsigned `iphoneos` build is only a compilation gate and is not uploaded.
The Simulator ZIP is for the runner's architecture/SDK, cannot access real BLE,
and may require building from source on other hosts. See
[iPhone installation](../../release/ios-install.md).

Signed iOS distribution would need a separate explicit distribution decision,
Apple team, bundle registration, certificates/profiles and protected CI secrets.
Do not automatically import a developer's local signing identity into GitHub.

Flashing requires local Python 3 and esptool, for example
`pipx install esptool==4.11.0`. The bundled helper checks image SHA, ESP32-C3
identity and bounds before application-only flashing at `0x10000`. It preserves
data only with a compatible partition layout. A merged image belongs at `0x0`
and can reset settings; it is not the normal application-only update path.

See [Release notes](../../release/notes.md), [firmware layout](../engineering/firmware-layout.md)
and the [project README](../../../README.md).
