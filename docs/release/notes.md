**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.7.1

Stability and experience update · 2026-11-14

## Highlights

- **Stay in the conversation.** Follow-up recording, sending and waiting preserve existing chat. New replies follow the latest content without moving readers who paged back. Animated waiting feedback makes progress visible.
- **Lighter connections, less noise.** Improve clock synchronization, connection reuse and TLS reconnection to reduce repeated preparation. Ask filters recording-start noise and short bursts, and longer recordings no longer hit the old gateway frame limit. First connection latency and recognition still depend on the network, provider and gateway model.
- **A clearer screen.** More room for call subtitles, simpler assistant settings and duplicate labels, and balanced home voice actions. Battery icon and percentage use three color levels. Expanded text/punctuation coverage and a monochrome keyboard fallback avoid known missing glyphs.
- **Reserve resources for dialogue.** Reduce firmware size and disable Bluetooth by default while online; enable it manually for setup. Downloads are now `agent-talk.bin` and `agent-talk-full.bin`, while the website continues recognizing historical versions.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome/Edge, connect by USB and choose **Update firmware**.
Leave **Keep settings** enabled for a compatible update to retain saved networks, assistants and device identity.
A first installation or full replacement may clear configuration; read the [data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

Update the external plugin for your backend: `ai-passport-openclaw-0.7.1.tgz` or `ai-passport-hermes-0.7.1.tar.gz`.
Firmware is shared; its bundle is `ai-passport-agent-firmware.tar.gz`.
For CLI installation, flash `agent-talk-full.bin` at `0x0`; `agent-talk.bin` is only for compatible application updates at `0x10000`.
Verify `SHA256SUMS.txt` and `release-manifest.json`; never publish device grants, Wi-Fi passwords or model keys.

**Bluetooth behavior changed:** it is off by default while online. Enable it in Passport Settings before phone/browser Bluetooth setup.
Offline devices still enable setup automatically; USB setup is unaffected. On iPhone use Bluefy because Safari/Chrome lack Web Bluetooth.

## Build and limitations

With ESP-IDF 5.5.3, run `./tools/validate.sh` from the matching tag. CI rebuilds from the immutable tag, validates firmware and re-downloads release assets before publication.
Native macOS/iOS apps remain source-only and are not built or distributed.

Earlier local candidates were flashed and checked for boot/networking, and the user confirmed visible UI changes; the tagged artifact still needs separate hardware acceptance.
Voice detection cannot exclude every noise source or recognize every short phrase.
ASR models are gateway configuration: this release does not embed large-v3-turbo or apply the maintainer's host configuration as user defaults.
Call uses a realtime model for ordinary dialogue and consults the agent for tasks/context. Device-side interactive tool approval is not supported.

See [release readiness](readiness.md) for actual validation status.
