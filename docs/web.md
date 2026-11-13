<p align="right"><a href="web.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Browser configuration studio

Open the published studio at <https://ai-passport-agent.codelife.me/>.
The repository's Pages address, <https://vangie.github.io/ai-passport-agent/>,
redirects to this custom domain.

### Legacy address compatibility

Keep `ai-passport-openclaw.codelife.me` as a compatibility entry, not a second
configuration studio. Its DNS record is `ai-passport-openclaw`, type `CNAME`,
value `vangie.github.io`, with the default TTL. Keep the new hostname's record
unchanged. DNS alone is insufficient: GitHub Pages needs a separate site bound
to the old hostname because a Pages site supports only one custom hostname.

The dedicated `vangie/ai-passport-legacy-site` repository hosts only a static
redirect, with the old hostname in its `CNAME` file and HTTPS enabled after its
certificate is issued. Generate its deployment files with
`python3 tools/legacy_site.py`; publish the resulting `build/legacy-site/`
contents to that repository's Pages source. Do not publish them to the main
studio or replace its custom hostname. The redirect source lives in
`tools/legacy-site/index.html`; its `404.html` copy handles old deep links too.
It uses `location.replace` to avoid a back-button loop, preserves paths, query
parameters and fragments, has no third-party requests or analytics, and includes
a bilingual link when scripts are disabled. This is a browser redirect, not an
HTTP redirect for firmware download clients. The new studio serves its own
firmware directly as before.

Restoring the old DNS record and its valid HTTPS certificate is necessary for
old bookmarks to work. Users with an already-open old page should reload it.
Device Wi-Fi, assistant credentials and firmware remain unchanged. Browser USB
and Bluetooth grants are origin-specific, so users may need to authorize their
device again at the new address; they do not need to erase or re-pair it merely
because the website moved. Keep the legacy hostname reserved and bound to this
redirect while its DNS points to GitHub Pages; do not delete the binding first.

The static website is the new browser-first configuration path. Existing
macOS App and iOS App remain as source-only alternatives, without CI builds or
new Release packages. No account, database or configuration
backend is needed; Passport remains the source of truth. Voice connects directly
from Passport to OpenClaw, not through the browser or its hosting service.

The compact header combines the existing project icon, AI Passport Agent name and page title in
one row, with language controls alongside. Narrow screens hide the repeated
brand text rather than adding another title row. The same project icon is used
for the browser tab.

Before connecting, the device heading and USB/Bluetooth buttons stay on one row,
including on narrow screens. Browser support notices and connection feedback appear
below that row only when relevant; there is no permanent instruction paragraph.

The header’s language control shows Chinese and English options, with a sliding
highlight on the current language. Click either option or use the arrow keys while focused.
It changes the website language only; device display language is saved separately
under **Device preferences**. Switching languages keeps the current inputs.
Device language and volume controls are dimmed and disabled until the device is
connected and its settings have been read. They disable again on disconnect,
without discarding drafts. Firmware without volume control keeps that slider disabled.

## Start locally

Use Node.js 22.12+ or 24 and Python 3:

```sh
make web
```

`http://localhost` and loopback addresses are trustworthy contexts for browser
device APIs; a local HTTPS certificate is not required. Opening `index.html`
directly is not supported. A phone opening the computer's ordinary LAN IP over
HTTP does not get the same exception. Use trusted HTTPS for phone testing.

`make web` owns the local server and tunnel in one foreground process. Without
`cloudflared`, it runs locally only. Press Ctrl+C to stop both. The server binds
only to loopback and serves `web/dist`, never the repository or Vite development
server. It prefers port 4173; if that port is occupied, it automatically selects
a free port without interrupting or exposing the existing service. Use the local
and HTTPS addresses printed by the command.

`make web` is the only Make entry point for the website. Keep the first terminal
running and run the same command again after editing: it rebuilds, reuses the
existing server and tunnel, prints the same addresses, and exits. Refresh the
page to see changes. Concurrent invocations serialize their builds and only one
owns the preview. Files are staged separately, then replaced with the entry page
last; existing asset files stay available for already-open pages. A failed build
does not replace the working preview. No extra start, status or rebuild command
is needed.

