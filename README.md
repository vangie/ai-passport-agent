<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# AI Passport OpenClaw

A wearable OpenClaw voice terminal for the **FoloToy AI Passport**: configure it
from a Mac, then use Wi-Fi without keeping a phone connected. Built for the
ESP32-C3, 8 MB Flash, 240 × 240 display, microphone, speaker and three buttons.

This is an independent community project derived from
[FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), not official
FoloToy firmware or an OpenClaw distribution.

## What is included

| Component | Source | Purpose |
| --- | --- | --- |
| Firmware | `app/`, `main/`, `components/bsp/` | Chinese/English UI; Call and Ask modes; device-held configuration; Wi-Fi, audio and transport |
| Mac app | `mac/`, `shared/` | USB status, Wi-Fi and agent configuration, pairing, firmware checks and application-only flashing |
| OpenClaw plugin | [`tools/openclaw/`](tools/openclaw/README.md) | Upgradeable native Talk integration and authenticated `/passport/v1` WebSocket transport; no edits to installed OpenClaw core |
| Connectivity guide | [Networking](docs/networking.md) | LAN, Cloudflare Tunnel, SakuraFrp, TLS, authentication and troubleshooting |
| Development tools | `tools/`, `tests/`, `sim/` | Host tests, firmware verification, packaging and simulator launchers |

Call uses a realtime voice provider through OpenClaw; Ask submits recorded
speech to the gateway's `badge` agent. Create that dedicated agent in OpenClaw
and configure its model/tool permissions before using Ask. Multiple assistant
profiles select gateway connections; they do not change Ask's agent ID yet.
Model credentials stay on the gateway.
Configuration is stored on the Passport; the Mac also remembers useful local
form values and saved Wi-Fi passwords using platform storage. Do not publish
configuration exports, pairing codes, logs or device credentials.

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
5. Write configuration. Normal application-only updates target `0x10000` and
   preserve settings **only with a compatible partition layout**. A blank device
   needs the merged image from `0x0`; this can reset stored configuration.

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
