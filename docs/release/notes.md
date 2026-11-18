**English** · [简体中文](notes.zh_CN.md)

# AI Passport Agent v0.7.6

Shared gateway source and restored TLS 1.3 · 2026-11-18

## Highlights

- **One maintained gateway for multiple devices.** OpenClaw and Hermes adapters now
  come from the pinned Valet Gateway v0.1.0 submodule. Existing plugin IDs,
  `/passport/v1`, device signatures and pairing credentials remain compatible.
- **Restore TLS 1.3 in actual builds.** Keep the ESP-IDF dependency that previously
  disabled it silently, and fail the build if TLS 1.3 or client ticket support is
  missing. Logs distinguish offering a ticket from accepted resumption.
- **Complete installable sources.** Setup prompts use the shared release and exact
  reviewed commit. CI, doctor and packaging read that source. A new complete source
  archive includes submodule files; GitHub's automatic source archives do not.

## Install or update

Open <https://ai-passport-agent.codelife.me/> in desktop Chrome/Edge, connect by USB
and choose **Update firmware**, leaving **Keep settings** enabled for a compatible
update. Networks, assistants and device identity are preserved by application-only
updates. Full replacement can clear data; read the
[data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).

Gateway adapters are independently versioned: both packages remain 0.7.5, supplied
by [Valet Gateway v0.1.0](https://github.com/vangie/valet-gateway/releases/tag/v0.1.0).
A compatible installed plugin does not need replacement or re-pairing. If replacing
it, preserve device grants and use its normal lifecycle. Firmware updates never
replace or restart a gateway automatically.

Verify release assets against `SHA256SUMS.txt` and `release-manifest.json`, which
also records the shared source commit and adapter versions. Use
`ai-passport-agent-source.tar.gz` for a complete offline checkout, or initialize
`integrations/valet-gateway` with `git submodule update --init --recursive`.
Native macOS/iOS apps remain source-only.

## Validation and limits

The maintainer device retained three Wi-Fi networks and three assistants, negotiated
TLS 1.3 through Shadowsocks and completed three warm gateway probes and three
recording/stop tests without allocation failure. Ask audio reached the existing
OpenClaw and its final reply was retrieved; upstream model 502 retries delayed that
reply independently of device transport. A 20-second synthetic Live run completed
with zero allocation failures. These are not microphone/speaker quality or
long-term network guarantees. Offering a ticket did not show a measurable speedup
in this sample; warm WebSocket reuse avoided a new handshake.

OpenClaw plugin/relay/IMA tests, browser and native-client regressions, source/package
integrity checks and 31 isolated adapter tests under the actual Hermes v0.23.1
runtime passed. Fresh pairing on the physical device and E-Ink4 hardware were not
tested in this release. Existing gateways were not replaced or restarted.

See [release readiness](readiness.md) for exact validation and artifact receipts.
