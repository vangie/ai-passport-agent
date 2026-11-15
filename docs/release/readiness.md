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
Publishing itself does not modify personal gateway models or grants.

After publication the maintainer device was application-flashed with the Release's `agent-talk.bin`, written after its SHA-256 was checked;
it reports build identifier `4eb73c9951984233`, and three Wi-Fi networks and three assistants were retained.
The same USB key-command test passed on the tagged firmware: both adjustment pages opened, stepped, saved on leaving and changed nothing before
leaving; the device was then restored to 60 s / 1800 s with volume and Bluetooth as before the test.
The first version of the test script assumed the Settings cursor rests on the first row, while it keeps its last position, so for a moment it
changed the volume and the screen-off time and turned Bluetooth on; those were restored and the script now navigates by the page the device reports.
That was a fault of the test script, not of the firmware.
The maintainer gateway still runs the 0.7.3 plugin, which behaves like 0.7.4, and was not replaced.
Key commands were sent over USB, not with the physical keys; this is not full hardware acceptance of the tagged firmware.

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

GitHub v0.7.4 was published on 2026-11-15 at 11:49:28 UTC:
<https://github.com/vangie/ai-passport-agent/releases/tag/v0.7.4>.
Its immutable source commit is `49d437a3d28f75caaf43443735c91173823e42ac`; all four workflows on main passed for that commit before tagging.
[Release run 43710375265](https://github.com/vangie/ai-passport-agent/actions/runs/43710375265)
passed every job; all eight downloaded assets passed provenance, filename and checksum verification.
The merged image SHA-256 is `0cdbb49856d500a4830ee6a344e11ff9ffd084d2e17cf5614679f524d076e0bd`;
the application SHA-256 is `a0dc2bb6b58b87b783bcb222a5cb8f0a04d2add61a808e1981757f1d98702a26`.
Its device build identifier is `4eb73c9951984233`.

[Pages run 43710781645](https://github.com/vangie/ai-passport-agent/actions/runs/43710781645)
succeeded. The live <https://ai-passport-agent.codelife.me/> manifest reports v0.7.4 and
the same source commit; both same-origin firmware downloads are byte-identical to Release.
Identities for v0.7.3 and earlier remain in the live historical-build catalog.

Publisher 1.14.0 validated the exact downloaded merged firmware and cover. The cover and the three posters were regenerated at this
version and each was opened and inspected before submitting.
The approved descriptions were preserved, step 3 of the instructions now points to the device's Settings for the two times,
and this release's changes were submitted separately.
Submission returned revision **5707**, **pending review**, for existing play **2236**.
A subsequent `projects` read confirmed the same submission result, firmware hash and independent bilingual
description/instructions/changelog fields. Approved revision **5674** (v0.7.3) remains public;
v0.7.4 is not yet public in the community. The grant is consumed; do not resubmit or reuse it.

Build: PASS. Host tests: PASS. Device tests: USB key-command test on the tagged firmware PASS; physical keys, automatic power-off and key wake NOT RUN.
Unverified: operation with the physical keys and the look on the real screen, automatic power-off and key wake, a call with real voice interruption,
and community approval of revision 5707.
