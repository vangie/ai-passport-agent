<p align="right"><a href="readiness.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# v0.7.5 release readiness

2026-11-16: the user authorized merging and releasing v0.7.5, including the automatic power-off fix and the update of existing community play 2236.

## Required checks

- Align plugin/web versions, bilingual notes and changelogs.
- Run repository/host gates, complete firmware build/archive verification, web unit/browser tests, plugin packaging and privacy checks.
- Push immutable tag `v0.7.5`, wait for successful Release and verify all downloaded assets and source provenance.
- Wait for Pages and verify its firmware matches Release.
- Query community play 2236 before submission and stop on any pending or draft conflict.

## Change and basis

v0.7.4 added device-side idle settings and automatic power-off. On the maintainer device, automatic power-off entered deep sleep but a standby timer wake source remained armed, causing an immediate timer wake and apparent reboot. v0.7.5 clears all wake sources before arming the function-key wake source.

The fix was reproduced with shortened timers: the device entered deep sleep and stayed off for more than 150 seconds, then remained off for about 27 minutes until a physical key woke it. The normal settings were restored to 60 seconds / 1800 seconds afterwards.

## Validation

- The complete local gate passed with the Hermes test environment and Homebrew mbedTLS path configured.
- The automatic power-off contract test covers wake-source ordering and the regression case.
- Device validation covered the diagnostic reproduction, key wake, restored settings, normal boot, Wi-Fi and saved configuration.
- Physical-key and long-term battery behavior should still be checked on each hardware revision.

## Community

The existing play is **2236**, titled **Agent Talk**. The v0.7.5 update requires a fresh target-bound `update_once` grant. Submission status and revision will be recorded here after the upload.

## Receipts

- GitHub tag: `v0.7.5` at commit `8fb2e9e8013ecef5cc654f1a57a002a6542ab46d`.
- Release: https://github.com/vangie/ai-passport-agent/releases/tag/v0.7.5
- Verified full-image SHA-256: `8f36693c27e250120108427e61c6d575612ba9c7444f119319f3e1f486535930`.
- Pages manifest reports v0.7.5 and the same commit; its full and application images match the Release.
- Community play 2236 update: revision **5782**, status **pending review**. Public revision remains 5707.
