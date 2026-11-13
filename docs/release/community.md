<p align="right"><a href="community.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Community submission · v0.6.0

Submitted on 2026-11-13 for existing play **2236** as **Agent Talk**, revision
**5492**; authenticated readback confirms **pending review, not publicly released**.
The earlier pending revision 5485 was withdrawn before this replacement was
submitted. Public revision **5467** remains available while the update is reviewed.
The Chinese community name is maintained independently in the paired document;
the GitHub repository and web studio keep the AI Passport Agent project name.
Description, instructions and update notes are independent fields. An update requires a fresh
`update_once` grant bound to that play; a `create_once` grant cannot update it.
Do not create a duplicate play. Classification is assigned by the server.

## Title

Agent Talk

## Description

Agent Talk brings voice access to your own personal agent. Currently supports OpenClaw.
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
   by USB. Choose Update firmware; for a first install uncheck Keep settings and
   confirm. Compatible updates can preserve settings.
2. Copy the prompt under OpenClaw to an agent with access to your running gateway.
   Let it prepare the plugin and reachable ingress; paste its address and pairing
   code into the page. Call additionally needs a realtime voice service.
3. Scan/select 2.4 GHz Wi-Fi, enter its password and Save configuration. The Wi-Fi
   icon confirms connection. Correct settings and retry without reflashing;
   regenerate expired codes. For iPhone hotspots enable Allow Others to Join and
   Maximize Compatibility; keep Personal Hotspot settings open if discovery fails.
4. Ask: press to record, then press again or pause to send; read replies and follow
   up. Call: wait for Listening, then speak; use the displayed keys to mute,
   interrupt or leave. Switch saved networks under Settings → Wi-Fi; add networks
   with USB or a Bluetooth browser (Bluefy on iPhone).

## Update notes

- Renamed the community play to Agent Talk, with a separate Chinese name; OpenClaw support, existing pairing and saved networks are unchanged.
- Added on-device speech detection to Ask and Call, reducing non-speech uploads and accidental sends.
- Kept completed chats when no speech is detected; Call retains the beginning of speech and checks for speech before voice interruption.
- Moved setup to ai-passport-agent.codelife.me, kept old-address redirects and refreshed the cover, bilingual guides and source links.

## Files and provenance

- Source: https://github.com/vangie/ai-passport-agent
- Community title: Agent Talk. Repository and web studio branding remain unchanged;
  the renderer reads the community titles from these paired documents.
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
