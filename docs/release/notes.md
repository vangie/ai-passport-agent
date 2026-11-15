**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.7.3

Idle power settings, a centred home screen and two call fixes · 2026-11-15

## Highlights

- **Automatic power-off.** A device that has been in standby for 30 minutes now powers off by itself; any function key starts it again like a fresh boot. This board cannot cut its own supply from firmware, so power-off is deep sleep with Wi-Fi, audio and the display stopped. A computer USB connection, a key press or any activity restarts the timer.
- **Both idle times are adjustable.** **Device preferences** in the web studio has two stepped sliders: screen-off from 15 seconds to 10 minutes, power-off from 5 minutes to 2 hours or never. Power-off is always at least one minute later than screen-off, so the two cannot conflict; the firmware enforces the same rule.
- **A centred home screen.** The ring, greeting, caption and level meter share one axis that accounts for the key column on the right, and busy pages share the centre of the content area.
- **Calls survive a late interruption (OpenClaw).** Interrupting after the reply had already been generated, while its audio was still playing, left the gateway waiting for a cancellation the voice service could not confirm. It closed the call 30 seconds later. The plugin now settles such an interruption itself. This is the cause behind the remaining "call ends after a while" reports; v0.7.2 fixed a different path.
- **Chinese calls stay in Chinese (OpenClaw).** The call instructions now open with a line stating the reply language. Before, a one-syllable utterance was sometimes answered in English.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome/Edge, connect by USB and choose **Update firmware**.
Leave **Keep settings** enabled for a compatible update to retain saved networks, assistants and device identity.
A first installation or full replacement may clear configuration; read the [data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).
The idle sliders need this firmware; with older firmware they stay disabled and ask for an update.

Update the external plugin for your backend: `ai-passport-openclaw-0.7.3.tgz` or `ai-passport-hermes-0.7.3.tar.gz`.
Both call fixes are in the OpenClaw plugin, not the firmware: OpenClaw users must install the new plugin to get them.
The Hermes plugin is unchanged apart from its version.
Restart the gateway after replacing plugin files: neither backend reloads them while running.
Firmware is shared; its bundle is `ai-passport-agent-firmware.tar.gz`.
For CLI installation, flash `agent-talk-full.bin` at `0x0`; `agent-talk.bin` is only for compatible application updates at `0x10000`.
Verify `SHA256SUMS.txt` and `release-manifest.json`; never publish device grants, Wi-Fi passwords or model keys.

Bluetooth stays off by default while online. Enable it in Passport Settings before phone/browser Bluetooth setup.

## Build and limitations

With ESP-IDF 5.5.3, run `./tools/validate.sh` from the matching tag. CI rebuilds from the immutable tag, validates firmware and re-downloads release assets before publication.
Native macOS/iOS apps remain source-only and are not built or distributed.

Automatic power-off and key wake were not observed on hardware: the test device stayed on a computer USB connection, which holds both timers. They are covered by host tests of the policy and by the existing deep-sleep path of the board support package.
The home screen was checked by pixel tests and in the host renderer, and a local candidate with this layout was flashed to one device.
The interruption fix was reproduced in a host test and checked on a live gateway with synthetic speech: an interruption after a finished reply returned in 8 ms and the call was still open 34 seconds later. A real spoken call with voice interruption has not been repeated, and interrupting during an agent consult was not exercised live.
The reply-language change was measured directly against the voice model with rebuilt gateway instructions: 4 of 50 one-syllable prompts were answered in English before and 0 of 50 after.
Gateways bound call length on their own: OpenClaw closes a call after 30 minutes and Hermes after 60.
Voice detection cannot exclude every noise source; nearby speech can still interrupt a reply.

See [release readiness](readiness.md) for actual validation status.