Quick Tunnels allocate a new address after the tunnel process restarts; a saved
URL cannot reserve it. The launcher clears the public address when all tunnel
connections go down, and restores it after a connector registers again. If
Cloudflare reports that a Quick Tunnel no longer exists, the launcher tries up
to two replacements while keeping the local server running; each replacement
has a new address. Run `make web` again to print the current address. Named
tunnels are never replaced with Quick Tunnels automatically.

For a stable address across restarts, configure a dedicated
named tunnel and a hostname in your Cloudflare account once:

```sh
cloudflared tunnel login
cloudflared tunnel create ai-passport-web-dev
cloudflared tunnel route dns ai-passport-web-dev passport-dev.example.com
```

Create `~/.config/ai-passport-agent/web-tunnel.json` outside the repository.
If absent, the previous `~/.config/ai-passport-openclaw/web-tunnel.json` is still read:

```json
{
  "tunnel": "00000000-0000-0000-0000-000000000000",
  "hostname": "passport-dev.example.com",
  "credentials_file": "~/.cloudflared/00000000-0000-0000-0000-000000000000.json"
}
```

Replace the example hostname with one in your Cloudflare-managed domain and use
the UUID and credential file reported by `tunnel create`. Next time `make web`
automatically selects this tunnel, and the address survives restarts. An optional
`PASSPORT_WEB_TUNNEL_CONFIG` environment variable selects another JSON file.
Invalid fixed configuration fails explicitly rather than silently opening a
temporary public URL. Credentials must stay outside the repository. The launcher
does not change DNS, install a background service, or modify existing tunnels.

