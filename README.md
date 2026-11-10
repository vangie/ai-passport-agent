<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# AI Passport OpenClaw

A wearable OpenClaw voice terminal for the **FoloToy AI Passport**: configure it
from a Mac, then use Wi-Fi without keeping a phone connected. Built for the
ESP32-C3, 8 MB Flash, 240 × 320 display, microphone, speaker and three buttons.

This is an independent community project derived from
[FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), not official
FoloToy firmware or an OpenClaw distribution.

## What is included

| Component | Source | Purpose |
| --- | --- | --- |
| Firmware | `app/`, `main/`, `components/bsp/` | Chinese/English UI; Call and Ask modes; device-held configuration; Wi-Fi, audio and transport |
| Mac app | `mac/`, `shared/` | USB status, Wi-Fi and agent configuration, pairing, firmware checks and application-only flashing |
| iPhone app | `ios/`, `shared/` | Bluetooth configuration, progressive Wi-Fi discovery, hotspot hints, pairing and device preferences |
| OpenClaw plugin | [`tools/openclaw/`](tools/openclaw/README.md) | Upgradeable native Talk integration and authenticated `/passport/v1` WebSocket transport; no edits to installed OpenClaw core |
| Connectivity guide | [Networking](docs/networking.md) | LAN, Cloudflare Tunnel, SakuraFrp, TLS, authentication and troubleshooting |
| Development tools | `tools/`, `tests/`, `sim/` | Host tests, firmware verification, packaging and simulator launchers |

Call uses a realtime voice provider through OpenClaw; Ask submits recorded
speech to the gateway's `passport` agent. Create that dedicated agent in OpenClaw
and configure its model/tool permissions before using Ask. Multiple assistant
profiles select gateway connections; they do not change Ask's agent ID yet.
Model credentials stay on the gateway.
Configuration is stored on the Passport; the Mac also remembers useful local
form values and saved Wi-Fi passwords using platform storage. Do not publish
configuration exports, pairing codes, logs or device credentials.

### Endpoints and private configuration

No personal gateway, tunnel hostname, proxy node or model API key is bundled in
the source or clean firmware. Gateway URLs come from pairing or configuration;
the Passport stores them and its device credentials in NVS. Configuration apps
read those profiles from the device. Model provider addresses and API keys remain
in the gateway's OpenClaw configuration, not in the Passport firmware.

The fixed networking addresses are infrastructure defaults: public NTP servers
(`ntp.aliyun.com`, `pool.ntp.org`, `time.cloudflare.com`), loopback connections to
the local OpenClaw gateway, and the setup portal at `192.168.4.1`. They are not
personal agent endpoints. Examples use reserved `.example`/`.invalid` names.
CI runs the public-source audit and secret scanner; device dumps, private config,
signing files, local build products and diagnostic logs must stay out of Git.

## Start with a Release

