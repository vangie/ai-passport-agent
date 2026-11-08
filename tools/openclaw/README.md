<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# AI Passport OpenClaw plugin

## Project component and distribution

This directory is the maintained OpenClaw plugin source for the
`ai-passport-openclaw` open-source project, released under the same
[MIT license](LICENSE) as the firmware and configuration apps. It is maintained
in this repository, not in a private host checkout or a fork of OpenClaw. Runtime
installation remains separate because the plugin runs on the gateway computer,
while firmware runs on the Passport. No personal gateway address, agent name,
account, network or credential is built into the plugin.

From the repository root, with Node.js 22 or later, npm, Python 3 and Make:

```sh
make plugin-test
make plugin-package
```

Tests and packaging do not need ESP-IDF, hardware, a running OpenClaw instance,
model credentials or a connected microphone. The versioned archive is written
to `build/openclaw/ai-passport-openclaw-<version>.tgz`. Its explicit file allowlist
includes runtime source, migration/doctor tools, bilingual instructions and the
license, but not dependencies, caches, host links, gateway configuration or keys.
The package regression verifies the actual tarball contents and JavaScript syntax.
The checked-in lockfile pins the transport dependency for source installations;
it does not install a second OpenClaw gateway.

The dedicated [plugin workflow](../../.github/workflows/openclaw-plugin.yml)
installs locked dependencies and tests/packages on Node 22, 24 and 26, then keeps
the archive as a CI artifact. It does not publish to npm, create a release,
contact a gateway or deploy firmware. The full repository gate includes the
same package regression. Firmware and plugin versions can advance separately;
coordinate changes to their shared wire protocol and validate the supported
OpenClaw version before release. Packaging tests are not a live gateway SDK test.

## Upgradeable native integration

Version 0.3.0 replaces the installed-core compatibility patch with an independent
plugin: `passport-realtime` provider, `/passport/v1` WebSocket transport and the
authenticated `passport.status` RPC. Runtime code uses public plugin SDK imports,
not generated filenames, private exports or edits to OpenClaw installation files.
OpenClaw 2026.10.5 is the tested baseline; future compatibility must be checked.

Original device challenge, signature, role, scopes and token pass to the native
loopback gateway unchanged. No server/admin credential or synthetic caller is
used. The HTTP route is plugin-authenticated because authentication occurs in
the native WebSocket handshake, not a new HTTP password. Unauthenticated clients
cannot dispatch RPCs. Client forwarding headers are ignored, and upstream
forwarding never qualifies as direct-local ingress. Keep existing gateway
exposure/proxy policy. Connections, pre-auth time, frames, pending requests,
socket backlog and audio queues are bounded; stopping the plugin closes sockets.

Native OpenClaw owns revocation, session authority, transcripts and tool approval.
The wrapper inherits the configured official provider's model, voice and
agent-scoped credentials without copying keys. It retains native direct answers,
consult/control tools, language policy, IMA compression, pacing and working/final
tool distinctions. Other clients retain their provider/route. Global
`force-agent-consult` is rejected rather than silently changed.

Install a durable copy outside the gateway package:

```sh
cd /path/to/ai-passport-openclaw/tools/openclaw
npm ci --omit=peer --ignore-scripts
openclaw plugins install --link --accept-capabilities /path/to/ai-passport-openclaw/tools/openclaw
node doctor.mjs
openclaw gateway call passport.status --json --params '{"agentId":"badge"}'
```

Node must resolve the host's `openclaw/plugin-sdk/*`. If global resolution is
unavailable, link the existing host package to `node_modules/openclaw`; do not
install a second gateway. The doctor checks public SDK contracts and the native
challenge, creating/closing a bridge without connecting to a model, starting a
microphone or exporting credentials. The status RPC checks optional `agentId`
credential readiness inside the running gateway.

For an archive installation, extract the package to a durable directory on the
gateway computer, run `npm install --omit=peer --ignore-scripts` there, and use
that directory with the same `openclaw plugins install --link` command. An npm
publication is not required. Set up model credentials and a reachable gateway
using OpenClaw itself; this project does not distribute either. Substitute your
actual agent ID for `badge` in the status example.

When updating a source installation, retain its working version, update only
the plugin source, run `npm ci --omit=peer --ignore-scripts`, then reload/restart
through OpenClaw's existing service manager and rerun the doctor and status RPC.
Do not put the plugin inside the OpenClaw package or reapply the legacy patch.

Forwarding through loopback requires that the gateway's existing trusted-proxy
policy accepts the local transport hop. Do not widen that policy automatically.
Migration requires zero active Passport connections. Closed calls discard queued
audio and reject late events before another call can reuse the connection.
The transport verifies native event continuity and assigns its own contiguous
downstream sequence after filtering/batching; native sequence gaps still fail.

