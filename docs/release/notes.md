**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.8.0

Muse direct support and per-agent proxy routing · 2026-11-27

## Highlights

- **Add Muse direct.** The badge integrates the official Muse Gadget SDK Link and
  Noise paths, stores the SDK token and pairing state on the device, and supports
  voice questions with text replies.
- **Keep existing gateway compatibility.** OpenClaw and Hermes pairing, stored
  credentials and the existing gateway protocol remain compatible, so existing
  assistants do not need to be paired again.
- **Add per-assistant routing.** Each assistant can use Direct or Shadowsocks.
  Muse can use the same proxy path as an external gateway without changing other
  assistants.
- **Add web setup controls.** The studio provides Muse token writing, pairing
  guidance and route editing. Tokens are write-only.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome or Edge, connect
Passport over USB and choose **Update firmware** with **Keep settings** enabled.
This application-only update preserves saved networks, assistants and device
identity. Select **Muse direct** under **Add assistant**, write the personal Gadget
SDK token over USB, then complete **Settings → Pair Muse** with the Muse app. Use
the Muse assistant's **Connection settings** to choose Direct or Shadowsocks.

Existing gateway adapters remain independently versioned and do not need replacement
or re-pairing. Firmware updates never replace or restart a gateway automatically.

## Validation and limits

The firmware build, merged-image layout, host tests, browser tests and Muse protocol
tests pass. Direct Muse account pairing and a first real Muse answer still require
validation with a real Muse account and app. The device has no PSRAM and the
application partition remains nearly full, so future additions need a size check.
