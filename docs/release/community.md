<p align="right"><a href="community.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Community submission · v0.7.0

Prepared for existing play **2236** only. Not submitted. A readback on 2026-11-13
confirmed published revision **5492**, approved, with both titles below and the
current source repository. No draft or pending revision existed. The new scoped
authorization matched this project and had no submission result. Before any update, read
`projects` / `publishedRevision`. Stop if a draft/pending revision or version conflict
exists; do not withdraw it, create a duplicate or consume an old code.

## Title

Agent Talk

## Description

Agent Talk is a voice interface to your own personal agent, with OpenClaw and Hermes support.
Use Ask for questions and tasks: see your words, read replies as they arrive and follow up in the same conversation.
Use Call for spoken back-and-forth, with scrolling subtitles and on-screen keys to mute, interrupt or end.

It connects to a personal agent, not just a generic chat model. Ask uses your agent's authorized tools and context:
OpenClaw's dedicated assistant or your native Hermes agent. Call's realtime model handles ordinary conversation
and can consult that agent for tasks, current information or private context; not every turn invokes the full agent.

Set up in your browser. Use computer Chrome/Edge and USB to install or update firmware; use a compatible
Bluetooth browser to change settings, including Bluefy on iPhone. Save home Wi-Fi, office Wi-Fi and a phone hotspot,
then choose a saved network on Passport. The setup site has no application backend and does not relay conversations;
once configured, Passport connects to your gateway over Wi-Fi without using the phone as an audio relay.

Bring a working OpenClaw or Hermes with a usable model. Choose its backend in the web studio and send the setup
prompt to an agent with access to that host, to prepare the plugin, secure reachable ingress and limited pairing code.
Remote access may require your own tunnel resources; Call needs a realtime voice service, billed by your provider.
Independent open-source community project. Images are actual UI renders with fictional data, not device photographs.

## Quick-start instructions

1. In computer Chrome/Edge, open https://ai-passport-agent.codelife.me/, connect USB and update firmware.
   For a first install, uncheck Keep settings and confirm; compatible updates may retain it.
2. Select OpenClaw or Hermes under Add assistant. Send its setup prompt to an agent on your gateway host;
   paste the returned address and pairing code into the page. Call also needs realtime voice configuration.
3. Scan for reachable 2.4 GHz Wi-Fi, enter its password and Save configuration. The Wi-Fi icon confirms connection.
   Correct failures and retry without reflashing; regenerate expired pairing codes. For iPhone hotspots enable Allow Others
   to Join and Maximize Compatibility; keep that settings page open if discovery fails.
4. Ask: press to record, then press again or pause to send; follow up after the reply. Call: wait for Listening,
   speak and use displayed keys to mute, interrupt or end. Switch saved networks in Settings → Wi-Fi;
   add networks through USB or Bluetooth in the web studio (Bluefy on iPhone).

## Update notes

- Add Hermes alongside OpenClaw: Ask reaches the native personal agent and keeps a conversation per device; Call can delegate tasks.
- Select the backend when adding an assistant; use matching bilingual setup prompts for installation and pairing.
- Show the authenticated backend's icon separately from the assistant name; inset icons away from rounded screen corners.
- Supply an external Hermes plugin that survives ordinary core upgrades, with versioned packaging and clearer setup guides. Fix replies cut short, false reconnecting states and incomplete redraws when entering Call.

## Files and provenance

- Source: https://github.com/vangie/ai-passport-agent
- Version: v0.7.0. Use only the verified assets from its completed GitHub release.
- Preserve both approved titles from the paired documents. Do not change other plays.
- Generate materials with `node tools/community_materials.mjs`; output:
  `build/community-market/v0.7.0/materials/`. Cover `00-cover.png` plus three optional posters are 1152 × 1536.
- All network names, assistant names and endpoints are fictional. Inspect the final PNG files and independent UTF-8
  description, instructions and update-note files before uploading the same files. Generation does not submit anything.
- Only use the verified published full firmware after release; compare with the actual public community revision,
  not merely these candidate notes. Current private community status is unavailable without a fresh authorization.
