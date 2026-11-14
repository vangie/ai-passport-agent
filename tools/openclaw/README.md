<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# AI Passport Agent — OpenClaw plugin

## Project component and distribution

This directory is the maintained OpenClaw plugin source for the
`ai-passport-agent` open-source project, released under the same
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

### Device standby

After 60 seconds without activity, the Passport turns off its backlight, puts the
LCD into reversible sleep, pauses LVGL ticks and component button polling, and
releases the application's full-speed/keep-awake power locks. ESP-IDF automatic
light sleep and Wi-Fi modem power saving are enabled; the application still polls
the ADC keys every 50 ms. This is connected standby, not deep sleep or shutdown:
the current page, RAM conversation and saved configuration survive. Any function
key wakes the display; that entire press is consumed, so it cannot accidentally
record, submit or change settings. The next press performs the displayed action.

Recording, calls, sending/cancelling, gateway work, settings pages, active
configuration and an enumerated USB host keep the display awake. A BLE connection
or configuration command wakes it. USB identification and read-only diagnostics do not continually reset
the idle timer. USB commands hold power locks while executing; an enumerated USB
host also prevents automatic light sleep through ESP-IDF's USB safety lock.
The `POWER` diagnostic reports screen standby, idle time and USB-host presence,
without returning credentials or microphone data. Power locks are not a current
measurement. The board does not expose reliable charger status through its BSP;
USB-host presence and battery percentage are not treated as charging detection.
Ordinary wall chargers cannot be detected this way, so their screen timeout remains
the same as battery operation; charging is not interrupted by screen sleep.
Battery-only sleep current and runtime require physical measurement.

Validation checkpoint (2026-11-09): the full repository gate and standby policy,
power-lock ownership and display rollback tests passed. Application image SHA-256
`8bdd6e7d2fb210fea88e36a7a004ac938d35209c3028c766026332434be11402`
and matching ELF `ce6888d41b294fc135f62bc93e3b997bcdd73d4efaf4460d2fbd558fc4ad0b57`
were verified and flashed application-only, preserving stored configuration.
A bounded 74-second USB-host test stayed awake beyond the timeout, retained the
configuration generation and profile counts, passed heap integrity with no
allocation failures, and navigated settings/home. Display standby and wake-only
first input were exercised on the preceding build before adding the USB-host
inhibit. Final battery-only sleep, physical wake, audio and power draw remain
unverified; USB tests cannot establish them.

### Ask-mode speech echo

The plugin captures OpenClaw's existing audio transcription through public prompt
hooks. Matching firmware displays the question and answer in chronological order,
with the user's messages right-aligned in mint and the assistant's left-aligned
in warm white. Captions use “You” and the configured assistant name, falling back
to “Assistant” when unnamed. Long names are ellipsized without reducing the body
font size. At most three recent turns remain in device RAM.
Continue adds a new turn without replacing earlier messages; page keys read long
conversations. The reader opens at the latest turn and follows new text while you remain
on the latest page; after you page back, it does not take control of the page again. History is not persisted, is cleared on restart or assistant
change, and is not the gateway's conversation context. Retry updates the same turn;
cancel or re-record removes the unfinished turn. Status/error text is not stored
as an answer. This does not run a second ASR or
ask the answer model to reconstruct the recording. `passport.transcript` uses
native device authentication and session access checks, matching the session,
request and run IDs rather than returning the latest transcript from any turn.
At most 32 short transcripts (479 UTF-8 bytes each) remain in process memory for
up to ten minutes; they are not persisted or logged by the plugin. New recordings
and cancellation clear the current input echo, not completed turns. Older gateways without this method
continue normal Ask operation without the echo.

Ask transcription is performed by the OpenClaw gateway's audio model, not on the
Passport. For Chinese input, explicitly lock the language and give the transcriber
a spelling/context hint; automatic language detection can misclassify short Chinese
utterances, and proper names are especially prone to homophone errors. Add this to
the OpenClaw configuration:

```json5
{
  tools: {
    media: {
      audio: {
        enabled: true,
        language: "zh",
        prompt: "Accurately transcribe spoken Mandarin. Preserve person names, place names, product names, and English proper nouns; do not translate or rewrite."
      }
    }
  }
}
```

Or set the two values from the command line:

```sh
openclaw config set tools.media.audio.language zh
openclaw config set tools.media.audio.prompt 'Accurately transcribe spoken Mandarin. Preserve person names, place names, product names, and English proper nouns; do not translate or rewrite.'
```

