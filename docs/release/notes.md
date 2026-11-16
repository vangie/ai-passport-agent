**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.7.5

Automatic power-off now stays off until a key wakes the device · 2026-11-16

## Highlights

- **Keep automatic power-off asleep.** The device now clears the standby timer wake source before entering deep sleep, so it remains off until a function key is pressed.
- **Preserve the existing idle controls.** Screen-off and power-off times remain adjustable on the device and in the web studio, with the same ordering rule.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome/Edge, connect by USB and choose **Update firmware**.
Leave **Keep settings** enabled for a compatible update to retain saved networks, assistants and device identity.
A first installation or full replacement may clear configuration; read the [data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

This release includes the same OpenClaw and Hermes plugin interfaces as v0.7.4. The versioned packages are
`ai-passport-openclaw-0.7.5.tgz` and `ai-passport-hermes-0.7.5.tar.gz`.
Restart the gateway after replacing plugin files. Firmware is shared by both backends.

Bluetooth stays off by default while online. Enable it in Passport Settings before phone/browser Bluetooth setup.

## Build and limitations

With ESP-IDF 5.5.3, run `./tools/validate.sh` from the matching tag. CI rebuilds from the immutable tag, validates firmware and re-downloads release assets before publication.
Native macOS/iOS apps remain source-only and are not built or distributed.

The automatic power-off fix was reproduced on the maintainer device with shortened timers: the device entered deep sleep and remained off until a function key woke it. Physical-key and long-term battery behavior should still be checked on each hardware revision.

See [release readiness](readiness.md) for the complete validation record.
