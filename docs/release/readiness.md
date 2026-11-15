<p align="right"><a href="readiness.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# v0.7.4 release readiness

2026-11-15: the user authorized merging and releasing v0.7.4. Scope is shared firmware, web studio and both backend plugins; native apps remain source-only.
This release changes the firmware only: the screen-off and power-off times can be set under Settings on the device. Both plugins are unchanged apart from their version.

## Required checks

- Align plugin/web versions, bilingual notes and changelogs.
- Run repository/host gates, complete firmware build/archive verification, web unit/browser tests, plugin packaging and privacy checks.
- Push immutable tag `v0.7.4` at the release commit, wait for successful Release and verify all eight downloaded assets and source provenance.
- Wait for Pages and verify its firmware matches Release and old versions remain recognizable.

## Change and basis

Since v0.7.3 the two idle times could be changed only in the web studio. This release brings the same steps to the device:
the Settings list grows from four items to six and scrolls, and each new item has an adjustment page with the key meanings shown beside the keys.
The steps and the ordering rule live in one definition shared by the firmware and its interface, and a host test checks that every pair of steps
the device can reach is one the firmware accepts.
The headline font gained the characters of the two new headlines; it was regenerated from the same source font with the same options, and
regenerating it before the addition reproduced the committed file byte for byte.

## Hardware and limitations

A local candidate (device build identifier `525fbabf5a058e0b`) was application-flashed to the maintainer device; three Wi-Fi networks and
three assistants were retained, and boot and networking were normal.
USB key commands walked the device through opening the Settings list, the screen-off page and the power-off page, stepping, and saving on leaving:
the active values did not change before leaving, `DEVICE` reported the new `idle_s` / `poweroff_s` after leaving, the changed values survived a
reset, and the device was restored to 60 s / 1800 s at the end.
These were key commands sent over USB, not presses of the physical keys, and the look on the real screen was checked only in the host renderer.
The notice shown when saving fails is covered by the host renderer and was not triggered on hardware.
Automatic power-off and key wake remain unobserved on hardware, as in v0.7.3: the test device stays on a computer USB connection, which holds both timers.
This is not hardware acceptance of the tagged artifact. Publishing itself does not modify personal gateway models or grants.

## Community

After the user supplied a fresh one-time grant, `whoami` confirmed `scope: update_once`, `projectId: 2236`.
A `projects` read before submitting showed revision **5674** (v0.7.3) approved and public, with no draft or pending conflict.
See [community submission](community.md).

## Previous release

v0.7.3 tag is `6ddaadd9fc061d68b9a911c3a28a3605a54c851d`; its GitHub release went public on 2026-11-15 at 06:44 UTC and Pages was synchronized.
Its merged image SHA-256 is `bb42e850edbb2345d93cde85a09bfece183ba952afe92eda5d9294a21cf57354`.
Community revision **5674** (v0.7.3) is approved and public.

## Current receipts

The local complete gate passed: repository and host tests, web unit and browser tests, complete firmware build and archive verification.
The added key-flow test fails when the step limits are removed.
Four Hermes tests requiring unavailable native imports still skip locally.

Build: PASS. Host tests: PASS. Device tests: USB key-command test on the local candidate PASS; NOT RUN for the tagged release.
Unverified: tagged-artifact hardware acceptance, operation with the physical keys and the look on the real screen, automatic power-off and key wake,
and the community update.