For first migration retain the previous firmware and run:

```sh
python3 manage-plugin.py inspect --dist /actual/openclaw/dist
python3 manage-plugin.py migrate --dist /actual/openclaw/dist
```

Migration verifies the legacy receipt/original, retains a checksummed rollback
directory, and restores the official handler. It does not restart or change
gateway settings. Restart the existing service once and flash protocol-1
firmware. Only Call appends `/passport/v1` to the saved endpoint path; Ask and
pairing retain their route. Saved endpoints/proxies/device credentials need not
change. Missing protocol markers fail before recording starts.

After upgrading OpenClaw run the doctor and status RPC again, not the old patch.
Incompatible SDK/provider/protocol contracts fail explicitly. Update this plugin
in its own directory/package and reload/restart normally. Retain the last working
plugin package. Explicit legacy rollback refuses to overwrite upgraded handlers:

```sh
python3 manage-plugin.py rollback --dist /actual/openclaw/dist --backup /saved/rollback-directory
```

Restart and restore the previous root-route firmware for legacy rollback. Never
restore an old patch into a new OpenClaw release. Synthetic ready/close and host
tests do not prove physical voice quality or actual news-result playback.

### Validated migration checkpoint (2026-11-08)

On the existing host, plugin 0.3.0 runs with OpenClaw 2026.10.5. The original
handler checksum was restored and the legacy patch receipt/modules removed.
An ephemeral signed device completed native pairing, Call creation, provider
ready and close against the unpatched core; its test authorization was removed.
Host tests cover tool-progress/final retention, session reuse, sequence continuity,
migration, tamper rejection and rollback refusal after a simulated core upgrade.
No actual future OpenClaw release upgrade was performed.

Physical Call audio, news-result playback and real network reconnects remain
user acceptance checks. Published firmware identities come from each Release's
manifest and checksums, not a developer's earlier local build.

## Legacy patch reference

This optional, version-checked patch addresses compatibility behaviors of the inspected
OpenClaw 2026.10.5 gateway. It is not a firmware dependency or an upstream fix.
Ordinary OpenClaw clients remain unchanged. Gateway authentication, credentials,
network exposure, agent configuration and tool authority are not modified.

For an authenticated client whose signed device family is `ai-passport`:

- Apply the selected call language to the provider's conversation instructions,
  forced-consult response style, waiting acknowledgement and final spoken result.
  `zh` / `zh-*` uses Mandarin with Simplified Chinese captions; `en` / `en-*`
  uses English. Explicit user requests to switch languages or translate remain
  supported. The upstream `language` parameter only hints input transcription;
  it does not constrain spoken output. Preserve existing persona/tool instructions
  and leave other device families and unspecified languages unchanged. End and
  reopen existing calls after installing this fix; no firmware flash is needed.
- Enable native audio responses for ordinary conversation and general knowledge,
  without waiting for final transcription or forcing an agent consult on every
  turn. Retain the configured agent persona and consult/control tools. Current
  information, tools, actions, private memory and workspace/cross-session context
  still require an agent consult, with relevant direct-call context included in
  its arguments. This is model-directed routing, not guaranteed classification;
  ambiguous speech should prompt clarification and tool failures must not be
  presented as successful work. Existing tool authority and confirmation checks
  remain unchanged. Non-Passport clients retain their configured consult routing.
  End and reopen existing calls after updating; no firmware flash is needed.
- Combine output PCM16/24 kHz into at most 80 ms frames, flushing a partial frame
  within 20 ms and before marks, captions or response boundaries. Pace at 48
  bytes/ms with a 160 ms PCM or 800 ms negotiated IMA lead. Strip duplicated `talkEvent` metadata and
  omit input-audio echo events from the Passport wire only; gateway history and
  control/upload acknowledgements remain unchanged.
- Preserve the `type` and `final` fields of tool-result metadata on the compact
  wire. A working or partial result must not retire the badge's pending consult:
  its later chat completion still needs to be submitted to the voice provider.
  Other duplicated tool metadata remains omitted. This gateway-only correction
  requires reopening the call, not reflashing the badge.
- Advertise `passportSilence` during session creation. Only after negotiation,
  exact-zero input frames may use `audioBase64: "passport-silence:<byte-count>"`.
  Decode bounded even lengths (2–3,840 bytes) into identical PCM before the
  existing bridge; preserve capture cadence, timestamps and VAD time. Ordinary
  audio and unpatched gateways retain base64 PCM. This is a private compatibility
  extension, not an upstream base64 encoding or new unauthenticated endpoint.
