<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

<p align="center"><img src="assets/logo/logo.png" alt="AI Passport OpenClaw app icon" width="104" /></p>

<h1 align="center">AI Passport OpenClaw</h1>

<p align="center"><strong>A lightweight, wearable voice interface for OpenClaw</strong></p>
<p align="center">A voice terminal for FoloToy AI Passport.<br />Voice Q&amp;A and realtime calls, connected directly to your own assistant.</p>

<p align="center">
  <a href="https://github.com/vangie/ai-passport-openclaw/releases">Download</a> ·
  <a href="#get-started">Get started</a> ·
  <a href="#tap-for-a-hotspot">NFC hotspot tip</a> ·
  <a href="docs/configuration.md">Configuration guide</a>
</p>

![Passport firmware: Home, Ask conversation and live Call](assets/images/screenshots/firmware.png)

An independent community project built on [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport).
Not official FoloToy firmware or an OpenClaw distribution. This firmware replaces
the stock application; it does not promise to retain its original features.

## Voice Q&A and realtime calls

| | Ask | Call |
| --- | --- | --- |
| Best for | A question, a task, an answer to read | A back-and-forth voice conversation |
| Interaction | Press to record; press again to send, or pause after speaking | Speak naturally once connected; pause for a reply |
| Feedback | Recording waveform, your transcript and streamed reply text | Microphone waveform, spoken replies and scrolling subtitles |
| Keep going | Continue adds a turn; page through recent messages | Mute, interrupt or end using the on-screen key labels |

The home screen has just **Settings / Call / Ask**. Button captions sit beside
the physical keys and change with the current screen. Chinese and English UI,
battery and connectivity indicators, and adjustable speaker volume are included.

Ask uses a dedicated OpenClaw `passport` agent. Call uses a realtime voice
provider configured on the gateway. Model availability, tools and response speed
depend on your gateway, provider and network, not only the device.

## Configure on the screen you have

**The macOS App and iOS App configure Passport; neither relays voice. The iOS App is optional.**

Both apps automatically follow the system language (English or Simplified Chinese),
including setup prompts and permission messages. Passport's screen language is set separately.

| Tool | When to use it | Must it stay running? |
| --- | --- | --- |
| macOS App | Configure Wi-Fi, assistants, language and volume over USB; check connectivity, update firmware, or prepare an iPhone hotspot before leaving | No; disconnect USB after setup |
| iOS App (optional) | Add or change Wi-Fi, personal hotspots and assistant settings over Bluetooth when no computer is available | No app or Bluetooth connection is needed afterward; keep the hotspot on if it provides internet |

