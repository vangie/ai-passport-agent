**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.7.2

Call reliability fix · 2026-11-15

## Highlights

- **Calls stay connected.** When several people talk around the badge, it interrupts replies often. The realtime voice service can then reject one of those requests while the session itself stays healthy. Earlier firmware treated that rejection as a lost connection: it muted the microphone, showed "connecting" and gave up 30 seconds later, because with the microphone muted no recovery signal could arrive. The badge now keeps listening.
- **A bound on real faults.** The call still ends with the gateway's message after more than five such errors in a row with no reply or transcript between them, when the gateway closes the session, or after 90 seconds without any data.
- **Hermes follows the same rule.** The Hermes plugin no longer ends an established call because the voice service rejected a single request.
- **Diagnostics name the fault.** The OpenClaw plugin logs a category for each upstream error from a fixed vocabulary, never the error text. The device's call diagnostic counts stream errors.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome/Edge, connect by USB and choose **Update firmware**.
Leave **Keep settings** enabled for a compatible update to retain saved networks, assistants and device identity.
A first installation or full replacement may clear configuration; read the [data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

Update the external plugin for your backend: `ai-passport-openclaw-0.7.2.tgz` or `ai-passport-hermes-0.7.2.tar.gz`.
The firmware fix works with the v0.7.1 plugins; Hermes users need the new plugin for the Hermes-side fix.
Restart the gateway after replacing plugin files: neither backend reloads them while running.
Firmware is shared; its bundle is `ai-passport-agent-firmware.tar.gz`.
For CLI installation, flash `agent-talk-full.bin` at `0x0`; `agent-talk.bin` is only for compatible application updates at `0x10000`.
Verify `SHA256SUMS.txt` and `release-manifest.json`; never publish device grants, Wi-Fi passwords or model keys.

Bluetooth stays off by default while online, as in v0.7.1. Enable it in Passport Settings before phone/browser Bluetooth setup.

## Build and limitations

With ESP-IDF 5.5.3, run `./tools/validate.sh` from the matching tag. CI rebuilds from the immutable tag, validates firmware and re-downloads release assets before publication.
Native macOS/iOS apps remain source-only and are not built or distributed.

The fix was reproduced in a host test from a recorded failure and a local candidate was flashed and checked with synthetic calls on both backends.
A real long call with several speakers has not been repeated on the tagged artifact.
The exact upstream error text of the recorded failure was not logged, so its category is inferred from timing.
Gateways bound call length on their own: OpenClaw closes a call after 30 minutes and Hermes after 60. This release does not change that.
Voice detection cannot exclude every noise source; nearby speech can still interrupt a reply.

See [release readiness](readiness.md) for actual validation status.
