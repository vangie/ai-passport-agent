<p align="right"><a href="readiness.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# v0.7.2 release readiness

2026-11-15: the user authorized committing and releasing v0.7.2. Scope is shared firmware, web studio and both backend plugins; native apps remain source-only.
This is a call reliability fix with no interface or setup changes.

## Required checks

- Align plugin/web versions, bilingual notes and changelogs.
- Run repository/host gates, complete firmware build/archive verification, web unit/browser tests, plugin packaging and privacy checks.
- Push immutable tag `v0.7.2` at the release commit, wait for successful Release and verify all eight downloaded assets and source provenance.
- Wait for Pages and verify its firmware matches Release and old versions remain recognizable.

## Fault and basis for the fix

On 2026-11-14 at 17:41 an OpenClaw call of about 90 seconds ended abnormally with "the gateway's realtime voice service failed".
Device diagnostics showed `step=ready-timeout`, `voice_interrupts=4`, a minimum free heap of 13,776 bytes and zero allocation failures.
The gateway log shows an upstream error at the fourth voice interruption; the gateway emitted `session.error` and the session kept accepting audio.
The device then stopped uploading, and the gateway recorded `session.closed` 30 seconds later.
Earlier firmware treated a stream-phase error during a call as a lost connection and muted the microphone, while the only recovery
conditions were a new reply or transcript event, which cannot occur with the microphone muted.
The gateway did not log the upstream error text; "a rejected interruption request" is inferred from timing.

## Hardware and limitations

A local candidate was application-flashed; three Wi-Fi networks and three assistants were retained, and boot and networking were normal.
Synthetic 20-second calls passed twice on OpenClaw and once on Hermes with no dropped frames, no allocation failures and `errors=0`.
Synthetic calls produce no upstream errors and carry no real speech; a long real call with several speakers has not been repeated.
This is not hardware acceptance of the tagged artifact.
Maintainer hosts were updated: the OpenClaw plugin reports 0.7.1 with error-category diagnostics after a gateway restart at 02:21 on
2026-11-15, and the Hermes plugin reconnected every channel after a restart at 02:56. Previous files were backed up on both hosts.
Publishing itself does not flash/restart the device or modify personal gateway models or grants.

## Community

Revision **5603** (v0.7.1) of community play **2236** was pending review when submitted; its current state was not read for this release.
There is no fresh one-time grant, so v0.7.2 is **not submitted to the community**. A later submission needs a new grant and a read of
the actual state first, and must stop on a draft, pending revision or version conflict.
See [community submission](community.md), which still describes v0.7.1.

## Previous release

v0.7.1 tag is `555295fc84a0c7d9027dc838ff2f8c44773808e2`; its GitHub release went public on 2026-11-14 and Pages was synchronized.
Its merged image SHA-256 is `f946bb78be95503c4215685743a2ce8432be1207e57ff2d6608bd67f82c60451`.

## Current receipts

The local complete gate passed: repository and host tests, complete firmware build and archive verification.
The added host test fails on the previous logic and passes on the new logic.
Four Hermes tests requiring unavailable native imports still skip locally.

Build: PASS. Host tests: PASS. Device tests: local-candidate synthetic calls PASS; NOT RUN for the tagged release.
Unverified: tagged-artifact hardware acceptance, a long real call with several speakers, and the community update.
