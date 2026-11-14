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

After the user supplied a fresh one-time grant, `whoami` confirmed `scope: update_once`, `projectId: 2236`.
A `projects` read before submitting showed revision **5603** (v0.7.1) approved and public, with no draft or pending conflict.
See [community submission](community.md).

## Previous release

v0.7.1 tag is `555295fc84a0c7d9027dc838ff2f8c44773808e2`; its GitHub release went public on 2026-11-14 and Pages was synchronized.
Its merged image SHA-256 is `f946bb78be95503c4215685743a2ce8432be1207e57ff2d6608bd67f82c60451`.

## Current receipts

The local complete gate passed: repository and host tests, complete firmware build and archive verification.
The added host test fails on the previous logic and passes on the new logic.
Four Hermes tests requiring unavailable native imports still skip locally.

GitHub v0.7.2 was published on 2026-11-14 at 19:11:10 UTC:
<https://github.com/vangie/ai-passport-agent/releases/tag/v0.7.2>.
Its immutable source commit is `40e3aedbb60259649d6792a41e210234bdce658c`; all four checks on main passed for that commit before tagging.
[Release run 43630226505](https://github.com/vangie/ai-passport-agent/actions/runs/43630226505)
passed every job; all eight downloaded assets passed provenance, filename and checksum verification.
The merged image SHA-256 is `ed44c6cc5e6d5753a935672b67071f60bf602395df13f342269bcdadaa8bbd93`;
the application SHA-256 is `78c600c2e7bb3c49148207d875148175f689e67f0700b37038afd451ba742c92`.
Its device build identifier is `3b9d1c17852932fd`.

[Pages run 43630713742](https://github.com/vangie/ai-passport-agent/actions/runs/43630713742)
succeeded. The live <https://ai-passport-agent.codelife.me/> manifest reports v0.7.2 and
the same source commit; both downloaded same-origin firmware hashes match Release.
Identities for v0.7.1 and earlier remain in the live historical-build catalog.

Publisher 1.14.0 validated the exact downloaded merged firmware and cover. The interface did not change, so the cover and
three posters approved for v0.7.1 were reused and each was opened and inspected before submitting.
The approved bilingual descriptions and instructions were preserved, and this release's changes were submitted separately.
Submission returned revision **5643**, **pending review**, for existing play **2236**.
A subsequent `projects` read confirmed the same submission result, firmware hash and independent bilingual
description/instructions/changelog fields. Approved revision **5603** (v0.7.1) remains public;
v0.7.2 is not yet public in the community. The grant is consumed; do not resubmit or reuse it.

Build: PASS. Host tests: PASS. Device tests: local-candidate synthetic calls PASS; NOT RUN for the tagged release.
Unverified: tagged-artifact hardware acceptance, a long real call with several speakers, and community approval of revision 5643.
