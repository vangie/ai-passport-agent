**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.8.1

Verified Muse pairing, status-bar icon, proxy inheritance and voice-turn fixes · 2026-11-27

## Highlights

- **Streamlined Muse setup and BLE pairing.** The web studio unifies Muse into a
  single **Muse** assistant option and writes the Gadget SDK token through
  **Save settings**. On Passport, **Settings → Agent → Pair Muse** uses verified
  Muse Link GATT handles, `gadget` device discovery and single-step on-device
  confirmation while keeping Passport awake throughout pairing.
- **Muse status-bar icon and automatic proxy inheritance.** Paired Muse assistants
  display a dedicated 22 × 22 status-bar icon and automatically inherit an existing
  assistant's Shadowsocks route when no explicit Muse route has been saved yet.
- **Reliable Muse voice turns on ESP32-C3.** Noise single-frame payloads and NDJSON
  events are decoded in place with a 14 KB WebSocket frame buffer, multi-message
  assistant reply chains are collected cleanly, and non-retryable failures return
  to the home screen with the error message instead of an empty conversation page.
- **Clean speech capture and recognition.** Incremental Flash erases now run one
  4 KB sector at a time inside a 100 ms I2S DMA window to prevent mid-sentence
  dropouts, and Muse uploads trim the 120 ms codec wake-up pop with an 80 Hz
  high-pass filter and automatic speech gain normalization.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome or Edge, connect
Passport over USB and choose **Update firmware** with **Keep settings** enabled.
This application-only update preserves saved networks, assistants and device
identity. Select **Muse** under **Add an agent**, paste the personal Gadget
SDK token over USB and choose **Save settings**, then complete **Settings → Agent → Pair Muse** with the Muse app. Use
the Muse assistant's **Connection settings** to choose Direct or Shadowsocks.

Existing gateway adapters remain independently versioned and do not need replacement
or re-pairing. Firmware updates never replace or restart a gateway automatically.

## Validation and limits

The firmware build, merged-image layout, host tests, browser tests, on-device Muse
BLE pairing and end-to-end voice questions over Shadowsocks pass. The device has no
PSRAM and the application partition remains nearly full, so future additions need a
size check.
