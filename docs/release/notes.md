[Simplified Chinese](notes.zh_CN.md) · **English**

# AI Passport OpenClaw v0.4.0

## Highlights

- iPhone configuration over Bluetooth: progressive 2.4 GHz discovery, saved
  networks, personal hotspot hints and limited-code OpenClaw pairing.
- Improved voice controls, Ask transcript/chat display, connected standby and
  the Mac's unified configuration/maintenance workflow.
- Product-style bilingual README with sanitized firmware, Mac and iPhone
  screenshots, plus the tested NFC-to-hotspot Shortcuts tip.
- iPhone physical-device/Simulator compile gates and Simulator UI tests now
  block publication on failure. No Apple signing secrets are required by CI.

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
  matching partition layout. The Mac app uses this route and preserves NVS,
  Card ID and audio partitions.
- `ai-passport-openclaw-*.tgz`: independent OpenClaw plugin source package.
- `ai-passport-openclaw-ios-source.zip`: iPhone source kit with `ios/`, shared
  Swift code and bilingual Xcode signing/install instructions. **Requires your
  own local signing team**, including Personal Team where supported; iOS 17+.
- `ai-passport-openclaw-ios-simulator.zip`: Simulator-only app for a compatible
  Mac architecture/runtime. **Cannot install on an iPhone or use real BLE.**
- `SHA256SUMS.txt`: release-asset checksums.

There is **no signed iPhone IPA or TestFlight release**. Download the source kit
for a physical iPhone; the Simulator ZIP is only a preview. Mac, iPhone and plugin
versions are 0.4.0; the existing wire protocol and OpenClaw baseline are unchanged.

## Upgrade notes

Back up settings you need to preserve. Prefer the Mac app's compatible
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
