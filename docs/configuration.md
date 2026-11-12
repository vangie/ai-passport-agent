<p align="right"><a href="configuration.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Configuration and everyday use

Use the [web studio](https://ai-passport-openclaw.codelife.me/). Desktop Chrome/Edge
supports USB configuration and firmware installation; compatible Web Bluetooth
browsers support wireless settings. On iPhone, [get Bluefy from the App Store](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
and open the configuration site inside it, not Safari/Chrome. The site's Bluetooth compatibility hint links directly to this listing.
Firmware flashing still needs a computer and USB. See [browser compatibility](web.md).

## Let OpenClaw prepare the gateway

Choose **Copy Prompt** in the OpenClaw section and send it to an agent able to
operate on the gateway host. That host need not be your setup computer. The page
does not install server software itself. You need:

- A running OpenClaw with a working model and host access for the setup agent.
- Passport, a computer/data cable for initial flashing, and reachable 2.4 GHz
  Wi-Fi or a compatible phone hotspot.
- Authorized tunnel resources and usable DNS/TLS for remote access. The prompt
  reuses existing ingress or asks for missing login, permissions or resources;
  it does not provide accounts, free services or permission for new charges.
- A realtime voice provider for Call. Ask can be configured independently.

The agent checks/installs the compatible project plugin, prepares the dedicated
`passport` agent, transcription/model settings and persistent public WSS ingress.
It backs up affected configuration, retains device authentication and loopback
binding, and restarts only when necessary. DNS/TLS/WebSocket checks precede the
limited pairing code; these are readiness checks, not microphone acceptance.
Paste the returned address/code into the page and save alongside Wi-Fi settings.
Expired or consumed codes need regeneration. Never paste administrator tokens
or model keys into device configuration.

For manual or LAN-only setup, see the [plugin guide](../tools/openclaw/README.md)
and [network/security guide](networking.md). Restrict tools on the dedicated agent;
assistant profiles select connections, not arbitrary Ask agent IDs.

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