Restart the gateway and run the plugin `doctor`; `askAudio.language` should be `zh`
and `askAudio.promptConfigured` should be `true`. This only affects Ask transcription;
it does not put credentials on the Passport or change the realtime voice model used by
Call. If errors remain, inspect the gateway's selected audio model and logs before
changing the screen or reflashing the firmware.

Ask prepares the authenticated gateway connection as soon as recording starts.
On paired devices, audio is uploaded as it is recorded; the final request is
submitted only after recording stops. After at least 240 ms of detected speech,
about two seconds of silence automatically triggers the same Send action as the
button. Initial silence, a brief click, and the codec's startup transient do not
trigger silence submission. This is a lightweight local energy detector, not ASR;
background speech or loud noise can affect it. Cancel and re-record invalidate
queued auto-send events. A held recording key defers automatic submission until
release. Recording keys retain their original action and capture identity, so a
late Send cannot become Cancel after automatic submission. Call mode is unchanged,
and manual Send remains available.

Wi-Fi and assistant settings show a list of the available choices rather than
one item at a time. The cursor highlights the browsed choice; connection or
in-use status appears below the list. Browsing does not change the active
network or assistant. Wi-Fi includes an explicit Add row; longer lists follow
the cursor with a position counter. Long names are ellipsized without shrinking
the font. These lists reuse the chat row widgets rather than allocate another
set of LVGL objects.

Enable the plugin's conversation hooks once with
`openclaw config set plugins.entries.ai-passport-openclaw.hooks.allowConversationAccess true`,
then restart the gateway. This permission allows reading conversation content;
the implementation only captures audio-transcript prompts in Passport sessions.
The transcript RPC requires `operator.write` and the authenticated profile, as
required by OpenClaw's session-participation policy; it does not modify sessions.

Version 0.3.0 replaces the installed-core compatibility patch with an independent
plugin: `passport-realtime` provider, `/passport/v1` WebSocket transport and the
authenticated `passport.status` RPC. Runtime code uses public plugin SDK imports,
not generated filenames, private exports or edits to OpenClaw installation files.
OpenClaw 2026.10.5 is the tested baseline; future compatibility must be checked.

Call interruption keeps the authenticated WebSocket and conversation open. The
top key mutes/unmutes the microphone, the middle key interrupts speaking or a
pending reply (and reconnects only after failure), and the bottom key ends the
call. Both button and local voice detection use native `talk.session.cancelOutput`
with `reason: "barge-in"`; no OpenClaw core patch is required. The transport drops
queued old audio/text while cancellation completes, and preserves microphone
input for the next turn. Muting disables voice interruption, not the button.
The Passport provider disables automatic upstream audio interruption: the device
owns echo-confirmed voice interruption and the interrupt button. Server VAD still
detects normal turn endings and starts replies; it must not independently cancel
speaker output. This policy is local to Passport calls, not other clients.
Gateway diagnostics retain bounded response status/reason/error-code fields,
not transcript text, endpoint URLs or credentials.
The top key can mute while connecting; audio startup retains that choice and
muted input is sent as silence, without silencing the assistant's speaker.
Connecting displays “Connecting”, not a recording prompt. The bottom key can
end both a connecting and an active call. Physical press/release events are
queued by the button driver independently of screen/network polling and produce
one action on release, including after a long hold. The first wake-up press is
consumed without triggering a call action.

The C3 uses separate bounded input/output rings and a small speaker-reference
history. Sustained microphone activity not strongly correlated with recent
speaker output starts a brief silent playback probe; only speech that persists
after the echo-settling interval interrupts. The last 32 ms of confirmed speech
are retained before resuming capture. This is an echo-aware detector, **not full
acoustic echo cancellation** or continuous full-duplex transcription. Loud noise,
room reverberation, placement and volume still need physical acceptance;
synthetic tests do not establish real-world accuracy. A rejected candidate can
briefly dip playback without ending the reply. Extra buffers exist only during
Call, never alongside Ask recording.

The provider exposes a call-local `passport_end_call` tool. Explicit requests such
as “hang up” or “I do not want to chat anymore” close the current voice session,
stop audio admission and use native session cleanup to retire its pending work.
They do not shut down the gateway or affect other calls. Task cancellation,
muting, quotations and negated hangup requests are not hangup commands; the voice
model selects the tool, rather than a transcript keyword match. Updated firmware
treats a completed call as normal, releases audio and returns to the home screen.
Unexpected disconnects still show a retryable error. Reopen existing calls after
updating the plugin; update the firmware for the normal home-screen transition.

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
cd /path/to/ai-passport-agent/tools/openclaw
npm ci --omit=peer --ignore-scripts
openclaw plugins install --link --accept-capabilities /path/to/ai-passport-agent/tools/openclaw
node doctor.mjs
openclaw gateway call passport.status --json --params '{"agentId":"passport"}'
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
actual agent ID for `passport` in the status example.

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

