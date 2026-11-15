**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.7.4

Screen-off and power-off times on the device · 2026-11-15

## Highlights

- **Set both idle times on the device.** Settings gains **Screen off** and **Power off**. Each opens a page where the top key moves the time one step later and the middle key one step earlier; a key greys out at the end of its scale. Leaving the page saves and applies the change. A computer or the web studio is no longer needed for this.
- **Same steps and the same rule as the web studio.** Screen-off from 15 seconds to 10 minutes; power-off from 5 minutes to 2 hours, or never. A later screen-off pushes power-off to the next allowed step, and power-off cannot move in front of screen-off.
- **A scrolling Settings list.** Settings now holds six items in four rows; the list follows the selected item and the header shows its position.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome/Edge, connect by USB and choose **Update firmware**.
Leave **Keep settings** enabled for a compatible update to retain saved networks, assistants and device identity.
A first installation or full replacement may clear configuration; read the [data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

This release changes the firmware only. Both plugins are unchanged apart from their version:
`ai-passport-openclaw-0.7.4.tgz` and `ai-passport-hermes-0.7.4.tar.gz` behave like 0.7.3, and the 0.7.3 plugins keep working with this firmware.
OpenClaw users who have not yet installed the 0.7.3 plugin still need it (or this one) for the call fixes of v0.7.3.
Restart the gateway after replacing plugin files: neither backend reloads them while running.
Firmware is shared; its bundle is `ai-passport-agent-firmware.tar.gz`.
For CLI installation, flash `agent-talk-full.bin` at `0x0`; `agent-talk.bin` is only for compatible application updates at `0x10000`.
Verify `SHA256SUMS.txt` and `release-manifest.json`; never publish device grants, Wi-Fi passwords or model keys.

Bluetooth stays off by default while online. Enable it in Passport Settings before phone/browser Bluetooth setup.

## Build and limitations

With ESP-IDF 5.5.3, run `./tools/validate.sh` from the matching tag. CI rebuilds from the immutable tag, validates firmware and re-downloads release assets before publication.
Native macOS/iOS apps remain source-only and are not built or distributed.

The two pages were exercised on one device through USB key commands with a local candidate: opening each page, stepping, saving on leaving, no change before leaving, persistence across a reboot, and restoring the previous values.
They were not operated with the physical keys, and how they look on the real screen was checked only in the host renderer.
The notice shown when saving fails is covered by the host renderer, not on hardware.
Automatic power-off and key wake remain unobserved on hardware, as in v0.7.3: the test device stays on a computer USB connection, which holds both timers.
A real spoken call with voice interruption has still not been repeated.

See [release readiness](readiness.md) for actual validation status.