Only the configured hostname routes to the preview; other hostnames get 404.
The preview stores its address and lock under ignored `build/`, never `web/dist`.
With either tunnel type, the URL is public unless you separately configure
Cloudflare Access, and reachability varies by network. The tunnel serves only the
page, never USB/Bluetooth traffic or OpenClaw. Do not enter real credentials into
an untrusted copy. See Cloudflare's [Quick Tunnel limitations](https://developers.cloudflare.com/tunnel/get-started/quick-tunnels/)
and [named tunnel setup](https://developers.cloudflare.com/tunnel/features/local-management/create-local-tunnel/).

## Connect and configure

The page detects Web Serial and Web Bluetooth capabilities, not browser names.
Supported browsers show no compatibility notice. Bluetooth-only browsers such as
Bluefy hide the USB connection option and replace the firmware module with one
short note to update over USB on a computer. Other unavailable connection methods
are disabled with an explanation beside that button; an insecure address shows a
single HTTPS/localhost reminder. Detection does not request device permissions.
Browsers with Web Serial retain the full firmware-update controls.

The configuration page only connects to real devices over USB or Bluetooth.
It has no sample-settings entry or virtual-device mode; test fixtures are used
only by automated tests. Without a connected device, reading, scanning and saving
remain disabled, while a full firmware installation is available over USB.

1. On a computer, open the page in Chrome or Edge and choose **Connect USB**.
   Select the Passport serial port; close native apps or serial monitors that
   already hold it. For Bluetooth, use a browser implementing Web Bluetooth and
   enable Bluetooth in **Passport → Settings → Bluetooth** while the device is idle.
   Firmware advertising `ble-takeover` stays discoverable while connected. A new
   client takes over only after subscribing and sending an encrypted `HELLO`;
   completed commands are acknowledged before the handoff, while queued commands
   from the old session are rejected. The old client keeps its draft and does not
   reconnect automatically. Only one client can edit; a second slot is reserved
   for handoff and expires after two minutes without a successful handshake.
   Older firmware requires disconnecting in the phone app before scanning again;
   saved pairings never need deleting. iPhone Safari and WeChat's webview do not
   support this route; Bluefy is a candidate requiring real-device acceptance.
   If the chooser is empty on Mac, check **System Settings → Privacy & Security →
   Bluetooth** for the browser you are actually using, then reopen it. USB access
   does not grant Bluetooth access. After updating the page, close the old chooser
   and connect again. Discovery matches either the configuration service or the
   `AI Passport ` name prefix; connecting still requires the correct GATT service.
   Bluetooth guidance appears below the connection card only after choosing
   **Connect Bluetooth**, not on initial load or when choosing USB. It disappears
   after a successful connection. If permission is already enabled but discovery
   stays empty, expand **Can’t find
   your Bluetooth device?**. Note any unsaved inputs, then fully quit and reopen
   the browser, not just refresh the page. In Chrome, enter `chrome://restart` in
   the address bar, then reopen this page and connect again. If the device is still
   missing, use **Compatibility scan: show all Bluetooth devices**. It removes
   discovery filters only; select `AI Passport` followed by
   four characters. The configuration service must still exist before any commands
   are sent. Do not reflash firmware, erase settings or reset all system Bluetooth
   pairings as a first troubleshooting step.
2. The page reads saved Wi-Fi networks, agents and device preferences. Scan
   nearby 2.4 GHz networks with Passport, or type a name. Leave the password blank
   only for a saved or explicitly open network. Wi-Fi testing may switch the
   Passport connection, never the computer's Wi-Fi.
3. Under **Add an agent**, select **OpenClaw** or **Hermes**, then copy the matching
   setup prompt to an agent with access to that gateway host. Paste its
   pairing code or QR JSON and name the agent. A conflicting address is rejected,
   not silently replaced. No camera-based QR reader is included in this version.
4. Each agent lists its saved connection method. Choose **Connection settings**
   on that agent to expand its direct or Shadowsocks settings in place. Editing
   another agent does not make it current; unsaved connection changes are marked
   on their owner and confirmed before switching editors. Edit device language
   or speaker volume as needed, then choose **Save settings**. The firmware currently
   supports `aes-256-cfb` only. Secrets are write-only. Saving multiple groups is
   sequential, not atomic: completed groups stay saved if a later group fails.
5. Saving, errors and disconnects do not clear your inputs. **Discard changes**
   explicitly abandons the draft. Forgetting a network, removing an agent and
   making an agent current take effect immediately. No settings or secrets are
   sent to a configuration server or persisted in browser storage.

The floating save bar shows a single status beside its buttons. After saving it
shows **Saved**, without an additional summary line or a taller bar. Editing again
restores the unsaved-changes status. Errors and partial-save details can wrap so
the recovery instructions remain readable on a narrow screen.

The agent type selects the setup instructions only; switching it keeps entered
names, addresses and pairing codes and does not change saved device settings.
Prompts follow the website language. OpenClaw instructions use its plugin and QR
commands; Hermes instructions use its external platform plugin, active profile
and pairing tool. Hermes requires compatible v0.7.0 or newer firmware and plugin;
it is not included in v0.6.0. The device recognizes the backend after an
authenticated connection, independently of the assistant's display name.

Copy feedback stays beside **Copy setup prompt**, never in the page-wide notice.
The button briefly shows **Copied** with a check mark and a short next-step hint;
loading and clipboard errors stay in the same place and can be retried. Copying
does not rebuild the form, change the connection or discard inputs.

The browser cannot retrieve system Keychain passwords, silently enable Personal
Hotspot, or read its password. For iPhone hotspot discovery, enable Allow Others
to Join and Maximize Compatibility, then keep the Personal Hotspot settings page
open and the screen awake. Test this background transition on the actual phone.

## Privacy and data

The footer's **Privacy & data** link opens an explanation without interrupting
device setup or repeating a privacy notice in each card. This is a static
single-page application with no configuration backend: Wi-Fi passwords, proxy
secrets, gateway addresses and pairing codes go directly to Passport over USB or
Bluetooth. Drafts remain in page memory for editing and retry, not persistent
browser storage. Saved settings live on the device. The notice distinguishes
this page from the device's OpenClaw/voice services and notes that the static
hosting provider may retain ordinary access logs.

## Firmware without runtime Release redirects

The compact firmware card places device and available versions side by side,
with a USB label in its header and one right-aligned action button. Equal-width
version columns keep the vertical divider at the center of the card.
**Keep settings** is always shown and checked by default. **Update firmware**
stays disabled until a USB connection has read the device, so keeping settings
never silently falls back to a full installation.
For first installation or recovery, uncheck **Keep settings** and use the same
button, now labelled **Install firmware**. This remains available without a
configuration connection, with a warning and confirmation before overwriting
settings. There is no separate recovery entry. New connections restore the
checked default. Bluetooth enables device preferences, but the firmware button
and checkbox stay disabled until a USB connection is used.
Without bundled firmware, installation remains disabled.

The website bundles dependencies locally. CI downloads the official Release at
build time, verifies its receipt's size and SHA-256, and places both images and a
manifest under the website's own `firmware/` directory. User browsers fetch them
from that same origin; they do not call GitHub Releases, S3 or a third-party CDN.
GitHub Pages itself can still be slow or unreachable on some networks. Temporary
Cloudflare URLs have the same reachability caveat, not a bypass guarantee.

To stage an already downloaded official Release locally:

```sh
python3 tools/web.py firmware --from-release PATH_TO_RELEASE_FILES
make web
```

The directory must contain `release-manifest.json`, `FoloToy-AI-Passport.bin`
and `FoloToy-AI-Passport-full.bin`. These generated copies are ignored by Git.
The build succeeds without them, but bundled flashing is then unavailable.
The website installs only its bundled project release; custom images remain a
command-line developer workflow. Normal updates preserve settings. For first
install or full recovery, uncheck **Keep settings** and choose **Install firmware**,
with or without an existing USB configuration connection;
an additional confirmation is required before stored configuration is overwritten.

The website resolves the device's build identifier internally. CI verifies
application images and receipts (or legacy SHA-256 checksum lists) from up to 12 recent official releases and
bundles their version identities; no runtime history lookup is required. The
retired builds retain verified identities in `web/release-builds.json`, independent
of whether their old GitHub Release downloads remain available. Known
older releases show **Update available**; the bundled release shows **Up to date**
with **Update firmware** still available for reinstallation. A newer known release is not called an
update to an older image. Unrecognized builds show **Not identified**, not
**Installed** as a substitute for a version. The button stays **Update firmware**;
the available version is shown above it, with development-build replacement and
downgrade warnings kept in the confirmation dialog.
Raw identifiers live under collapsed **Version details**.

For `make web`, local archived application hashes and embedded ELF identifiers
are verified to identify development builds automatically. Only a normalized
base version, development channel and build identifier enter the preview
catalog, never arbitrary descriptor text, local paths or device settings.
Development firmware uses the same **Update firmware** button, with a separate confirmation
that switching to the official release may remove development features. This
catalog does not replace the bundled official firmware or label development
code as a public release. Production staging discards the local catalog.

An app update first requires a successful USB read, validates ESP32-C3 image
headers and the on-device partition table, and writes only `0x10000..0x34ffff`.
It preserves NVS, Card ID and audio data. First install/full recovery writes the
bundled merged image at zero, overwrites settings, and requires confirmation.
Neither mode performs a full-chip erase. Image download hashes are checked before
resetting the device; the flasher also compares device MD5 after writing. Keep
USB connected. Web flashing/Bluetooth compatibility still require physical-device
acceptance; automated tests with a virtual device are not such a test.

## GitHub Pages

`.github/workflows/web.yml` runs protocol/browser tests, stages the latest
published firmware and uploads the static site to Pages. PRs only test and build;
main pushes, manual runs and published Releases deploy. A completed successful
tagged Release workflow also triggers deployment, including releases created
with GitHub's own workflow token. The workflow has no
runtime backend and needs no stored deployment secret. Set Pages' build source
to **GitHub Actions**. Its relative asset paths support repository subpaths.

Changes to `web/`, shared setup prompts, artwork and web build/audit tools on
`main` automatically publish after tests pass. Deployment first scans source
with the public-endpoint audit and Gitleaks, then audits the exact static artifact.
The artifact allowlist excludes local configuration, logs, source maps and debug
files; firmware checks reject stored device data and local development catalogs.
Failures stop publication. Only `web/dist` is uploaded, never the working tree,
local preview tunnel configuration, browser drafts or developer credentials.

The static source and firmware hashes share the website's trust boundary; hashes
detect corruption, not a compromised publisher. Avoid third-party scripts,
analytics or URLs containing credentials. Keep OpenClaw's public ingress and
authentication separate; see [Gateway connectivity](networking.md).