- Limit the server queue to 6 MiB PCM and 20,000 events. An overflow fails the
  call explicitly. Cancellation discards queued audio, and session close stops
  the timer and releases queued references.
- Negotiate `passportIma` only if the authenticated Passport advertises
  `passport-ima-v1`. Encode both directions as `passport-ima:<base64>` using
  IMA ADPCM: a six-byte little-endian predictor/index/reserved/sample-count
  header, then low-nibble-first codes for up to 480 samples per block. Four
  full blocks use 984 bytes instead of 3,840 PCM bytes. Validate all bounds and
  decode input to PCM before the existing provider bridge. This lossy private
  extension does not change the provider format or sample rate; unpatched
  clients/gateways keep the existing PCM path.

OpenClaw can generate output faster than the badge plays it. Without pacing,
the small playback ring stalls reading the shared WebSocket; acknowledgements
behind queued output audio can reach the ten-second upload deadline even though
the gateway is healthy. Increasing that deadline or the badge heap does not
remove the head-of-line blockage. The badge reuses its existing 13,440-byte
half-duplex allocation for a 280 ms PCM ring (160 ms prefill), or 54 fixed
246-byte IMA slots: 1.08 seconds with bounded 640 ms prefill. Batched frames
decode through the existing 960-byte gateway scratch. Playback needs a local
246-byte block scratch; neither the shared allocation nor task stack sizes
grow. Upload uses spare space in its existing allocation before a single
base64 encoding, including frames ending in partial codec blocks. An eight-slot
firmware mark queue coalesces only its last, strictly increasing `audio-N`
mark when full; provider acknowledgements are cumulative and wait until the
replacement's complete PCM target has played. Arbitrary marks are not coalesced.
Reconnect is still explicit. Wall-clock pacing is not playback-credit flow
control and cannot guarantee continuity through arbitrary network stalls.

Playback preserves its primed state across packet gaps shorter than 10 ms,
within the approximately 21 ms I2S DMA window at 24 kHz. Previously, an empty
software ring immediately triggered another 160 ms prefill even when DMA still
held audible samples; ordinary packet jitter could become a noticeable pause.
Longer starvation and a new response still use bounded prefill. The existing
13,440-byte ring, scratch space and task stacks do not grow. Deterministic audio
tests cover a 5 ms refill without rebuffering and a 60 ms interruption with
bounded rebuffering; physical speaker continuity remains a device check.

Voice is configured on the gateway, not compiled into the badge. For example,
`openclaw config set talk.realtime.providers.openai.voice coral` selects Coral
for newly created OpenAI realtime sessions. This setting is provider-wide,
not an agent-specific override, and does not change an already speaking
session. Keep the configured model and credentials unchanged.

Paced output carries a separate `playbackSequence`. The matching firmware keeps
output deduplication independent from input event sequencing: otherwise a newer
input event could cause older, deliberately delayed audio to be discarded. Old
gateways without this field retain their existing sequence handling. Install
the matching firmware before testing calls against a patched gateway.

## Install and recover

Copy this directory to the gateway host. Use its actual installed OpenClaw
`dist` directory; do not guess a globally installed package's location.

```sh
python3 install-passport-relay.py /path/to/openclaw/dist
python3 install-passport-relay.py /path/to/openclaw/dist --apply --expected-sha <dry-run-original-sha>
```

The installer requires exact supported source boundaries and syntax-checks the
patched JavaScript. It retains a checksummed original below
`.passport-relay-backups/` and a receipt with handler, relay and codec SHA-256
hashes. Repeating an identical
installation is a no-op; unexpected modifications abort instead of being
overwritten. Restart the gateway through its existing service manager after
installation; active gateway connections will be interrupted by that restart.

Rollback is explicit:

```sh
python3 install-passport-relay.py /path/to/openclaw/dist --restore
```

Restart through the same service manager after restoring. Do not blindly replay
this patch after an OpenClaw upgrade: the package manager may replace the
handler/module, and version-specific integration must be checked again.

## Verification

```sh
make plugin-test
make plugin-package
./tools/validate.sh
```

Run these from the repository root. The JavaScript regression simulates a
20-second output burst, concurrent control traffic, ordered completion,
cancellation and queue overflow. Firmware regressions verify output sequence
isolation and bounded playback startup. These checks do not exercise a physical
microphone, speaker, Wi-Fi or public tunnel.

To update an installed patch, verify its receipt and use `--restore` before
applying the new revision with the original handler hash. Restore validates
the actually installed module against its receipt, not the new source copy.
Do not restart between restore and reapplication; restart once after both
succeed. Retain the previous patch source for explicit rollback.
