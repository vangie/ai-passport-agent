<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# AI Passport Agent — Hermes plugin

This is an **experimental Ask and Call platform plugin**, not a fork of Hermes and
not a second model-only chatbot. Signed Passport questions enter the native
Hermes gateway, using its configured personal agent, tools, memory, session
storage and authorization policy. Each device has its own conversation.

The existing OpenClaw integration is unchanged. Ask and Call use the existing
device protocol. Updated firmware also recognizes the authenticated Hermes
backend and displays its name and official icon; previously paired devices
migrate automatically after their next successful connection, without re-pairing.
Call uses a host-side realtime voice model for ordinary conversation and delegates
tools, current information and private context to the same native Hermes agent.
This is included in v0.7.0. v0.6.0 does not include it.

## Requirements and installation

- A working Hermes gateway with a configured model and speech recognition.
- Tested runtime: Hermes v0.22.6+481, commit
  `c314285f077e91937c995d419b9fa40af9eb4981`.
- Upgrade check: Hermes v0.23.1, commit
  `3b421578588243f920886e6fe79a6faa87ea026e`: clean core checkout, external plugin
  discovery and actual-runtime adapter tests passed. The maintainer subsequently
  confirmed physical regression acceptance for the v0.7.0 release.
- Python dependencies are declared with upper bounds in `plugin.yaml`. Let the
  Hermes package manager admit them; do not edit its managed Python environment.

Use v0.7.0's `ai-passport-hermes-0.7.0.tar.gz` and verify it against
`SHA256SUMS.txt` and `release-manifest.json`; older v0.6.0 does not contain Hermes.
The archive contains one `ai-passport-hermes/` directory, code/docs/license only.

Copy this directory to `<active-hermes-home>/plugins/ai-passport-hermes`, keeping
the complete directory together. It is outside the Hermes installation and
survives an ordinary Hermes core upgrade. Before replacing an installed copy,
stop its platform using the gateway's normal lifecycle; do not hot-reload a live
transport or open a second process on the same profile store. Device grants and
receipts live in `<active-hermes-home>/passport/`, not the plugin or core checkout.
Back up that private directory before maintenance. Core upgrades do not promise
future API compatibility: run plugin doctor and the adapter tests after upgrading,
then test Ask/Call. Keep the previous reviewed plugin package for rollback; do not
delete the grants or auto-upgrade the plugin from a floating source branch.

```sh
hermes plugins doctor <active-hermes-home>/plugins/ai-passport-hermes --ci
hermes plugins enable ai-passport-hermes
```

Add this platform to the active profile's existing `config.yaml`, preserving
other configuration:

```yaml
gateway:
  platforms:
    ai_passport:
      enabled: true
      extra:
        host: 127.0.0.1
        port: 18790
        speech_language: zh
        realtime:
          enabled: true
          api_key_env: OPENAI_API_KEY
          model: gpt-realtime-2.1
          voice: coral
          output_gain_db: 0
```

Load it through the normal Hermes gateway restart procedure when no important
task is running. This plugin does not patch, auto-restart or upgrade Hermes.
Other profiles must use different ports. The gateway still applies its own
authorization checks; only a transport-verified, paired device receives the
adapter authorization grant. Model tool approvals remain Hermes's responsibility.

Set `speech_language` to `zh`, `en`, or `""` for automatic detection. It is a
Passport-only recognition hint applied through Hermes's `pre_transcription`
hook, not a global language change. On the tested local Whisper base model,
auto-detection misidentified a short Chinese sample as English; the explicit
Chinese hint restored Chinese transcription, but some proper nouns were still
wrong. Recognition quality is not solved by transport compatibility.

Realtime voice is optional and requires an OpenAI account with access to the
configured realtime and transcription models. It incurs separate model usage.
Instead of an environment variable, `api_key_file` may name a private, non-symlink
file (0600) outside the repository. Keep the credential on the host, never on the
Passport. Realtime audio uses 24 kHz PCM at the provider boundary and the device's
bounded IMA transport; no additional firmware buffers are required. The normal
Hermes text provider and Ask speech recognition remain unchanged.

`output_gain_db` optionally boosts Call playback on the host (0–12 dB, default
0). A soft limiter protects loud peaks before device compression; audio timing,
microphone gain and the device volume setting remain unchanged. For a quiet
speaker, start with 3 dB. A limiter prevents digital overflow, not physical
speaker distortion; lower gain or device volume if playback becomes harsh.
This setting does not change other Hermes platforms or allocate more device
buffers.

Call supports two-way audio, captions, voice/button interruption, playback marks,
explicit hang-up and consecutive turns. Interruption is owned by the device's
echo-confirmed voice detector and interrupt button. Upstream VAD retains automatic
turn detection but does not independently cancel or clear playback. Tool requests
use Hermes's normal native session and approvals; the realtime model must not
invent tool results. Closing
the call cancels its pending delegation, not another device's task. Ask and Call
cannot run simultaneously on the same device. One call is limited to one hour;
playback buffering is capped at 2 MiB on the host. Synthetic device audio tests
use an owned `-synthetic` session and cannot execute tools.

