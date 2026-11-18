<p align="right"><a href="readiness.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# v0.7.6 release readiness

The user authorized physical validation and publication of a new version on
2026-11-18. This release combines the shared-gateway source migration and the
verified TLS 1.3 configuration correction. Existing remote gateway installations
are not replaced or restarted by this release.

## Shared gateway

- Valet Gateway v0.1.0: `40cbd826504bbf65a26f50bcea604ab5a0335351`.
- Release: <https://github.com/vangie/valet-gateway/releases/tag/v0.1.0>.
- OpenClaw and Hermes adapter versions remain 0.7.5; IDs, `/passport/v1`, signed
  device authentication, bootstrap/device tokens and database formats remain compatible.
- Node 22/24/26 CI and package download verification passed. The actual Hermes
  v0.23.1 runtime passed all 31 adapter tests using a disposable workspace.
- E-Ink4 signed identity support is additive; no E-Ink4 firmware is included here.

## Physical candidate

The pre-release candidate is archive `576130cf705f978d68d62c37d9d2987cdc5443d361b63908ed6effbcfdaf81ed`,
ELF `589c8f86086444b91cbe1df940f6976df7f3b1e16d1eb95cb78e157ab254d82a`.
Only the application was written; partition identity, settings and device grants
were preserved. The device retained three networks and three assistants.

- TLS 1.3: cold handshake about 2.7 s through the configured Shadowsocks route.
- Three warm gateway probes: about 0.25 s each, existing socket reused.
- Three microphone capture/stop tests: valid PCM, successful worker cleanup.
- Bluetooth on/off: socket closes, ticket is cached and offered again; reconnect
  took about 2.75 s. Offering the ticket did not establish a measurable speedup.
- Actual Ask: audio uploaded, request accepted, final response retrieved. Upstream
  model 502 retries delayed completion; the provider fallback eventually succeeded.
- Synthetic Live: 250 appends, 956672 audio bytes, ready in 2881 ms with an existing
  gateway connection, clean close. Minimum free internal heap 12848 bytes,
  no allocation failures and heap integrity intact.

## Gates

Complete static/host and isolated firmware gates passed for the candidate; web
protocol/browser and retained native-client checks passed. Source archive extraction,
plugin package allowlists, privacy and secret scanning passed. Release artifacts
will be rebuilt from the immutable v0.7.6 tag and independently downloaded/verified.

No existing gateway is automatically upgraded. Fresh physical-device pairing,
long-run network reliability and microphone/speaker quality are not established by
these tests. Release artifact and Pages receipts are recorded after publication.