Download artifacts from [Releases](https://github.com/vangie/ai-passport-openclaw/releases):

- `ai-passport-openclaw-macos-universal-adhoc.zip`: macOS 14+, Apple Silicon and
  Intel, with the matching firmware bundled. Currently **ad-hoc signed, not
  Developer ID signed or notarized**; macOS may require an explicit local
  security approval. No Apple signing credentials are distributed.
- `ai-passport-openclaw-firmware.tar.gz`: merged/application firmware, manifest,
  bootloader, partition table and flash parameters. Check `SHA256SUMS.txt`.
- `ai-passport-openclaw-<plugin-version>.tgz`: install on the OpenClaw host,
  following the [plugin instructions](tools/openclaw/README.md).

1. Install the plugin on your gateway. **OpenClaw 2026.10.5 is the tested
   baseline**, not a promise of compatibility with every future SDK release.
   Configure model credentials there, including a realtime-capable provider for Call.
2. Make the gateway reachable using an existing LAN or public WSS address.
   Read [Networking](docs/networking.md) before exposing a local service.
3. Open the Mac app and connect the Passport with a data-capable USB cable.
   Firmware flashing requires Python 3 and `esptool` on the Mac; one option is
   `pipx install esptool==4.11.0`. The app does not install system tools silently.
4. Configure Wi-Fi, choose an agent and pair using OpenClaw's limited setup code.
   The app's copy-prompt flow helps obtain pairing information from your agent.
5. Use the bottom **Save Configuration** button for Wi-Fi, all edited agents,
   screen language and volume. It lights up when there are valid changes;
   **Discard Changes** restores the latest device settings without writing.
   Saving configuration never flashes firmware. Use Device Maintenance for an
   explicit firmware update. Normal application-only updates target `0x10000` and
   preserve settings **only with a compatible partition layout**. A blank device
   needs the merged image from `0x0`; this can reset stored configuration.

The one-page Mac app also provides screen language, device-synchronized speaker
volume, a separate Device Maintenance section and device-side connection checks.
All settings remain drafts until Save Configuration, including language and
volume. The Wi-Fi list fits its rows and scrolls when expanded. Pairing, routing
and diagnostics are collapsible. Settings are saved sequentially with device
readback between commands; Wi-Fi is saved last. This is not an atomic transaction:
if a step fails, completed steps remain saved and the remaining edits are retained
for retry. Saving never clears the form or reuses an acknowledged pairing code.
Switching agents keeps each draft; the global Save and Discard cover all of them.
Volume is saved on the Passport, not as a separate Mac preference. Matching
firmware is not flashed again. During Call, recording or a device check,
connection-changing controls are disabled; forms stay intact on success or failure.
The footer shows USB, device Wi-Fi, selected agent and available battery readings.
Idle standby turns the screen off after 60 seconds, not the device; a computer
USB connection keeps it awake and is not evidence of battery charging.

“Test address” runs from the Mac. “Check current agent” uses the Passport's Wi-Fi,
route and device credentials to check gateway authentication, access to the Ask
session and the plugin's realtime-provider configuration. It does not record,
send a prompt or prove end-to-end speech quality. The check can be stopped.
Older firmware keeps basic configuration available; update it to enable volume
sync and device checks. API keys stay on the gateway. This protocol adds `DEVICE`,
`VOLUME <expected> <desired>`, `CHECK` and `CHECK_CANCEL`, advertised by the
`device-control` HELLO capability. Volume writes reject stale readings and busy
devices; changes are acknowledged only after persistence succeeds.

No personal gateway address, tunnel account, Wi-Fi password or API key is built
into this repository. Devices are authorized by native OpenClaw device pairing;
they are not given the administrator's gateway token. Possession of a paired
device grants its configured authority, so revoke a lost device in OpenClaw.

## Build and test

Firmware uses **ESP-IDF 5.5.3**. Activate your installation, then run:

```sh
./tools/validate.sh --firmware
python3 tools/release.py firmware build/release/firmware
```

The gate builds from tracked defaults, verifies flash layout and the merged
image, and retains matching debug artifacts locally. The public release omits
ELF/MAP files. See [build and test](docs/development/engineering/build-and-test.md).
This application reserves `cardid` and `audio` partitions; inspect
`partitions.csv` before migrating from other firmware.

For host checks on macOS, install Xcode command-line tools, Node.js 22+, Python 3,
`mbedtls@3`, `pkg-config` and `xcodegen`. Set `PKG_CONFIG_PATH` to the mbedTLS
pkg-config directory and `CJSON_SOURCE_DIR` to ESP-IDF's `components/json/cJSON`
or a compatible cJSON source checkout. The CI workflow shows the pinned setup.

```sh
./tools/validate.sh --static
make plugin-test
make plugin-package
python3 tools/check_public.py
bash tools/check-secrets.sh
```

Build the Mac app locally with `tools/mac.sh` (optional `DEVELOPMENT_TEAM` in your
local environment). Use `make sim-passport` / `make sim-iphone` with their
respective simulator dependencies installed. Simulation is not a BLE or hardware
acceptance test. An iOS configuration app is also provided under `ios/` but is
not currently a signed Release deliverable.

### Product identifiers

Product source files, C APIs/macros, Swift types, Xcode targets, assets and
simulator tools use `Passport` / `passport` / `PASSPORT`. The setup access point
is `Passport-Setup` with password `passport-setup`.
The iOS Bundle ID is `ai.mirasim.passport`. NVS uses `passport` for settings and
`passport_id` for device identity; serialized settings start with `PSP1`.
The dedicated OpenClaw agent ID is `passport`, with device sessions named
`agent:passport:passport-<device-id>`. BLE service UUIDs are unchanged.
Unreleased prototype identities and settings are not migrated: development
devices need Wi-Fi setup and gateway pairing again, and the new iOS identity
installs separately from earlier development builds. Configure the dedicated
`passport` agent's model and tool permissions on the gateway before use.

### iPhone Wi-Fi provisioning

Wi-Fi uses compact saved-network rows and inline name/password fields, with
44-point minimum touch targets. Larger text sizes stack field labels above inputs.

Saved Wi-Fi networks can also be selected on the Passport itself: open
**Settings → Wi-Fi**, use **Next** to choose a network, **Open** for its details,
then **Join** to connect using the stored password. Joining runs in the
background; **Back** leaves the page without cancelling it. Call, Ask and
configuration writes are unavailable during the switch. A successful join is
saved for reconnect/startup; it does not delete or reorder the other networks.
On failure the device attempts to restore its previous Wi-Fi and shows the
reason. Add new networks and correct passwords from the phone or Mac app.

Bluetooth devices appear as `AI Passport XXXX`, where `XXXX` is the last four
hexadecimal digits of the device's Bluetooth address. The name remains stable
across restarts; the configuration service identifier is unchanged.

The iPhone app configures the passport over Bluetooth without switching the phone
to a temporary Wi-Fi network. Enable Bluetooth in the passport's settings, select
the device in the app, then scan for networks **from the passport** or enter a
2.4 GHz SSID and password manually. iOS does not expose saved Wi-Fi passwords;
this version does not request location access or read the current SSID.
Use **Search Wi-Fi** for one list of nearby networks, including personal hotspots.
The passport returns up to 32 network names. Firmware advertising `scan-live`
scans one allowed 2.4 GHz channel at a time and sends discoveries immediately.
The list updates during scanning without reordering existing rows; you can select
a network before scanning finishes. Saving waits until the scan has completed.
Older firmware still works but only returns results after its complete scan.
Recognized iPhone hotspots use a
chain-link icon; other networks use a Wi-Fi icon. Recognition uses an observed
Apple vendor information element, not the network name. This is a best-effort
hint, not an ownership or security check, and cannot identify every hotspot.
Unrecognized hotspots remain selectable as ordinary Wi-Fi networks. Older
firmware without optional `NET_KIND` metadata shows ordinary icons.

For an iPhone personal hotspot, enable Allow Others to Join and Maximize
Compatibility in Settings, then select its name from the same Wi-Fi list or
enter it manually. Selection updates the main draft without connecting or
saving. Enter the password, then use **Save Configuration** to send the draft
over Bluetooth. Canceling search preserves the existing draft. Selecting the
same name preserves a typed password; choosing a different name clears it to
avoid reusing another network's credentials. Empty scans can be retried, and
manual entry remains available after scan failure or Bluetooth disconnect.
iOS does not allow the app to enable the hotspot or retrieve its password
automatically. A saved hotspot can reuse the passport's password by leaving it blank.
The hotspot name is the iPhone name; it cannot be renamed independently. The
form asks for that existing name to tell the passport which network to join, not
to rename the phone. This app lacks Apple's restricted user-assigned-device-name
entitlement, so scan and select the hotspot, or copy the exact name from
Settings > General > About > Name instead of trusting a generic value returned
by iOS. Renaming the iPhone
also changes its hotspot name and requires updating the passport's Wi-Fi entry.
If the passport reports that it cannot find the hotspot, keep **Settings >
Personal Hotspot** open with the screen awake during connection; some iPhones
stop advertising to non-Apple devices after leaving that page. The app supports
background Bluetooth responses while you do this. A rejected configuration no
longer disconnects Bluetooth: drafts remain editable, and the error distinguishes
an undiscovered network, authentication, security compatibility, and IP timeout.

Saved networks (up to eight) are read from the passport; selecting one reuses its
stored password when the password field is empty. The global Save Configuration
button saves edited pairing/language settings first and Wi-Fi last, with an
acknowledgement and readback between changes. Success, failure, and reconnects
retain form inputs; Discard restores the last read or acknowledged values.
Forgetting a saved network is a separate confirmed action. Saving never flashes
firmware and does not modify the phone's network settings.

The iPhone form groups connection, Wi-Fi, OpenClaw and device preferences on one
scrolling page. Nearby networks open in a sheet; saved networks and additional
options expand in place. Save Configuration and Discard scroll with the form
instead of covering it with a fixed bottom bar. Larger accessibility text stacks
paired actions vertically.

Select Bluetooth in the passport's Settings menu and press the middle key to
turn it on or off in place; the row shows its current state, with no submenu.
The action is disabled while switching or saving configuration. Turning it off
disconnects the phone without forgetting its Bluetooth bond or saved networks.
Bluetooth is enabled by default in standby, with no two-minute discovery timeout;
leaving the network-setup page does not turn it off. On boot, saved Wi-Fi gets up
to ten seconds to connect before Bluetooth starts, unless configuration is
explicitly requested. Recording, calls and gateway operations temporarily release
Bluetooth memory and disconnect the phone; Bluetooth returns once audio and
gateway resources have been released. A manual off keeps it off until explicitly
enabled again or the passport restarts. Advertising alone does not prevent the
existing standby timeout. Idle gateway connections yield to Bluetooth, so the
next question may need to reconnect. Configuration and Wi-Fi changes keep an
existing phone connection available.

This client requires firmware advertising `ble-secure`: BLE writes require an
encrypted link, and replies use MTU-sized, acknowledged indications. Older
firmware must be updated with the Mac app before sending credentials. Firmware
persists bonds for up to three configuration clients in NVS, so normal reconnects
and reboots do not require pairing again. First connections, forgetting a device
in system settings, or clearing bonds still require pairing. The current Just
Works pairing encrypts traffic but does **not** provide
man-in-the-middle authentication or proof of device ownership. In untrusted
locations, turn configuration Bluetooth off in Settings when it is not needed.

Generate the Xcode project with `cd ios && xcodegen generate`, open
`ios/Passport.xcodeproj`, choose a local signing team, and run on a physical iPhone.
An unsigned simulator build can check the UI, but cannot provision a real passport
over Bluetooth. Physical-iPhone provisioning and hotspot coexistence still
require hardware acceptance testing.

Run the simulator layout checks after generating the project:

```bash
xcodebuild -project ios/Passport.xcodeproj -scheme Passport \
  -destination 'platform=iOS Simulator,name=iPhone 17' test CODE_SIGNING_ALLOWED=NO
```

## CI and releases

Pull requests and main pushes run repository/privacy checks, C/Python/Swift host
tests, a Mac build, an ESP-IDF firmware build and the plugin matrix on Node
22/24/26. Version tags build all distributables; publication waits for host
tests, firmware, plugin and the universal Mac app. Manual Release runs produce
artifacts without publishing a GitHub Release. See [CI and release](docs/development/ci/CI-build-and-release.md).

All Actions are pinned to commit SHAs; only the publishing job gets repository
write permission. Source scanning is preventative, not proof that arbitrary
future changes contain no secrets. Review diffs and generated assets before
publishing. Never upload local NVS dumps or private configuration.

## License and credits

[MIT](LICENSE), retaining FoloToy's attribution. Hardware/BSP and development
documentation originate from the upstream project. Fonts and other third-party
assets retain their own notices; see [assets](assets/README.md). Report sensitive
issues privately using the [security policy](.github/SECURITY.md).