For example, configure home Wi-Fi and your iPhone hotspot from the macOS App before
leaving. Later, enable the hotspot and select it in Passport **Settings → Wi-Fi**.
**Installing the iOS App is not required.** The firmware tests a new network
before saving it, so the phone must be nearby with its hotspot enabled during
initial setup; an unavailable hotspot cannot simply be entered and saved.
See [prepare a hotspot without the iOS App](docs/configuration.md#use-a-phone-hotspot-without-the-ios-app).

### macOS App: configuration and flashing, on one page

![macOS App with demo Wi-Fi, assistant pairing and device preferences](assets/images/screenshots/macos.png)

- **One page, one save.** Edit Wi-Fi, assistant profiles, language and volume,
  then save together. Discard restores the last device state; failed saves do
  not wipe your inputs.
- **USB you can see.** Connection, device Wi-Fi, assistant and battery readings
  appear in the footer. Check the gateway from the device's actual network.
- **Firmware when needed.** Device Maintenance updates compatible application
  firmware over USB. Saving settings is separate and never reflashes it.
- **Less retyping.** Remember Wi-Fi passwords locally, reuse them, or forget
  individual entries. See the [local storage notes](docs/configuration.md#where-settings-live).

### iOS App (optional): configure anywhere over Bluetooth

<p align="center"><img src="assets/images/screenshots/ios.png" alt="iOS App with demo device, Wi-Fi, assistant and language settings" width="320" /></p>

- Connect over **Bluetooth**, without switching the phone to a setup hotspot.
- Ask the **Passport to scan** nearby 2.4 GHz networks; results appear as they
  arrive on compatible firmware. Recognized iPhone hotspots get a link icon.
- Reuse saved networks, add a hotspot, scan pairing information and save changes
  to the device. Close the app and disconnect Bluetooth afterward. If Passport
  uses this phone's hotspot for internet, the hotspot must remain on.

All screenshots above show the actual firmware renderer or native apps with
**demonstration data**: fictional networks, assistant names and `.example`
endpoints. They are UI previews, not photos or proof of hardware connectivity.

## Yours, not a phone relay

```text
macOS App ── USB ─────┐
                   ├── configure ── AI Passport
iOS App ── BLE ──┘                    │
                                       Wi-Fi / personal hotspot
                                        │
                              Your OpenClaw gateway
                              + AI Passport plugin
                                        │
                                  Models and tools
```

The Passport sends conversations **directly to your gateway**. The macOS App and
iOS App configure it; they do not relay audio. Use your own LAN or an existing
authenticated public WSS endpoint. There is no shared hosted gateway bundled
with this project.

With a personal hotspot, the phone provides internet access only; **audio does
not pass through the configuration app**. Keep the phone nearby and its hotspot
available while using that connection.

Up to **8 Wi-Fi networks** and **4 assistant connection profiles** live on the
device. Switch saved networks and assistants from Settings without reflashing.
Profiles choose gateway connections; Ask currently keeps the `passport` agent ID.

## Reach your home OpenClaw while away

An internal gateway is directly reachable on the same LAN. From a phone hotspot
or another network, it needs an ingress reachable from that network. Configuring
the hotspot alone does not expose the gateway at home.

The [connectivity and security guide](docs/networking.md) covers Cloudflare
Tunnel, SakuraFrp, TLS, authentication and limited pairing. Keep the **gateway
bound to loopback, use public WSS, retain device authentication, and never put
the administrator token on the device**. A tunnel provides reachability, not
automatic access control. Limit the dedicated `passport` agent's tools and
revoke authorization if the device is lost.

## Get started

You need a FoloToy AI Passport, a data-capable USB cable, a 2.4 GHz Wi-Fi network
or compatible phone hotspot, and a running OpenClaw gateway with a working model.
Initial flashing needs a computer. The macOS App requires macOS 14+; the optional
iOS App requires local signing. **OpenClaw 2026.10.5** is the tested baseline.

**You do not have to install the plugin, create the `passport` agent or set up
public ingress manually first.** The app's prompt asks an agent with access to
the gateway host to check and complete those steps. Public ingress still needs
authorized tunnel resources, a usable domain/certificate and quota; existing
resources are reused, and missing access requires user action. Call also needs
a realtime voice service; Ask can be used first. Model API keys stay on the gateway.

1. **Install firmware and connect.** Download firmware and the macOS App from
   [Releases](https://github.com/vangie/ai-passport-openclaw/releases) and connect by
   USB. Follow the Release guide to install a blank device's merged image;
   compatible firmware can be updated through **Device Maintenance** in the app.
   Read the [flashing and data policy](docs/development/engineering/firmware-layout.md#flashing-and-stored-data)
   and save any settings you need to keep; a merged flash can reset configuration.
2. **Let OpenClaw prepare access.** Choose **Copy Prompt** in the app's OpenClaw
   section and send it to an agent that can operate on the gateway host. It prepares
   the plugin, dedicated agent and network ingress, verifies readiness and returns
   a QR code, address and limited setup code. The gateway can run on another computer.
3. **Choose the device's network.** Connect with the macOS App over USB, or the
   optional iOS App after enabling **Settings → Bluetooth** on Passport and choosing
   `AI Passport XXXX`. Scan for a Wi-Fi network or enter its name, then provide its password. The macOS App
   can prepare a phone hotspot too; it must be enabled and reachable on first save.
   On iPhone, enable **Allow Others to Join** and **Maximize Compatibility**; if
   discovery fails, keep iOS Personal Hotspot settings open, then scan again.
4. **Import pairing details and save.** Paste the address and setup code, or scan
   the QR code in the iOS App, then choose **Save Configuration**. Once the Wi-Fi
   icon appears, try Ask; the macOS App can also run a device connection check.
   Correct and retry failures without reflashing; request a new code if it expires.
   Close the app afterward; keep the hotspot on if it supplies internet.
5. **Start Q&A or a call.** Choose **Ask** to record, then **Send**, or pause to
   submit automatically. Page through replies and continue with a follow-up.
   Choose **Call**, wait for **Listening**, and follow the labels to mute,
   interrupt or end. Switch saved networks in **Settings → Wi-Fi**, or add one
   in either app.

See [agent-assisted setup](docs/configuration.md#let-openclaw-prepare-the-gateway)
for details and boundaries. LAN-only use needs no public ingress; follow the
[plugin guide](tools/openclaw/README.md) and [connectivity guide](docs/networking.md)
for manual setup. Saving configuration and flashing firmware are separate actions.

### What's in a Release?

| Artifact | Use |
| --- | --- |
| `ai-passport-openclaw-macos-universal-adhoc.zip` | macOS 14+, Apple Silicon and Intel; includes matching firmware |
| `ai-passport-openclaw-firmware.tar.gz` | Application/merged images, manifest, bootloader, partition table and flash parameters |
| `ai-passport-openclaw-<plugin-version>.tgz` | Installable gateway plugin |
| `ai-passport-openclaw-ios-source.zip` | iOS App source kit with shared code and signing instructions; build with your own Xcode team |
| `ai-passport-openclaw-ios-simulator.zip` | macOS App simulator preview only, not installable on iPhone; no real BLE |

Check the release's `SHA256SUMS.txt`. The macOS App is currently **ad-hoc signed,
not Developer ID signed or notarized**; macOS may require local security approval.
USB flashing requires Python 3 and `esptool` (for example,
`pipx install esptool==4.11.0`). The app does not silently install system tools.
There is **no signed iOS App download**, App Store or TestFlight release.
Use the source kit and [installation instructions](docs/release/ios-install.md)
to build/sign on your own iPhone, or the Simulator ZIP for a Mac-only preview.

## Tap for a hotspot

The Passport's passive NFC tag can trigger an **iPhone Shortcuts automation**
without installing this project's iOS App:

1. Configure your iPhone hotspot using the macOS App over USB or the iOS App
   over Bluetooth first. Enable
   **Allow Others to Join** and **Maximize Compatibility** in iOS Settings.
2. In **Shortcuts → Automation → NFC**, scan the Passport and select
   **Run Immediately**, if available.
3. Add **Set Personal Hotspot → On**. Optionally add **Open App → AI Passport**.
   Prefer On over Toggle so a second tap does not turn the hotspot off.

**Still getting the original website banner?** The stock NFC tag may contain a
URL record. Back up its contents with an NFC writer, then **replace that URL
with a plain Text record**, such as `AI Passport`; do not merely append text.
This removed the website prompt and worked with the hotspot automation on the
maintainer's iPhone. Do not overwrite the tag UID, lock bits or configuration.
The NFC tag is independent of firmware; USB flashing does not rewrite it.

This is an optional iOS automation, not device authentication or a guarantee of
silent execution on every iOS version. It opens the app only if requested;
it does not automatically pair Bluetooth or write settings. Never put passwords
or pairing credentials on the tag. See [NFC details and troubleshooting](docs/configuration.md#nfc-and-personal-hotspot).

## Privacy and sensible limits

- **No personal endpoints or secrets in clean firmware.** Gateway URLs, Wi-Fi
  and device credentials come from your configuration. Model keys remain on
  OpenClaw. Screenshots use fictional data, not redacted private exports.
- **Pair devices, not admin tokens.** A paired Passport carries its configured
  authority. Revoke lost devices in OpenClaw and choose tool permissions accordingly.
- **Bluetooth has a boundary.** Configuration uses encrypted Just Works pairing,
  not proof of ownership or MITM protection. Turn it off in device Settings when
  configuration is not needed in an untrusted environment.
- **Hardware stays modest.** ESP32-C3, 8 MB Flash, no PSRAM, a 240 × 320 display
  and 2.4 GHz Wi-Fi. BLE is temporarily released during audio/gateway work, then
  restored. UI previews cannot validate radio coexistence or audio quality.

## Build, extend, contribute

| Component | Source / guide |
| --- | --- |
| Firmware and board support | `app/`, `main/`, `components/bsp/` · [Build and test](docs/development/engineering/build-and-test.md) |
| macOS App and iOS App | `mac/`, `ios/`, `shared/` · [Configuration guide](docs/configuration.md) |
| Upgradeable OpenClaw plugin | [`tools/openclaw/`](tools/openclaw/README.md) |
| Networking | [LAN, tunnels, proxies and troubleshooting](docs/networking.md) |
| Project documentation | [Documentation index](docs/README.md) · [Contributing](.github/CONTRIBUTING.md) |

With the [required tools](docs/development/engineering/environment-setup.md) installed:

```sh
make preview-ui       # Render the actual LVGL screens locally
make sim-passport     # PassportSim, with its dependencies installed
make sim-mac          # Build and launch the macOS App
make sim-iphone       # Build and launch in the iPhone simulator; no real BLE
make plugin-test
make plugin-package
./tools/validate.sh   # Full gate; activate ESP-IDF 5.5.3 first
```

CI checks public sources, secrets, host tests, macOS App builds, ESP-IDF firmware,
iOS App device/Simulator builds and UI tests, and the plugin on Node 22/24/26.
Version tags publish only after the required jobs
pass; manual Release runs keep artifacts without publishing a GitHub Release.
See [CI and releases](docs/development/ci/CI-build-and-release.md).

## License and credits

[MIT](LICENSE), retaining FoloToy's attribution. Hardware/BSP and development
documentation derive from the upstream project; fonts and third-party assets
retain their notices. See [asset sources](assets/README.md). Report sensitive
issues privately using the [security policy](.github/SECURITY.md).
