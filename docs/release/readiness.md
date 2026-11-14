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
The last community submission to play 2236 was revision 5548, pending review at that time. Read current state rather than assuming approval.
Its one-time grant was consumed and cannot be reused.

## Current receipts

Local complete gate, 22 web unit tests, desktop/mobile browser tests, both plugin packages, privacy scan and UI regression passed.
The local application is 3,210,848 / 3,407,872 bytes, leaving about 5.8% Flash space, not runtime RAM.
Hermes tests requiring unavailable native imports still skip locally; this is not a new real-Hermes-runtime verification.
Pages staging accepts previous firmware asset names so the rename does not break staging v0.7.0 during release preparation.

In preparation, not yet a published release or review submission. Record actual URLs, source commit, firmware hash and community status after completion.
