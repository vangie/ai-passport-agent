<p align="right"><a href="readiness.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# v0.7.3 release readiness

2026-11-15: the user authorized committing and releasing v0.7.3. Scope is shared firmware, web studio and both backend plugins; native apps remain source-only.
This release carries idle power settings, the home screen layout and two call fixes in the OpenClaw plugin.

## Required checks

- Align plugin/web versions, bilingual notes and changelogs.
- Run repository/host gates, complete firmware build/archive verification, web unit/browser tests, plugin packaging and privacy checks.
- Push immutable tag `v0.7.3` at the release commit, wait for successful Release and verify all eight downloaded assets and source provenance.
- Wait for Pages and verify its firmware matches Release and old versions remain recognizable.

## Fault and basis for the fix

After v0.7.2 was released, an OpenClaw call on 2026-11-15 at 12:04 again ended after about 50 seconds with "the gateway cannot reach the realtime service".
The gateway log shows the same timing as the call on 2026-11-14 at 17:41: about 3.5 seconds after the upstream reported the reply as
finished (`response.done completed`) the device sent an interruption; one second later the gateway logged that the upstream had not
confirmed output cancellation within 1000 ms, and exactly 30 seconds later it logged `session.error` and `session.closed`.
The cause lies between the gateway and the upstream: once a reply has been generated and is only still playing on the device, the upstream
has no reply to cancel and sends no terminal event, and the gateway fails the session after `STALE_OUTPUT_FENCE_MAX_MS` (30 seconds).
v0.7.2 fixed a different path, firmware treating a stream-phase error as a lost connection, and has no effect on this one.
The Passport provider now tracks the upstream's reply lifecycle and settles an interruption itself when no reply is being generated.

In the same call a three-character Chinese utterance was answered entirely in English. The gateway's own call instructions are in English
and the Chinese requirement came only at the end; this release adds a line stating the reply language at the very start.

## Hardware and limitations

A local candidate was application-flashed; three Wi-Fi networks and three assistants were retained, and boot and networking were normal.
The device reports the `power-config` capability and `DEVICE` returns `idle_s=60 poweroff_s=1800`.
The test device stayed on a computer USB connection, which holds the screen-off and power-off timers: automatic power-off and key wake
were **not** observed on hardware.
The home layout is covered by pixel tests and the host renderer and the local candidate was flashed; whether it looks right is for the user to judge on the device.

Both OpenClaw plugin fixes were deployed to the maintainer gateway (restarted on 2026-11-15 at 13:21), with the previous files backed up.
A direct call to that gateway with synthetic speech: an interruption 1.5 seconds after a reply finished returned in 8 ms and the call was
still open 34 seconds later; an interruption while a reply was being generated was still cancelled by the upstream and returned in 427 ms;
the log has no unconfirmed cancellation and no session error since the restart.
A 20-second synthetic call from the device passed with `errors=0`.
Reply language was measured directly against the voice model with rebuilt gateway instructions, not instructions captured from a live call:
with one-syllable prompts, 4 of 50 replies were English under the old instructions and 0 of 50 under the new ones.
A real spoken call with voice interruption, an interruption during an agent consult and a long call with several speakers remain unverified.
The Hermes plugin is unchanged apart from its version.
This is not hardware acceptance of the tagged artifact. Publishing itself does not modify personal gateway models or grants.

## Community

After the user supplied a fresh one-time grant, `whoami` confirmed `scope: update_once`, `projectId: 2236`.
A `projects` read before submitting showed revision **5643** (v0.7.2) approved and public, with no draft or pending conflict.
See [community submission](community.md).

## Previous release

v0.7.2 tag is `40e3aedbb60259649d6792a41e210234bdce658c`; its GitHub release went public on 2026-11-14 at 19:11 UTC and Pages was synchronized.
Its merged image SHA-256 is `ed44c6cc5e6d5753a935672b67071f60bf602395df13f342269bcdadaa8bbd93`.

## Current receipts

The local complete gate passed: repository and host tests, web unit and browser tests, complete firmware build and archive verification.
The added interruption test fails with the fix disabled and passes with it; the home centring test fails when the ring is moved by 4 pixels.
Four Hermes tests requiring unavailable native imports still skip locally.

Build: PASS. Host tests: PASS. Device tests: local-candidate synthetic calls PASS; NOT RUN for the tagged release.
Unverified: tagged-artifact hardware acceptance, automatic power-off and key wake, a call with real voice interruption, and the community update.
