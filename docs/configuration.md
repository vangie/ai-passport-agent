<p align="right"><a href="configuration.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Configuration and everyday use

Start with the [product overview](../README.md). This guide covers configuration
behavior, phone hotspots, NFC, local app setup and compatibility details.
For gateway installation, use the [plugin guide](../tools/openclaw/README.md);
for LAN/public ingress and proxies, use [Gateway connectivity](networking.md).

## Where settings live

The Passport is the source of truth for up to eight Wi-Fi networks, four
assistant connection profiles, language and speaker volume. Apps read that
state and edit drafts. The Mac also remembers useful form values and Wi-Fi
passwords locally; it is not a second authoritative device configuration.

**The Mac password cache is not Keychain storage.** It is a JSON file at
`~/Library/Application Support/AI Passport OpenClaw/wifi.json`, written with
owner-only permissions. It is not encrypted by the app. Treat it and system
backups as sensitive. Use a network row's Forget action to remove a local
remembered password; forgetting a device-saved network is a separate operation.
Do not attach this file, configuration exports or NVS dumps to issues.

Gateway URLs, device credentials and Wi-Fi settings are stored in device NVS.
Model API keys and provider configuration stay on OpenClaw. Ask uses the fixed
`passport` agent and a per-device session; assistant profiles select connections,
not arbitrary Ask agent IDs. Set tool/model permissions on that dedicated agent.
Possession of a paired device grants its authority; revoke lost devices.

Clean firmware has no personal tunnel endpoint, proxy node or provider key.
Public NTP servers, local loopback addresses and the setup portal `192.168.4.1`
are infrastructure defaults, not private assistant endpoints. Documentation and
screenshots use reserved `.example`/`.invalid` names and fictional networks.

## Save, discard and update

- Edits remain drafts until **Save Configuration**, including Mac language and
  volume changes. Switching assistant profiles preserves their individual edits.
- Save applies edited settings sequentially, with acknowledgement and device
  readback; Wi-Fi goes last. **This is not atomic**: if a later step fails,
  earlier steps remain saved, and unsaved edits remain available for retry.
- Success, failure and reconnection do not clear the form. **Discard Changes**
  restores the latest read or acknowledged state; it does not undo a completed
  write or reinstate a consumed one-time pairing code.
- Wi-Fi and assistant settings can be changed independently. Only configure
  what needs changing; do not repeat pairing just to change networks.
- **Device Maintenance** is a separate, explicit USB firmware action. Saving
  configuration never flashes. Matching bundled firmware is not written again.
- Compatible Mac application updates write at `0x10000`, below `0x350000`.
  Initial merged installation at `0x0` can reset NVS. This application reserves
  `cardid` and `audio`; inspect the partition table when migrating. There is no
  automatic stock-firmware rollback. See [firmware layout and data policy](development/engineering/firmware-layout.md).

### What a connection check means

The Mac's **Test Address** runs from the Mac. **Check Current Agent** runs over
the Passport's Wi-Fi, route and paired credentials: it checks gateway
authentication, Ask-session access and realtime-provider configuration without
recording or sending a prompt. It is cancellable, but does not prove model
responses, microphone quality or reliable long calls. A Wi-Fi check is performed
on the Passport and does not move the Mac to another network.

Connection-changing controls are disabled during a call, recording or device
check. Older firmware keeps basic configuration available but needs an update
for device checks and volume synchronization. These are advertised by the
`device-control` capability; stale volume writes and busy-device changes are
rejected, and writes are acknowledged only after persistence.

## iPhone Wi-Fi and hotspots

1. Turn on Bluetooth in Passport **Settings → Bluetooth** using the middle key.
   Open the iPhone app and select `AI Passport XXXX`; the four hexadecimal
   characters identify the device and remain stable across restarts.
2. Tap **Search**. The **Passport**, not the iPhone, scans nearby 2.4 GHz networks.
   Select a result or type its exact SSID. The app does not request location
   access, read the phone's current SSID or retrieve saved iOS Wi-Fi passwords.
