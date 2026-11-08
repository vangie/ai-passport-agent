[Simplified Chinese](notes.zh_CN.md) · **English**

# Release assets

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
- `SHA256SUMS.txt`: release-asset checksums.

CI builds are not hardware/audio acceptance. The OpenClaw plugin baseline is
2026.10.5; check compatibility before upgrading. Read the repository's setup,
networking and plugin instructions. No endpoints, account keys, Wi-Fi passwords
or device authorizations are distributed with this release.
