[Simplified Chinese](notes.zh_CN.md) · **English**

# AI Passport Agent v0.6.0

## Highlights

- **A new name, the same paired device.** AI Passport OpenClaw is now
  **AI Passport Agent**, a wearable voice terminal for your personal agent.
  **OpenClaw is the only supported backend in this release.**
- **Less noise sent as speech.** Ask and Call now use on-device speech detection.
  Ask discards recordings with no detected speech without clearing completed
  chats. After detected speech, about two seconds of silence submits the question.
- **Speech starts more naturally.** Call retains a short, bounded pre-roll and
  requires speech confirmation for voice interruption. Button interruption and
  mute remain available. Speech-like noise may still pass; very quiet or short
  speech may be missed. This is not speaker verification.
- **Browser-first setup.** Configure Wi-Fi, hotspots and OpenClaw, or update
  firmware from the web studio. The studio has no application backend and writes
  settings directly over USB/Bluetooth; conversations go to your configured services.

## Install and update

Start at <https://vangie.github.io/ai-passport-agent/> and follow the
[getting-started guide](../../README.md#get-started). The stable Pages link follows
the active custom domain during the hostname transition.

For a compatible USB update, leave **Keep settings** enabled. A first installation
or full replacement overwrites device configuration; see the
[data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).
Existing device identities, pairing, saved networks, wire protocol and the
`ai-passport-openclaw` plugin ID are unchanged. No re-pairing is required merely
because of the rename. Native apps remain source-only, not release downloads.

The plugin package is `ai-passport-openclaw-0.6.0.tgz`; install it on the OpenClaw
gateway host. The firmware archive is `ai-passport-agent-firmware.tar.gz`.
Use `SHA256SUMS.txt` and `release-manifest.json` to check downloads and provenance.

## Validation and limitations

This release is gated by host tests, a fresh firmware build, merged-image/layout
checks, browser UI tests and plugin package inspection. The maintainer reports
the speech-filtering firmware useful in device testing; this is not a guarantee
for every microphone, noise source, voice provider or network. Call still needs
a configured realtime voice service. Model/provider fees are separate.
