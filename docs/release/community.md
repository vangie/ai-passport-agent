<p align="right"><a href="community.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Community submission · v0.7.1

Update existing play **2236** only; never create a duplicate. Obtain a fresh grant and read
`projects` / `publishedRevision`, comparing notes against the actual public revision.
Stop on draft/pending or version conflicts; do not withdraw or overwrite.
The v0.7.0 submission was revision 5548, pending at the time; that is not current-state evidence.
This version has not been submitted yet.

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

1. Have a working OpenClaw or Hermes installation. Open https://ai-passport-agent.codelife.me/ in desktop Chrome/Edge, connect USB and update firmware. Uncheck Keep settings for first installation; retain it for compatible updates.
2. Choose your backend under Add assistant. Give its prompt to an agent on the gateway host and paste back its address and pairing code; Call also needs realtime voice setup. Scan 2.4 GHz Wi-Fi, enter its password and Save configuration. A Wi-Fi icon confirms connection; correct failures and retry, or regenerate expired pairing codes.
3. Ask: press to record, then press again or pause to send; follow up after the reply. Call: wait for Listening, speak and use displayed keys to mute, interrupt or end.
4. Switch saved networks in Settings → Wi-Fi. Add networks using web USB/Bluetooth; enable Bluetooth in device Settings first. On iPhone use Bluefy. Enable Allow Others to Join and Maximize Compatibility for hotspots; keep the hotspot settings page open if discovery fails.

## Update notes

- Keep chat visible throughout follow-up questions and follow new replies to the latest content without moving readers reviewing history. Animate waiting feedback.
- Improve connection/reconnection preparation, strengthen Ask noise filtering and fix longer recordings sometimes failing to send.
- Expand call transcript space, simplify settings/home actions, add battery color levels and cover known missing characters.
- Turn Bluetooth off by default while online to reserve resources; enable it in device Settings before setup. Preserve website recognition of older versions and clarify firmware download names.

## Files and provenance

- Source: https://github.com/vangie/ai-passport-agent
- Version: v0.7.1. Use only the verified `agent-talk-full.bin` from its completed GitHub release.
- Release: https://github.com/vangie/ai-passport-agent/releases/tag/v0.7.1
- Use the full-image hash in that release's `release-manifest.json`; upload the same file to the community, not a local candidate.
- Preserve both approved titles from the paired documents. Do not change other plays.
- Generate materials with `node tools/community_materials.mjs`; output:
  `build/community-market/v0.7.1/materials/`. Cover `00-cover.png` plus three optional posters are 1152 × 1536.
- All network names, assistant names and endpoints are fictional. Inspect the final PNG files and independent UTF-8
  description, instructions and update-note files before uploading the same files. Generation does not submit anything.
- Before uploading, compare the live public revision and trim update notes accordingly. Submit only the inspected cover and verified release firmware.
