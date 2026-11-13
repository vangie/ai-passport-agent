**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.7.0

Stable release · 2026-11-13

## Highlights

- **Preserve spoken replies.** Tentative voice-interruption checks pause the
  playback cursor instead of discarding reply syllables. They confirm speech after
  speaker echo settles, rather than relying on waveform matching alone.
  Both backend plugins prevent independent upstream audio interruption from
  competing with the device's voice/button controls. Reply-phase errors no longer
  leave OpenClaw calls waiting through a fictitious reconnection.
  Call layout changes request a complete redraw on the display task, avoiding the
  application-stack crash during connection.
- **Choose your personal agent.** Hermes joins OpenClaw for Ask and Call. Ask uses
  the native Hermes agent's tools, memory and per-device conversation, not a new
  standalone chatbot. Existing OpenClaw connections remain compatible.
- **Prepare the right backend.** Select OpenClaw or Hermes when adding an assistant
  in the web studio, then copy its bilingual setup prompt. It checks the matching
  plugin, provider and secure gateway ingress rather than mixing setup commands.
- **Know who is connected.** Passport recognizes the authenticated backend and
  shows its icon separately from the configured assistant name. Insets keep the
  icons away from rounded screen corners.
- **Upgrade without core patches.** The Hermes plugin and private device grants
  live outside the Hermes checkout. A versioned plugin archive joins the release,
  with checksums, source provenance and the Hermes icon license. Browser tests now
  also gate tag publication. Native apps remain source-only.

## Install or update

Start at <https://ai-passport-agent.codelife.me/> and the
[getting-started guide](../../README.md#get-started). Hermes requires v0.7.0 or
newer firmware and its matching external plugin; v0.6.0 does not include it.

For a compatible USB update, leave **Keep settings** enabled. A first installation
or full replacement overwrites configuration; read the
[data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).
Existing identities, saved networks, OpenClaw plugin ID and pairing remain compatible.
Adding Hermes requires its own gateway pairing. Older website links continue to redirect.

Install only your backend's host plugin: `ai-passport-openclaw-0.7.0.tgz` or
`ai-passport-hermes-0.7.0.tar.gz`. Firmware is shared between backends; the bundle
is `ai-passport-agent-firmware.tar.gz`. Verify `SHA256SUMS.txt` and
`release-manifest.json`; never upload private device grants or model keys.

## Validation and limitations

Hermes v0.22.6+481 has exercised physical Ask/Call; upgrade to v0.23.1 passed plugin
registration and actual-runtime adapter tests. The maintainer subsequently confirmed
physical regression and voice-interruption acceptance for release. Provider voice
quality, echo and recognition accuracy are not
universally established. Calls use a realtime model for ordinary dialogue and
consult the native agent for tasks/context; not every turn executes the full agent.
Both backends need your providers; Call requires realtime voice configuration.
Device-side interactive tool approval is not supported.

Automated acoustic checks completed two OpenClaw turns without false interruption,
reset or playback stall. Some capture chunks were dropped and ACK delays reached
several seconds; latency and recognition remain dependent on the network/provider.

See the [release-readiness checklist](readiness.md) before tagging or community submission.
