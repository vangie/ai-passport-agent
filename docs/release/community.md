<p align="right"><a href="community.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Community submission · v0.8.1

Prepared for play **2236**. Previous revision **5782** was submitted for v0.7.5;
this file tracks the v0.8.1 community materials and copy.

## Title

Agent Talk

## Description

Agent Talk is a voice interface to your own personal agent, with Muse, OpenClaw and Hermes support.
Use Ask for questions and tasks: see your words, read replies as they arrive and follow up in the same conversation.
Use Call on OpenClaw or Hermes for spoken back-and-forth, with scrolling subtitles and on-screen keys to mute, interrupt or end.

It connects to your own agent or Muse account, not a generic demo chatbot. Ask uses your agent's authorized tools and context
on OpenClaw or Hermes, or talks directly to Muse through the official Gadget SDK path with text replies. Call's realtime model handles
ordinary conversation and can consult your gateway agent for tasks, current information or private context.

Set up in your browser. Use computer Chrome/Edge and USB to install or update firmware; use a compatible
Bluetooth browser to change settings, including Bluefy on iPhone. Save home Wi-Fi, office Wi-Fi and a phone hotspot,
then choose a saved network or assistant on Passport. Each assistant can use Direct or its own Shadowsocks proxy route;
the setup site has no application backend and does not relay conversations.

Bring a Muse account (with a personal Gadget SDK token and the Muse app) or a working OpenClaw or Hermes installation.
In the web studio, paste your Muse SDK token or send the setup prompt to an agent on your gateway host to prepare the plugin,
reachable ingress and limited pairing code. Call needs a realtime voice service, billed by your provider. Independent open-source community project.

## Quick-start instructions

1. Have a Muse account or a working OpenClaw or Hermes installation. Open https://ai-passport-agent.codelife.me/ in desktop Chrome/Edge, connect USB and update firmware. Uncheck Keep settings for first installation; retain it for compatible updates.
2. Under Add an agent, select Muse to paste your personal Gadget SDK token and Save settings, then complete Settings → Agent → Pair Muse with the Muse app; or select OpenClaw/Hermes, send its prompt to an agent on the gateway host and paste back its address and pairing code. Scan 2.4 GHz Wi-Fi, enter its password and Save settings; configure Direct or Shadowsocks under Connection settings when needed.
3. Ask: press to record, then press again or pause to send; follow up after the reply. Call (OpenClaw/Hermes): wait for Listening, speak and use displayed keys to mute, interrupt or end. The screen turns off after 1 idle minute and Passport powers off after 30; press any function key to wake or start it. Adjust both times under Screen off and Power off in device Settings.
4. Switch saved assistants in Settings → Agent and networks in Settings → Wi-Fi. Add networks using web USB/Bluetooth; enable Bluetooth in device Settings first. On iPhone use Bluefy. Enable Allow Others to Join and Maximize Compatibility for hotspots; keep the hotspot settings page open if discovery fails.

## Update notes

- Add Muse support with on-device SDK token storage, Muse app BLE pairing, a dedicated 22 × 22 status-bar icon and voice questions with text replies.
- Add per-assistant Direct or Shadowsocks proxy routing, with automatic Shadowsocks route inheritance for Muse when a proxy is already configured on the device.
- Improve ESP32-C3 Muse voice turns and recording quality: 14 KB in-place Noise frame decoding, multi-message reply chains, single-sector Flash erases with a 100 ms I2S DMA buffer, and 80 Hz high-pass speech normalization.

## Files and provenance

- Source: https://github.com/vangie/ai-passport-agent
- Version: v0.8.1. Use only the verified `agent-talk-full.bin` from its completed GitHub release.
- Release: https://github.com/vangie/ai-passport-agent/releases/tag/v0.8.1
- Use the full-image hash in that release's `release-manifest.json`; upload the same file to the community, not a local candidate.
- Preserve both approved titles from the paired documents. Do not change other plays.
- Generate materials with `node tools/community_materials.mjs`; output: `build/community-market/v0.8.1/materials/`.
- Inspect the final cover and optional images before uploading the same files. All demo data must remain fictional.