### Local interruption checkpoint (2026-11-09)

Button and echo-aware voice interruption pass host tests, including delayed echo,
double-talk, mute, repeated cancellation, stale output, input-ring wrap/partial
reads and cancellation acknowledgement. Audio/detector tests also pass
AddressSanitizer/UndefinedBehaviorSanitizer; zh/en glyph/layout tests pass.
The complete gate verified local merged image
`ac7286f1a9b4411c50e99b0674e78cd1ddbb83ef943dfefcc848e20107f45e68`.
The application is 3,392,128 bytes with 15,744 bytes left in its existing
partition; use the matching local manifest for application and ELF hashes.
The existing gateway plugin was updated, restarted and passed the doctor and
provider-registration checks without core edits. This build was flashed as an
application-only update at `0x10000`; the running ELF prefix matches
`4d3aa6ec71b4c035`. Configuration generation 79, two saved networks, two agents
and Chinese UI were retained. Startup, Wi-Fi, heap integrity and USB-host
stay-awake checks passed. An immediate synthetic Call attempt was rejected while
Wi-Fi was still joining; after waiting for Wi-Fi readiness, a 20-second synthetic
Call completed with zero input drops, allocation failures or observed resets.
Its minimum free internal heap was 11,420 bytes and post-test free heap was
64,684 bytes. This test used synthetic input, not the microphone, and received
no reply audio: real button/voice interruption, acoustic accuracy, speech onset
quality, playback, CPU/heap margins under real replies and interruption latency
remain unverified.

### Local Call controls checkpoint (2026-11-09)

The complete gate and zh/en screen tests pass for merged image
`a68eb35ee04d07f1e73481ba3639e4ed3adfb20dc0565272abfb4412973d9752`,
application 3,392,400 bytes, ELF prefix `215b836c26dd6342`. The application-only
update at `0x10000` was flashed and the running identity matched; configuration
generation 79, two networks, two agents and Chinese settings were retained.
Host tests cover queued press/release handling, wake suppression, mute before
audio startup, silent muted input, unmute, cancellation before worker dispatch
and cleared Call controls after cancellation. Audio and key tests also pass
AddressSanitizer/UndefinedBehaviorSanitizer.

Device tests used USB-injected keys and synthetic input, not physical button
presses or microphone audio. Mute selected before audio startup remained active;
311 input chunks were suppressed with zero input energy, then unmute resumed
input. Ending an active Call returned home in about 0.84 seconds; ending a newly
connecting Call returned home in about 2.7 seconds including cleanup. Heap
integrity passed with zero allocation failures, input drops or observed resets;
minimum internal free heap was 24,836 bytes during this bounded test. Physical
key reliability, microphone privacy under real capture, reply playback, acoustic
interruption and behavior under stalled external networks remain unverified.

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
  wire. A working or partial result must not retire the passport's pending consult:
  its later chat completion still needs to be submitted to the voice provider.
  Other duplicated tool metadata remains omitted. This gateway-only correction
  requires reopening the call, not reflashing the passport.
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

OpenClaw can generate output faster than the passport plays it. Without pacing,
the small playback ring stalls reading the shared WebSocket; acknowledgements
behind queued output audio can reach the ten-second upload deadline even though
the gateway is healthy. Increasing that deadline or the passport heap does not
remove the head-of-line blockage. The passport has a 10,336-byte output ring
(215 ms PCM, 160 ms prefill), or 42 fixed
246-byte IMA slots: 0.84 seconds with bounded 640 ms prefill. Batched frames
decode through the existing 960-byte gateway scratch. Playback needs a local
246-byte block scratch. A separate 3,624-byte compressed capture ring (27
256-sample IMA blocks, 288 ms) and bounded
speaker-reference/detection state permit local interruption without overwriting
playback. This adds about 4 KB of Call-only heap, with unchanged task stack sizes.
PCM input is decoded when pulled; compressed wire input is then encoded in its
negotiated framing. This introduces an additional lossy codec pass; microphone
quality and CPU/heap margins require device acceptance.
Upload uses spare space in its existing allocation before a single
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
output ring, scratch space and task stacks do not grow during rebuffering. Deterministic audio
tests cover a 5 ms refill without rebuffering and a 60 ms interruption with
bounded rebuffering; physical speaker continuity remains a device check.

Voice is configured on the gateway, not compiled into the passport. For example,
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