3. Enter a password and save. Selecting a saved network with an empty password
   reuses the device's stored password. Selecting the same network retains a
   typed password; choosing a different name clears it to avoid credential mixups.
4. Save sends the draft over Bluetooth without changing the phone's Wi-Fi.
   Scan cancellation, save errors and reconnects retain editable inputs.

Firmware advertising `scan-live` returns discoveries channel by channel, up to
32 network names. Existing result rows stay in place while scanning; selection
is possible before the scan ends, but saving waits for completion. Older
firmware returns results only after its full scan. Scan failure or an empty
list does not prevent manual entry.

An observed Apple vendor information element marks recognized iPhone hotspots
with a chain-link icon. It is a best-effort hint, not identification of the
owner or a security guarantee. Unrecognized hotspots remain normal Wi-Fi rows;
older firmware without `NET_KIND` metadata cannot supply this hint.

### Connect to this iPhone's hotspot

- Enable **Settings → Personal Hotspot → Allow Others to Join** and
  **Maximize Compatibility** for 2.4 GHz operation.
- The hotspot SSID is the **iPhone's name**, not a separate editable hotspot
  name. Scan for it, or copy it from **Settings → General → About → Name**.
  Renaming the phone also changes the SSID. This app does not hold Apple's
  restricted entitlement for reading the user-assigned device name.
- **If your own hotspot is missing, keep the Personal Hotspot settings page
  open and the screen awake while scanning/connecting.** Some iPhones stop
  advertising to non-Apple devices after leaving that page. Background Bluetooth
  responses remain supported by the app.
- Enter the hotspot password manually once. iOS does not allow this app to
  read that password or silently enable the hotspot. A configured network can
  reuse its device-saved password later.
- There is no supported direct Personal Hotspot settings link in this app.
  Tested private settings URLs did not reliably open the required page.

Saved networks can also be switched on the Passport: Settings → Wi-Fi, choose
a network and connect. Add networks or correct passwords using either app.
Device errors distinguish network not found, authentication failure, incompatible
security and IP-address timeout. A rejected setting does not intentionally drop
Bluetooth or discard your draft.

## Bluetooth behavior and security

Bluetooth is on by default in standby. After boot, saved Wi-Fi gets up to ten
seconds to connect before Bluetooth starts; explicitly enabling it skips this
wait. There is no two-minute discovery timeout, and leaving the setup screen
does not turn it off. The Settings row shows its state and toggles it in place.

Recording, Call and gateway operations temporarily release BLE memory and
disconnect the configuration app. BLE returns after audio/gateway cleanup.
Idle gateway connections may give memory back to Bluetooth, requiring a new
gateway connection for the next Ask. Configuration saves and network switching
do not deliberately turn off an existing configuration connection. Manually
disabling BLE keeps it off until enabled again or the device restarts.

The phone requires firmware advertising `ble-secure`; update older firmware over
USB before sending passwords. Writes require an encrypted link and replies use
MTU-sized acknowledged indications. NVS retains up to three configuration-client
bonds, so ordinary reconnects/reboots do not require another system pairing.
Forgetting the device or clearing bonds does.

**Just Works encryption does not authenticate device ownership and does not
protect against an active man-in-the-middle.** Configure in a trusted environment
and disable configuration Bluetooth when unnecessary in an untrusted location.

## NFC and personal hotspot

The passive NFC tag can run an iPhone automation independently of the Passport's
power and firmware. It does not wake the ESP32, configure Wi-Fi or authenticate
a device. The tested hardware uses an NTAG213 with 144 bytes of user storage.

### Set up the automation

1. Configure the hotspot SSID/password on the Passport over Bluetooth first.
2. Open **Shortcuts → Automation → NFC**, scan the Passport and name the trigger.
3. Select **Run Immediately**, where available. Add **Set Personal Hotspot → On**,
   optionally followed by **Open App → AI Passport**. Use the system Open App
   action; the app does not currently register custom App Shortcuts.
