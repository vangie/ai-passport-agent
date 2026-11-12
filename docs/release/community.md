<p align="right"><a href="community.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Community submission · v0.5.1

Prepared for author review; **not submitted**. Description, instructions and update
notes are independent fields. Do not reuse expired one-time authorizations or create
a duplicate play to update an existing one. Classification is assigned by the server.

## Title

AI Passport OpenClaw · Your Wearable Voice Assistant

## Description

A lightweight, wearable voice interface for your own OpenClaw. Ask records a
question, shows your transcript and streams the text reply; follow up without
starting over. Call supports spoken conversation, scrolling subtitles, muting,
interruption and ending with the on-screen key labels.

Configure in the browser: desktop Chrome/Edge handles USB installation and updates;
compatible Bluetooth browsers edit Wi-Fi, hotspots and assistant connections.
iPhone users can use Bluefy. Settings stay on Passport; the page has no application
backend and does not relay conversations. After setup the device connects directly
over Wi-Fi. No companion application needs to stay open.

Bring a running OpenClaw with a working model. Copy the web studio's setup prompt
to an agent able to operate on the gateway host; it checks the plugin, dedicated
assistant and secure ingress before limited pairing. Public access may require your
own tunnel resources and authorization. Call additionally needs a realtime voice
provider; provider fees are separate. This is an independent open-source community
project. Images show actual interfaces with fictional demo data, not device photos.

## Quick-start instructions

1. Open https://ai-passport-openclaw.codelife.me/ in desktop Chrome/Edge and connect
   Passport by USB. For a first install uncheck Keep settings, install and confirm;
   compatible updates can preserve settings.
2. Choose Copy Prompt in OpenClaw and send it to an agent with access to your
   running gateway. Let it prepare the plugin and reachable ingress, then paste
   the returned address and limited code into the page. Call needs realtime voice.
3. Scan/select 2.4 GHz Wi-Fi, enter its password and Save configuration. Wait for
   the Wi-Fi icon. Retry corrected settings without reflashing; regenerate expired
   codes. For iPhone hotspots enable Allow Others to Join/Maximize Compatibility;
   keep the Personal Hotspot settings page open if discovery fails.
4. Choose Ask: press to record and again to send, or pause for automatic submission.
   Read/page through replies and continue. Choose Call, wait for Listening and
   speak; follow the keys to mute, interrupt or end. Switch saved networks under
   Settings → Wi-Fi; add networks using USB or a Bluetooth browser such as Bluefy.

## Update notes

- Browser-first USB flashing and Bluetooth configuration, including iPhone Bluefy.
- Unified Save/Discard, progressive Wi-Fi scan, assistant routes and bundled firmware.
- More reliable encrypted Bluetooth handover and clearer connection/button indicators.
- Native-client packages removed; firmware, plugin and checksums are retained.

## Files and provenance

- Source: https://github.com/vangie/ai-passport-openclaw
- Release: https://github.com/vangie/ai-passport-openclaw/releases/tag/v0.5.1
- Firmware: the exact release's verified `FoloToy-AI-Passport-full.bin`, not an older
  local build. Validate the published receipt/checksums before preparing upload.
- Cover: `00-cover.png`; gallery: `01-ask-and-call.png`, `02-web-studio.png`,
  `03-mobile-setup.png`. All are exact 1152 × 1536 portrait layouts; interface renders
  use fictional Wi-Fi/assistant data and `wss://gateway.example`.
- Rebuild with `node tools/community_materials.mjs` after preparing the browser
  dependencies. Output and separate UTF-8 publishing fields are under ignored
  `build/community-market/v0.5.1/materials/`. The renderer does not upload anything.
- Exported copy removes source-only soft line breaks while retaining paragraphs,
  numbered steps and update bullets. The preview uses natural browser text wrapping.
- Inspect the final four local PNGs and the metadata preview. Ask for author
  approval before community upload; GitHub publication does not imply community approval.
