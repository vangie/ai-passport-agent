<p align="right"><a href="configuration.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Configuration and everyday use

## Project name and compatibility

AI Passport Agent is the product name; `vangie/ai-passport-agent` is its source
repository. v0.7.0 supports OpenClaw and Hermes; v0.6.0 supports OpenClaw only.
The OpenClaw plugin keeps
the package/ID `ai-passport-openclaw`, including existing configuration entries
and versioned `.tgz` packages; this is an integration name, not the product name.

Existing USB/Bluetooth firmware still identifies itself as `ai-passport-openclaw`
in `HELLO` messages. Keep this wire identifier, BLE service UUIDs, native bundle
IDs, device identity, NVS storage and pairing credentials unchanged. The retained
native clients generate `ai-passport-agent://` configuration links and still
accept `ai-passport-openclaw://` links. Their existing Wi-Fi cache path is also
unchanged. Renaming does not require reflashing or pairing again.

The stable Pages URL follows the active custom domain during migration. The old
`ai-passport-openclaw.codelife.me` address redirects to
`ai-passport-agent.codelife.me`; see [legacy address compatibility](web.md#legacy-address-compatibility).
Browser USB/Bluetooth permissions may need granting again at the new origin;
device configuration is unchanged. Website hostname and community listing
changes are separate from the repository rename; neither changes a user's
gateway endpoint. Firmware archives use `ai-passport-agent-firmware.tar.gz`
in future releases; already-published assets retain their original names.

## Configuration tools

Use the [web studio](https://vangie.github.io/ai-passport-agent/). Desktop Chrome/Edge
supports USB configuration and firmware installation; compatible Web Bluetooth
browsers support wireless settings. On iPhone, [get Bluefy from the App Store](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
and open the configuration site inside it, not Safari/Chrome. The site's Bluetooth compatibility hint links directly to this listing.
Safari/Chrome on iPhone do not provide Web Bluetooth; Bluefy does, letting this page
configure Passport over Bluetooth. Bluefy is only a setup browser: close it after saving,
and Passport connects to its configured agent independently over Wi-Fi.
Firmware flashing still needs a computer and USB. See [browser compatibility](web.md).

## Prepare your agent gateway

Choose OpenClaw or Hermes under **Add assistant**, then **Copy Prompt**. Send it to an agent able to
operate on the gateway host. That host need not be your setup computer. The page
does not install server software itself. You need:

- A running OpenClaw or Hermes with a working model and host access for the setup agent.
- Passport, a computer/data cable for initial flashing, and reachable 2.4 GHz
  Wi-Fi or a compatible phone hotspot.
- Authorized tunnel resources and usable DNS/TLS for remote access. The prompt
  reuses existing ingress or asks for missing login, permissions or resources;
  it does not provide accounts, free services or permission for new charges.
- A realtime voice provider for Call. Ask can be configured independently.

The agent checks/installs the matching plugin, prepares transcription/model settings
and persistent public WSS ingress. OpenClaw uses the dedicated `passport` agent;
Hermes uses its native personal agent and per-device sessions, not an OpenClaw agent.
It backs up affected configuration, retains device authentication and loopback
binding, and restarts only when necessary. DNS/TLS/WebSocket checks precede the
limited pairing code; these are readiness checks, not microphone acceptance.
Paste the returned address/code into the page and save alongside Wi-Fi settings.
Expired or consumed codes need regeneration. Never paste administrator tokens
or model keys into device configuration.

For manual or LAN-only setup, see the [OpenClaw guide](../tools/openclaw/README.md),
[Hermes guide](../tools/hermes/README.md) and [network/security guide](networking.md).
Restrict the dedicated OpenClaw agent and review Hermes tool policy. Device-side
interactive approval is not supported. Assistant profiles select connections.

## Connect, save and discard

1. Connect over USB or Bluetooth and let the page read the device. Bluetooth
   devices appear as **AI Passport XXXX**; enable Bluetooth in device Settings.
   If a browser cannot discover a device that appears on USB, restart Passport
   and rescan before assuming the hardware lacks Bluetooth.
2. Scan Wi-Fi or enter its exact name/password. The **Passport**, not the browser,
   scans nearby networks. Results arrive progressively on current firmware.
3. Edit Wi-Fi, assistant profiles/routes, screen language or volume, then choose
   **Save configuration**. Fields with no changes need no repeated pairing.
4. Wait for acknowledgements/readback. Failed saves and reconnects keep editable
   drafts. **Discard** restores the last read/acknowledged state, not a completed
   write or a consumed one-time pairing code.

Saving multiple settings is **not atomic**: earlier successful changes remain if
a later change fails. Wi-Fi is tested before a new network is saved, so the target
must be reachable. A scan with no results still allows manual entry. Saving and
testing run on Passport and do not switch the computer/phone to another Wi-Fi.
The device connection check validates authentication/session access and realtime
configuration, not model answers, audio quality or long-call reliability.

## Speech filtering

Ask and Call use lightweight on-device WebRTC voice activity detection, not just
volume thresholds. Ask submits after confirmed speech followed by about two
seconds of silence. Manually submitting a recording without detected speech
discards that recording without sending it to the agent; completed chat history
is preserved. Cancel and re-record controls are unchanged.

Call sends silence instead of unconfirmed microphone noise and keeps up to
171 ms of compressed pre-roll to retain the beginning of speech. Mute and
playback protection clear pre-roll. Voice interruption also requires speech
confirmation during its echo-checking probe. Candidate v0.7.0 pauses the playback
cursor without consuming queued reply audio, waits 100 ms after the in-flight
write finishes for speaker/DMA echo to settle, and confirms continued speech
before cancelling output. An unconfirmed probe resumes at the same cursor within
350 ms; it does not discard reply syllables. Button interruption remains immediate.
Finer echo matching reduces probes but is not sufficient by itself: a device
recording confirmed false voice cancellation in the previous candidate. Tests
cover unmatched echo, genuine speech, PCM/IMA pause/resume and real VAD confirmation.
Diagnostics expose
separate voice/button interruption counts without recording microphone content.
The OpenClaw Passport plugin disables upstream automatic audio interruption while
retaining server turn detection. Only a device-confirmed voice/button interruption
cancels output; another upstream detector must not compete with it.
The Hermes adapter uses the same device-owned interruption policy. An OpenClaw
reply-phase failure is shown immediately as retryable failure, not a fictional
30-second reconnection; startup/transport recovery retains its existing bound.
Call layout changes invalidate the full screen under the LVGL lock; the display
task performs rendering. The application task never forces a synchronous refresh,
which would put the renderer on its smaller stack and can restart the device.
Buffer overflow drops the oldest pre-roll rather than replaying
it out of order, and is counted in audio diagnostics.

This is not speaker identification or a guarantee against false activation.
Television, other speakers, music or strong speech-like noise can still pass;
very short, quiet or distant speech can be missed. Current host fixtures cover
speech at several levels, silence, DC, clicks, white noise, low-level filtered
noise and mains hum. Loud filtered noise can still trigger detection. Physical
microphone, noisy-room, soft-speech and interruption acceptance remain necessary.

The detector uses about 948 bytes of heap only during capture. Call adds a
2,144-byte compressed pre-roll but removes the previous 1,536-byte PCM onset
buffer; Ask and Call do not allocate separate simultaneous detectors. Fixture
audio is not included in the firmware. [Source and notices](../assets/README.md#speech-detection-fixture).

## Where settings live

Passport is authoritative for up to **eight networks**, **four assistant profiles**,
language and volume. Credentials are stored on the device; model/provider keys
stay on OpenClaw. The web studio has no application backend: configuration goes
straight over USB/Bluetooth. Secret drafts stay in page memory, not persistent
browser storage; refreshing/closing discards them. Static hosting may retain
ordinary IP/time access logs, not configuration the page never uploads.

Clean firmware contains no personal gateway, proxy or provider key. Examples use
fictional networks and reserved `.example` addresses. Do not publish configuration
exports, device memory dumps, pairing codes or unsanitized logs.

Legacy native-client source remains for developers only. Its historical desktop
Wi-Fi cache is an unencrypted, owner-readable `wifi.json` under
`~/Library/Application Support/AI Passport OpenClaw/`; treat that file/backups as
sensitive. It is not part of the web workflow or browser storage.

## Firmware installation and updates

Saving configuration never flashes firmware. **Keep settings** is checked by
default for a compatible application update over USB, preserving device data.
For a first install/full replacement, uncheck it and confirm **Install firmware**;
this may overwrite configuration. Neither path performs a full-chip erase.
The website serves verified release images on its own origin; no local file
chooser or runtime GitHub download redirect is needed.

Application-only firmware belongs at `0x10000`, within its compatible partition;
merged firmware belongs at `0x0`. Do not interchange them. There is no automatic
stock-firmware rollback. See the [firmware layout/data policy](development/engineering/firmware-layout.md).

## iPhone Wi-Fi and hotspots

Open the site in Bluefy, connect by Bluetooth, then scan from the Wi-Fi card.
No iOS Wi-Fi password database is read: enter a new password yourself. Reusing a
saved network can use its device-stored password. A chain-link icon marks a
best-effort detected Apple hotspot, not ownership or a guarantee of identification.

### Use a phone hotspot without the iOS App

1. Open iPhone **Settings → Personal Hotspot**, enable **Allow Others to Join**
   and **Maximize Compatibility**, and check its name/password. Keep this page
   open if Passport's scan cannot find the hotspot.
2. Use desktop USB or Bluefy Bluetooth to select/enter that hotspot in the web
   studio and save. It must be nearby and enabled for the first connection test.
   There is no reliable supported link from the webpage directly to this Settings page.
3. Later, enable the same hotspot and select it in Passport **Settings → Wi-Fi**.
   Use the same menu to return to home/office Wi-Fi. No assistant re-pairing is needed.

The browser can close after configuration. Keep the hotspot on while it provides
internet. A hotspot alone does not expose a home gateway; see [connectivity](networking.md).

## Bluetooth behavior and security

Bluetooth is enabled by default in standby. At boot Wi-Fi gets up to ten seconds
before BLE starts; manually enabling it skips that wait. Settings toggles it in
place. The dim status icon means enabled; brighter/thicker blue strokes mean a
client is connected; disabling Bluetooth hides it. This is not proof of a saved
configuration or authenticated gateway connection.

The firmware retains encrypted client bonds. A new client may take over after
an encrypted configuration handshake; current work finishes first and replies
stay with their originating session. The previous page keeps its draft and does
not automatically take the device back. Just Works encryption does not authenticate
ownership or defeat an active man-in-the-middle. Configure in a trusted environment.

Recording, Call and gateway work temporarily release BLE memory/disconnect setup
clients. It returns after cleanup. Wi-Fi saves and scans do not intentionally
disable configuration Bluetooth. Older firmware may need a USB update before
secure configuration works. Browser/OS pairing prompts can still vary.

## NFC and personal hotspot

The passive tag can trigger an iPhone automation independently of firmware and
device power. It does not authenticate the owner, configure Wi-Fi or wake the
device processor. The tested tag is NTAG213 with 144 bytes of user storage.

1. First save/test the hotspot using the web studio.
2. Open **Shortcuts → Automation → NFC**, scan the tag and choose **Run Immediately**
   where available. Add **Set Personal Hotspot → On**, rather than Toggle.
3. Test with the phone awake. Check automation permissions/confirmation if needed.

If the stock URL still produces a website notification, back it up using an NFC
writer and **replace** the URL record with a plain-text record (`AI Passport`).
Do not append text behind the URL; do not touch UID, lock bits or configuration.
This worked on the maintainer's iPhone. USB flashing cannot rewrite the tag.
Never write passwords or pairing credentials to it. Behavior may vary by iOS version.