## Secure endpoint and pairing

Forward an existing TLS tunnel or reverse proxy to `http://127.0.0.1:18790`,
including WebSocket upgrades. Keep Hermes's own API server and this listener
off the public interface. Use a valid trusted certificate and preserve the
`/` and `/passport/v1` paths. A raw `ws://` endpoint is allowed only on loopback
for local testing. Browser-origin WebSocket requests are refused.

Issue a new pairing code using the same profile directory as the gateway:

```sh
python3 <plugin-directory>/passport_auth.py --home <active-hermes-home> pair \
  --url wss://gateway.example
```

The JSON output has `gatewayUrl` and `setupCode`; it is compatible with the
current Web Studio pairing field. Paste the complete JSON, give the assistant
a display name such as Hermes, and save the configuration to the device.
The code expires after ten minutes and can be used once. Treat it as a secret:
do not paste it into public issues, screenshots or CI logs.

To revoke a device, use its public SHA-256 device ID:

```sh
python3 <plugin-directory>/passport_auth.py --home <active-hermes-home> revoke <device-id>
```

New requests and reply delivery on an already-open connection are also blocked
after revocation. Already accepted Hermes work may finish; revocation does not
undo a tool action. Re-pairing rotates the device credential.

## Voice, sessions and failure behavior

1. The device proves its Ed25519 identity against a fresh challenge. A one-use
   bootstrap credential becomes an individual device credential; only credential
   hashes are stored on the host.
2. Ask sends the existing fragmented 16 kHz mono mu-law recording. The adapter
   validates its size and format and corrects the provisional streaming WAV length
   before calling Hermes's configured speech recognition, without a second API key.
3. The transcript is exposed through `passport.transcript`. The resulting text
   enters a native Hermes conversation as ordinary user input, not a gateway
   slash command. Model-generated results use the existing device chat protocol.
4. Reply drafts and final text are supported through Hermes's send/edit callbacks;
   the native gateway decides whether to stream. A second question stays in the
   same per-device Hermes session. Reconnecting does not create a new conversation.
5. Recorded request IDs are deduplicated across reconnects and adapter restarts.
   An interrupted in-progress task is marked failed, not automatically executed
   again. The device can retrieve completed replies through history.

Device cancellation uses a session-scoped native Hermes `/stop`; it does not
stop the gateway or another device. A pending recognition may finish on its
worker thread after cancellation, but its text is not submitted to the agent.
Interactive tool approvals cannot yet be answered from the wearable; the
adapter does not auto-approve them.

The listener accepts four connections and four concurrent questions, one per
device. Recordings are limited to the firmware's 30 seconds. Reply snapshots
are bounded to 4 KiB and transcript previews to 479 UTF-8 bytes. Up to 128
recent request receipts are stored; terminal receipts older than 24 hours are
pruned when a new question arrives. Native Hermes keeps its own durable history.
History responses prioritize the newest replies within a 5,500-byte JSON budget,
below the firmware's receive limit; older replies remain in Hermes's session.
Transient audio is private and removed after processing. The private
`<active-hermes-home>/passport/` directory contains device grants and recent
transcripts/replies; back it up or delete it according to your privacy policy.

## Validation and remaining work

Run protocol/security tests in a disposable Python environment with the two
declared dependencies:

```sh
python3 tests/test_passport_hermes.py
python3 tests/test_passport_hermes_talk.py
```

When run inside the real Hermes Python runtime, the same suite also exercises
the actual adapter and WebSocket lifecycle. `tests/test_passport_hermes_live.py`
is an explicit opt-in test: it requires `PASSPORT_HERMES_LIVE=1`, a disposable
profile named `passport-hermes-live-*`, and a configured provider. It can incur
model usage. Never point it at a production profile.

Validated: protocol/security tests, real Hermes plugin discovery, a signed-device
loopback question, reconnect deduplication, and two real-provider questions with
conversation continuity. A synthesized Chinese voice sample also completed
recognition, transcript echo and a real-provider answer with the scoped language
hint. This is not a real-microphone accuracy test. Not yet
validated: real-microphone accuracy, acoustic echo and interruption quality,
multi-profile operation, or arbitrary future Hermes upgrades. Realtime host tests cover
two real-provider Chinese spoken turns, native-agent consultation, codec parity,
playback interruption, revocation and call teardown. Web Studio backend selection
and a separate Hermes release package are included in v0.7.0. The maintainer
confirmed the final physical regression and voice-interruption checks before release.

Physical acceptance also exercised two Chinese spoken turns through the device
microphone and speaker; the listener confirmed the arithmetic answer. A call
remained active for over five minutes with no dropped capture frames; microphone
mute prevented another input turn, and hang-up returned to Home with no failed
allocations and an intact heap. A subsequent physical call also completed native
agent delegation and spoken delivery, and remained connected after a button
interruption. The listener reported quiet output initially and harsh playback
after a 6 dB boost; the deployment was reduced to 3 dB. Subjective quality at the
lower gain still requires listener acceptance. These checks do not establish
recognition accuracy or acoustic echo/voice interruption quality in general.
