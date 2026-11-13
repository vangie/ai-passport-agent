<p align="right"><a href="community.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Community submission · v0.6.0

Submitted on 2026-11-13 for existing play **2236**, revision **5485**; status at
submission: **pending review, not publicly released**. The existing public revision
5467 remains available while the update is reviewed. Description, instructions and
update notes are independent fields. A later update requires a fresh
`update_once` grant bound to that play; a `create_once` grant cannot update it.
Do not create a duplicate play. Classification is assigned by the server.

## Title

AI Passport Agent · Your Wearable Voice Assistant

## Description

A wearable voice terminal for your personal agent. Currently supports OpenClaw.
Ask a question or give your own assistant a task; see your transcript and the
streamed reply, then follow up in the same conversation. Call offers spoken
replies and scrolling subtitles, with on-screen keys to mute, interrupt or end.

This connects to your own agent, not just a generic model chat. Ask uses a
dedicated OpenClaw assistant with its permitted tools. Call uses a realtime voice
service and can consult the agent for tasks and context; not every spoken turn
runs through the full agent. On-device speech detection reduces noise-triggered
submissions, but does not identify the speaker or eliminate every false trigger.

Configure in the browser: desktop Chrome/Edge handles USB installation and updates;
compatible Bluetooth browsers edit Wi-Fi, hotspots and assistant connections.
iPhone users can use Bluefy. Save home and office Wi-Fi networks and a phone hotspot
on Passport, then select a saved network under Settings → Wi-Fi without re-entering
its details. The page has no application backend and does not relay conversations. After setup the device connects directly
over Wi-Fi. No phone audio relay is required.

Bring a running OpenClaw with a working model. Copy the web studio's setup prompt
to an agent able to operate on the gateway host; it checks the plugin, dedicated
assistant and secure ingress before limited pairing. Public access may require your
own tunnel resources and authorization. Call additionally needs a realtime voice
provider; provider fees are separate. This is an independent open-source community
project. Images show actual interfaces with fictional demo data, not device photos.

## Quick-start instructions

1. Open https://ai-passport-agent.codelife.me/ in desktop Chrome/Edge and connect
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

- Renamed to AI Passport Agent; currently supports OpenClaw and retains existing pairing and saved networks.
- Added on-device speech detection to Ask and Call, reducing non-speech uploads and accidental sends.
- Kept completed chats when no speech is detected; Call retains the beginning of speech and checks for speech before voice interruption.
- Moved setup to ai-passport-agent.codelife.me and refreshed source links, bilingual guides, the cover and configuration screenshots.

## Files and provenance

- Source: https://github.com/vangie/ai-passport-agent
- Release: https://github.com/vangie/ai-passport-agent/releases/tag/v0.6.0
- Firmware: the exact release's verified `FoloToy-AI-Passport-full.bin`, not an older
  local build. Validate the published receipt/checksums before preparing upload.
- Cover: `00-cover.png`; gallery: `01-ask-and-call.png`, `02-web-studio.png`,
  `03-mobile-setup.png`. All are exact 1152 × 1536 portrait layouts; interface renders
  use fictional Wi-Fi/assistant data and `wss://gateway.example`.
- Rebuild with `node tools/community_materials.mjs` after preparing the browser
  dependencies. Output and separate UTF-8 publishing fields are under ignored
  `build/community-market/v0.6.0/materials/`. The renderer does not upload anything.
- Exported copy removes source-only soft line breaks while retaining paragraphs,
  numbered steps and update bullets. The preview uses natural browser text wrapping.
- Inspect the final four local PNGs and the metadata preview before each upload.
  An explicit official update grant authorizes its target without another approval
  step; otherwise obtain author approval. GitHub publication does not imply community approval.