4. Try the tag with the phone awake. If it does nothing, check automation
   enablement, confirmation/notification settings and action permissions. Some
   iOS combinations still require tapping a notification or unlocking the phone.

Apple's [NFC trigger guide](https://support.apple.com/guide/shortcuts/apde31e9638b/ios)
describes tag identification, and the [automation guide](https://support.apple.com/guide/shortcuts/enable-or-disable-a-personal-automation-apd602971e63/ios)
explains automatic execution. Behavior remains subject to iOS and carrier rules.

### Remove the stock website prompt

A URL NDEF record can trigger iOS's website banner even when an NFC automation
also exists. On the maintainer's device, **replacing the URL with a single
Text record containing `AI Passport`** removed the banner; the hotspot
automation then worked in the tested setup.

- Use a compatible NFC writer, such as an iPhone NFC-writing app or a Proxmark
  command-line tool. No NFC-writing function is included in the Mac app.
- Read and privately back up the existing NDEF first: factory URLs may contain
  device-specific secrets. Do not share the backup, UID or original full URL.
- Replace the URL record, rather than appending another record. Write only the
  NDEF user content; preserve the UID, capability container, lock-control data,
  lock bits and configuration. Do not permanently lock the tag.
- Read the result back and verify there is no URL record. Because the UID is
  unchanged, an existing NFC automation should keep recognizing the tag.
- If the tag is locked or unsupported, stop rather than forcing a write.
  USB firmware updates cannot rewrite this passive tag.

This is an optional, tested tip, not a promise across every iOS/tag combination.
Opening AI Passport does not automatically pair BLE, select a network or save a
configuration. NFC content is publicly readable and cloneable: never store
Wi-Fi passwords, setup codes, gateway tokens or API keys on it.

## Run the iPhone app

The app is not distributed through TestFlight or the App Store. Download the
[Release](https://github.com/vangie/ai-passport-openclaw/releases) iPhone source kit
or use this checkout; both include `ios/` and `shared/`. With full Xcode and
XcodeGen installed:

```sh
cd ios
xcodegen generate
open Passport.xcodeproj
```

Choose your signing team and a connected physical iPhone, then run the `Passport`
scheme. Follow Xcode's trust/Developer Mode instructions on the phone as needed.
Personal Team signing is for local development and can expire; never commit
certificates or signing-account configuration. Simulator UI previews cannot
connect to real BLE devices or PassportSim's virtual radio.

```sh
xcodebuild -project ios/Passport.xcodeproj -scheme Passport \
  -destination 'platform=iOS Simulator,name=iPhone 17' test CODE_SIGNING_ALLOWED=NO
```

Run that test command from the repository root after generating the project.
Build the Mac app with `make sim-mac`, optionally setting `DEVELOPMENT_TEAM` in
your local environment. Host checks require the tools listed in the
[build guide](development/engineering/build-and-test.md), including mbedTLS and
cJSON source; firmware uses ESP-IDF 5.5.3.

## Standby and compatibility

- After 60 seconds of eligible idle time, the display sleeps; the device does
  **not** shut down. A first key press only wakes it. The next press performs
  the displayed action. Calls, recording, settings, configuration work and an
  enumerated computer USB connection keep it awake.
- A USB host is not a reliable charging detector. Wall charging can still let
  the screen sleep without stopping charging. Battery percentage is not proof
  of charger state; battery-only runtime needs physical measurement.
- Product names use `Passport`; iOS Bundle ID is `ai.mirasim.passport`.
  Device settings use NVS `passport`, identity uses `passport_id`, and serialized
  settings start with `PSP1`. Unreleased prototype identities are not migrated.
- The setup AP is `Passport-Setup` / `passport-setup`. BLE service identifiers
  remain unchanged; old prototype apps may coexist until removed manually.
- Ask sessions use `agent:passport:passport-<device-id>`. Recent on-screen chat
  history is in RAM, not a durable transcript or the gateway's context store.

For microphone, network, speech quality and long-call acceptance, test real
hardware. A successful build or simulator screenshot does not establish them.
