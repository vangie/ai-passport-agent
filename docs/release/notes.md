[Simplified Chinese](notes.zh_CN.md) · **English**

# AI Passport OpenClaw v0.4.2

## Highlights

- macOS App / iOS App automatically follow English or Simplified Chinese system
  preferences, including status messages, errors, permissions and setup prompts.
  The device's screen language remains independent. English Wi-Fi actions use
  Scan; the language option reads Simplified Chinese without truncation.
- The copied setup prompt checks plugin compatibility, a dedicated assistant,
  voice readiness and a secure persistent gateway entrance before limited pairing.
- Refined bilingual setup/networking guides and the distinction between optional
  iOS App hotspot configuration and direct device-to-gateway voice traffic.
- Release tags, app/plugin versions, bilingual notes and changelogs must agree.
  All ten assets are checked before publication, with source-commit provenance,
  SHA-256 verification and a verified draft upload before making a release public.
- Progressive-scan UI fixtures no longer race an elapsed-time deadline on slow
  hosted simulators. The v0.4.1 candidate was blocked by this CI check and was
  not published; its tag is retained rather than rewritten.

## Downloads

- `ai-passport-openclaw-macos-universal-adhoc.zip`: macOS 14+ configuration app
  for Apple Silicon and Intel, with matching verified firmware bundled. It uses
  an ad-hoc signature, **not** Developer ID signing or Apple notarization. macOS
  may require explicit approval in Privacy & Security; do not disable Gatekeeper.
  Configuration works without ESP-IDF. USB flashing requires Python 3 and
  esptool (`pipx install esptool==4.11.0`).
- `ai-passport-openclaw-firmware.tar.gz`: bootloader, partition table, merged
  image, application-only image and checksum manifest for ESP32-C3 / 8 MB.
- `FoloToy-AI-Passport-full.bin`: complete image at `0x0` for initial installation
  or intentional full refresh. It can reset settings; do not use it for a
  configuration-preserving update.
- `FoloToy-AI-Passport.bin`: application-only update at `0x10000`, only with the
  matching partition layout. The macOS App uses this route and preserves NVS,
  Card ID and audio partitions.
- `ai-passport-openclaw-*.tgz`: independent OpenClaw plugin source package.
- `ai-passport-openclaw-ios-source.zip`: iOS App source kit with `ios/`, shared
  Swift code and bilingual Xcode signing/install instructions. **Requires your
  own local signing team**, including Personal Team where supported; iOS 17+.
- `ai-passport-openclaw-ios-simulator.zip`: Simulator-only app for a compatible
  Mac architecture/runtime. **Cannot install on an iPhone or use real BLE.**
- `SHA256SUMS.txt`: release-asset checksums.
- `release-manifest.json`: exact source commit, version, asset sizes and hashes.

There is **no signed iOS IPA or TestFlight release**. Download the source kit
for a physical iPhone; the Simulator ZIP is only a preview. The macOS App, iOS App
and plugin versions are 0.4.2; the existing wire protocol and OpenClaw baseline are unchanged.

## Upgrade notes

App localization does not require re-pairing, changing the device language or
flashing firmware. The macOS App is prebuilt and does not need rebuilding or
another ad-hoc signature; local signing does not replace Apple notarization.
An iOS Simulator binary cannot be re-signed into a physical-device app. A prebuilt
device app could be re-signed only with valid signing/provisioning and compatible
entitlements; this release does not distribute such a device binary.

Back up settings you need to preserve. Prefer the macOS App's compatible
application-only update; do not write the merged image for a routine update.
Unreleased prototype device/app identities are not migrated and may require
new Wi-Fi configuration and pairing. Never use a shared administrator gateway
token in place of device pairing.

Bluetooth is enabled in standby and temporarily released during audio/gateway
operations. A first key press after display standby wakes the screen only.
Firmware, gateway/plugin and configuration apps must be compatible; a successful
connection check is not proof of full speech quality.

## Validation boundary

CI builds are not hardware/audio acceptance. The OpenClaw plugin baseline is
2026.10.5; check compatibility before upgrading. Read the repository's setup,
networking and plugin instructions. No endpoints, account keys, Wi-Fi passwords
or device authorizations are distributed with this release.
