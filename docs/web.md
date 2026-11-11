<p align="right"><a href="web.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Browser configuration studio

The static website is the new browser-first configuration path. Existing
macOS App and iOS App remain available. No account, database or configuration
backend is needed; Passport remains the source of truth. Voice connects directly
from Passport to OpenClaw, not through the browser or its hosting service.

## Start locally

Use Node.js 22.12+ or 24 and Python 3:

```sh
make web                 # Build and serve at http://localhost:4173
make web-dev             # Vite development server at http://localhost:5173
make web-test            # Protocol and browser checks; local Chrome required
make web-build           # Static files in web/dist
```

`http://localhost` and loopback addresses are trustworthy contexts for browser
device APIs; a local HTTPS certificate is not required. Opening `index.html`
directly is not supported. A phone opening the computer's ordinary LAN IP over
HTTP does not get the same exception. Use trusted HTTPS for phone testing.

For a temporary HTTPS URL, leave `make web` running and run this separately:

```sh
make web-tunnel          # Requires cloudflared; forwards only localhost:4173
```

The Quick Tunnel URL is public, temporary and not guaranteed reachable from every
network. Stop the command to close it. Only the built static directory is served;
the repository, environment, serial port and local filesystem are not exposed.
The tunnel serves the page, never USB/Bluetooth traffic or OpenClaw. Do not enter
real credentials into an untrusted copy of this page.

## Connect and configure

1. On a computer, open the page in Chrome or Edge and choose **Connect USB**.
   Select the Passport serial port; close native apps or serial monitors that
   already hold it. For Bluetooth, use a browser implementing Web Bluetooth and
   enable Bluetooth on the idle device. iPhone Safari and WeChat's webview do not
   support this route; Bluefy is a candidate requiring real-device acceptance.
2. The page reads saved Wi-Fi networks, agents and device preferences. Scan
   nearby 2.4 GHz networks with Passport, or type a name. Leave the password blank
   only for a saved or explicitly open network. Wi-Fi testing may switch the
   Passport connection, never the computer's Wi-Fi.
3. Copy the setup prompt to an agent on the OpenClaw gateway host. Paste its
   pairing code or QR JSON and name the agent. A conflicting address is rejected,
   not silently replaced. No camera-based QR reader is included in this version.
4. Edit device language, speaker volume and the selected agent's direct or
   Shadowsocks route, then choose **Save settings**. The firmware currently
   supports `aes-256-cfb` only. Secrets are write-only. Saving multiple groups is
   sequential, not atomic: completed groups stay saved if a later group fails.
5. Saving, errors and disconnects do not clear your inputs. **Discard changes**
   explicitly abandons the draft. Forgetting a network, removing an agent and
   making an agent current take effect immediately. No settings or secrets are
   sent to a configuration server or persisted in browser storage.

The browser cannot retrieve system Keychain passwords, silently enable Personal
Hotspot, or read its password. For iPhone hotspot discovery, enable Allow Others
to Join and Maximize Compatibility, then keep the Personal Hotspot settings page
open and the screen awake. Test this background transition on the actual phone.

## Firmware without runtime Release redirects

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
A local application `.bin` can also be selected; its SHA-256 is displayed, but
this is not proof of its publisher. Only use trusted images.

An app update first requires a successful USB read, validates ESP32-C3 image
headers and the on-device partition table, and writes only `0x10000..0x34ffff`.
It preserves NVS, Card ID and audio data. First install/full recovery writes the
bundled merged image at zero, overwrites settings, and requires confirmation.
Neither mode performs a full-chip erase. Image download hashes are checked before
resetting the device; the flasher also compares device MD5 after writing. Keep
USB connected. Web flashing/Bluetooth compatibility still require physical-device
acceptance; a working demo is not such a test.

## GitHub Pages

`.github/workflows/web.yml` runs protocol/browser tests, stages the latest
published firmware and uploads the static site to Pages. PRs only test and build;
main pushes, manual runs and published Releases deploy. A completed successful
tagged Release workflow also triggers deployment, including releases created
with GitHub's own workflow token. The workflow has no
runtime backend and needs no stored deployment secret. Set Pages' build source
to **GitHub Actions**. Its relative asset paths support repository subpaths.

The static source and firmware hashes share the website's trust boundary; hashes
detect corruption, not a compromised publisher. Avoid third-party scripts,
analytics or URLs containing credentials. Keep OpenClaw's public ingress and
authentication separate; see [Gateway connectivity](networking.md).
