<p align="right"><a href="readiness.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# v0.7.1 release readiness

2026-11-14: the user authorized a GitHub release and community update. Scope is shared firmware, web studio and both backend plugins; native apps remain source-only.

## Required checks

- Align plugin/web versions, bilingual notes, changelogs and community materials.
- Run repository/host gates, UI preview, complete firmware build/archive verification, web unit/browser tests, plugin packaging and privacy checks.
- Push immutable tag `v0.7.1` at the release commit, wait for successful Release and verify all eight downloaded assets and source provenance.
- Wait for Pages and verify its firmware matches Release and old versions remain recognizable.
- Community updates target only play 2236. Obtain valid authorization and read the current public revision and pending/draft state before submitting complete materials.

## Hardware and limitations

Local candidates were application-flashed and checked for boot, networking and zero allocation failures; the user confirmed the visible update.
This is not hardware acceptance of the tagged artifact or universal network/noise/ASR quality validation.
Publishing itself does not flash/restart the device or modify personal gateway models or grants.

## Previous release

v0.7.0 tag is `4a41111c0df58032c8499f96c650aaa49b847cec` and its GitHub release is public.
Before the v0.7.1 update, a fresh authorized read confirmed community revision 5548 was approved and public, with no newer draft or pending revision.
Its one-time grant was consumed and cannot be reused.

## Current receipts

Local complete gate, 22 web unit tests, desktop/mobile browser tests, both plugin packages, privacy scan and UI regression passed.
The local application is 3,210,848 / 3,407,872 bytes, leaving about 5.8% Flash space, not runtime RAM.
Hermes tests requiring unavailable native imports still skip locally; this is not a new real-Hermes-runtime verification.
Pages staging accepts previous firmware asset names so the rename does not break staging v0.7.0 during release preparation.

GitHub v0.7.1 was published on 2026-11-14 at 09:01:59 UTC:
<https://github.com/vangie/ai-passport-agent/releases/tag/v0.7.1>.
Its immutable source commit is `555295fc84a0c7d9027dc838ff2f8c44773808e2`.
[Release run 43578218698](https://github.com/vangie/ai-passport-agent/actions/runs/43578218698)
passed every job; all eight downloaded assets passed provenance, filename and checksum verification.
The merged image SHA-256 is `f946bb78be95503c4215685743a2ce8432be1207e57ff2d6608bd67f82c60451`;
the application SHA-256 is `767feda404e2ec650a78960fa2c04eadf50e285b245befd702ceaa1f4a28079e`.
Its device build identifier is `8be6be67b02a8882`.

[Pages run 43578592280](https://github.com/vangie/ai-passport-agent/actions/runs/43578592280)
succeeded. The live <https://ai-passport-agent.codelife.me/> manifest reports v0.7.1 and
the same source commit; both downloaded same-origin firmware hashes match Release.
The v0.7.0 identity remains in the live historical-build catalog.

The final 3:4 cover and all three optional posters were opened and inspected after rendering.
Publisher 1.14.0 validated the exact downloaded merged firmware and cover.
The user supplied a fresh one-time grant; `whoami` confirmed `scope: update_once`,
`projectId: 2236`. The approved bilingual descriptions were preserved, instructions were
updated for manual Bluetooth setup, and this release's changes were submitted separately.
Submission returned revision **5603**, **pending review**, for existing play **2236**.
A subsequent `projects` read confirmed the same submission result, firmware hash and
independent bilingual instructions/changelog fields. Approved revision **5548** remains public;
v0.7.1 is not yet public in the community. The grant is consumed; do not resubmit or reuse it.

Build: PASS. Host tests: PASS. Device tests: NOT RUN for the tagged release.
Unverified: tagged-artifact hardware acceptance, long calls, provider/network-dependent latency
and recognition quality, and community approval of revision 5603.
