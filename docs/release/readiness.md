<p align="right"><a href="readiness.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# v0.7.0 release readiness

Formal release authorized on 2026-11-13. The evidence below records pre-release
checks, not a publication receipt. See [release notes](notes.md) and
[community copy](community.md).

## Scope

Shared firmware, browser configuration, OpenClaw plugin and the new external
Hermes plugin. No native-app release. Position this as voice access to an existing
personal agent; do not promise a universal backend or agent execution on every Call turn.

## Final acceptance

The maintainer confirmed the two outstanding device/performance acceptance groups
and requested a stable GitHub release and community update. This is user-reported
acceptance, separate from the bounded automated checks below. The current binary
passes archive/layout verification at 3,407,408 / 3,407,872 application bytes.
The immutable tag build must pass the same checks before publication; the remaining
464 bytes are Flash capacity, not free runtime RAM. Existing partition boundaries
and saved-device configuration must not be changed for this release.

Community authorization is scoped to update project 2236. The latest readback
confirmed approved public revision 5492, no pending/draft and no submission result.
Do not reuse this grant for a later version or report pending review as published.

The final local release gate passed after refreshing the bilingual release copy:
repository/host tests, isolated firmware build and merged-image verification,
22 web unit tests, desktop/mobile browser checks and the redacted secret scan.
The four completed 3:4 posters were opened and checked individually; only fictional
networks, assistant names and example addresses are shown. This remains preparation;
the tag workflow and community API provide the publication/review receipts.

## Checked on 2026-11-13

- GitHub latest is still v0.6.0; no v0.7.0 has been published.
- Full repository/host/firmware gate passed. Local Hermes protocol suite ran 16
  tests with four native-runtime skips; the same 16 tests and 12 voice/codec tests
  subsequently passed on a real Hermes v0.23.1 installation with zero skips. No production
  service was restarted and these tests did not call a paid model.
- Release packaging/version tests: 15 passed. Web: 22 unit tests plus desktop/mobile
  browser acceptance passed. OpenClaw plugin, relay, migration and package tests passed.
- Source/privacy/secret audits and actionlint passed. An isolated production web build
  using the actual public v0.6.0 firmware passed the exact-artifact audit. Local preview
  catalogs intentionally fail that production audit and must not be uploaded.
- The 3:4 cover and three optional posters were rendered and inspected; the publisher's
  format/full-image validation passed. Neither validates real-device behavior.
- Verified local full image SHA-256:
  `66e4977ef8b0f8091aeac9742142c64c341d3d2d0874c9d039c15e355410e878`.
  Matching ELF SHA-256:
  `0ad0d502beba5394c33e3f64501356b00bcd5375880189272c6b2eac8b200301`.
  Its descriptor is `v0.6.0-5-g0504358-dirty`, not a tagged v0.7.0; rebuild from the
  authorized release commit/tag. Do not publish this as an immutable release asset.

## Before tagging

- Call playback and connecting-screen regressions were reported after the checks
  above. The local fix removes destructive probe muting, keeps echo checks active
  during speech confirmation and invalidates the full Call layout on transition.
  PCM/IMA preservation, echo/double-talk and bilingual layout host checks pass;
  the complete gate passes on the corrected candidate. Latest full-image SHA-256:
  `0be0ecbccfa0ecf79baa6588eed768863412558f0c9cc8ee746c4cda4fa3e3d2`;
  ELF SHA-256: `861d562e28454f5b3343672990187a2d273d16a39b98bc5b41d06c91d9afc774`.
  This superseded the earlier build, with 944 bytes of application flash remaining.
  On 2026-11-13, the connected Passport's partition table matched the verified
  bundle. The application-only image was flashed at `0x10000` and its write hash
  verified; NVS and identity partitions were not written. The device reports build
  `861d562e28454f5b`, retains three networks and three agents, reconnects to Wi-Fi,
  and enables BLE advertising. A bounded 24-second startup observation showed no
  panic/watchdog; heap integrity passed with zero recorded allocation failures.
  Serial opening caused a USB reset, so earlier pre-network diagnostic snapshots
  are not evidence of connection failure. The serial port is released.
  That startup-only check did not cover Call entry: a subsequent `KEY DOWN`
  reproduction confirmed a main-task stack-protection panic and reboot. The
  synchronous `lv_refr_now()` call ran rendering on the application task. It has
  been removed; full-screen invalidation now wakes the existing LVGL display task.
  A regression test forbids synchronous renderer/timer calls from `redraw()` and
  fails on the faulty implementation. The complete gate and simulator pass.
  New full-image SHA-256:
  `3affb71cd35771d0c1ec505fd66e28818def777ea8fca6ebc4e22706b70118bd`;
  ELF SHA-256: `1eef3ef85c746236e90311a4adea8daeac7faa02cfda92dc299d9d6efa1cf2ef`.
  This supersedes the crashing candidate, with 1,104 bytes of application flash
  remaining, without increasing task stacks or changing the partition layout.
  The replacement application was flashed at `0x10000` with write-hash verification
  and the same compatible partition table. A 75-second serial acceptance session
  matched build `1eef3ef85c746236`, retained three networks/agents, entered Call
  twice, reached LISTENING/capture both times, confirmed mute in audio diagnostics,
  and ended both calls back at Home. Only the initial USB-open reset was observed;
  no subsequent boot, panic/watchdog, heap corruption or allocation failure occurred.
  The serial port is released. This verifies the entry/re-entry crash repair, not
  physical screen appearance, intelligible playback or voice interruption quality.
  Do not tag or submit until OpenClaw/Hermes playback, speech/button interruption,
  mute, connection rendering and hang-up have been accepted on the device.
- A subsequent user report describes replies stopping early and returning to
  LISTENING. Removing probe muting exposed weak echo matching. The speech fixture
  reproduces false interruption before the repair; five delayed/filtered echo
  cases now produce zero interruptions, and all five mixed-speech cases still
  permit interruption. Address/undefined-behavior sanitizers and the complete
  host/firmware gate pass. Voice and button interruption counters distinguish
  cancellation from normal playback completion without recording audio content.
  Latest verified full-image SHA-256:
  `f7398714de69555b100b4a02c6c3fedb24327c168c730fa7e7197ae4110af460`;
  ELF SHA-256: `f502a7151d6f05104ec4cc7cb7882387c6ed38d048ebd1d8cc7ecfc92f442ef4`.
  This supersedes the previous candidate; application flash headroom is 768 bytes.
  Host reproduction is not physical acceptance of the user's spoken interaction.
  The verified application was flashed at `0x10000` with write-hash verification;
  NVS and identity partitions were not written. A bounded 70-second serial session
  confirmed build `f502a7151d6f0510`, three retained networks/agents, Wi-Fi
  reconnection, Call reaching LISTENING, mute/unmute and exit to Home. No panic,
  heap corruption or allocation failure was recorded. No reply audio was played
  during this check, so it does not validate physical echo rejection or playback
  quality. The port is released. A trailing-space assertion in the temporary
  acceptance script was corrected and the same saved recording rechecked without
  another device reset. The final host/static gate passed after extending the
  mixed-speech tests.
- The next candidate adds cursor-preserving quiet confirmation: actual playback
  pauses before a 100 ms echo guard, and unconfirmed probes resume within 350 ms.
  The full host/firmware gate and sanitized audio/barge tests passed. Verified full
  image: `741e1f05e1e840bc142912d3d6b6e5dc125023fb201a1d53a8c567d9f2e1cf84`;
  ELF: `acf615e2bac7695dc9a396d4736e8df85bce0c024ff166d1a49bd531c5e7ae8e`.
  This supersedes the threshold-only build; application flash headroom is 496 bytes.
  Its verified application was flashed at `0x10000`, preserving NVS and identity.
  An initial acoustic test played about 19 seconds with zero local interruptions,
  then received an upstream service error and failed; it was not acceptance.
  Upstream logs showed cancelled turns despite zero device interruption requests.
  The external OpenClaw plugin now disables upstream automatic audio interruption
  while keeping server turn detection and explicit device cancellation. Tests cover
  configuration isolation, explicit interruption, callbacks and redacted diagnostics.
  Only its two maintained runtime files were updated on the OpenClaw host, with a
  private rollback backup and service restart; no core files or credentials changed.
  A subsequent bounded 108-second acoustic check completed two question/reply turns,
  played 1,591,200 PCM bytes (about 33 seconds), recorded zero local interruptions,
  and returned Home on the end key. Both provider responses completed; no service
  error, panic or reset after USB opening was observed. The serial port is released.
  The call still recorded 53 dropped input chunks and peak ACK delay of 3,692 ms;
  zero stalls and successful completion do not establish latency or audio-quality
  acceptance. A second 87-second acoustic check confirmed mute/unmute during
  playback, exactly one middle-key interruption, a completed follow-up reply and
  return Home; no local voice interruption or playback stall was recorded. It
  dropped three input chunks, with peak ACK delay of 1,335 ms. Genuine speech
  interruption and longer calls remain unverified with this policy.
  The same competing automatic upstream interruption was removed from the Hermes
  adapter; its 13 local voice tests and a no-model-call installed-runtime check
  passed. The one external adapter file was updated with a private rollback backup;
  its existing service was restarted and its loopback listener recovered. No core,
  configuration or credential changes were made. Physical Hermes playback remains
  pending. A firmware regression test also confirms reply-phase errors fail
  promptly instead of claiming a 30-second reconnection; transient startup/stream
  recovery is unchanged. The exact original upstream error is not recoverable
  from the old generic log; bounded status/reason/code diagnostics are now available.
- The reply-error state fix passed the full host/firmware gate. Final verified full
  image: `d1fb056495301c26478226b1413adee1fe3452c1af4c18cb990ce92be0349b53`;
  ELF: `caf54c86dabc8b8c8f0b831f55b397e281070f0a8c08edd1f09425b287fa1e60`.
  Application size is 3,407,408 bytes, leaving 464 bytes in the unchanged partition.
  It was flashed application-only at `0x10000` with write-hash verification.
  A final 108-second acoustic regression matched running ELF `caf54c86dabc8b8c`,
  retained three networks and three agents, completed two question/reply turns and
  played 2,121,600 PCM bytes (44.2 seconds). Local voice/button interruptions and
  playback stalls were zero. Both provider responses completed, the end key returned
  Home, and the serial port was released; no post-open reset/panic was observed.
  The call still dropped 91 input chunks, including four counted as voiced, and
  peak ACK delay reached 3,860 ms. These remain performance/recognition risks, not
  proof of perfect latency or sound quality. The earlier mute/button check used
  ELF `acf615e2bac7695d`; deliberate voice interruption and long calls remain pending.
- Review and commit the candidate source only after authorization; confirm final date.
- Run `./tools/validate.sh`, web unit/UI tests, both plugin suites, privacy audits
  and the version/package gate. Do not treat skipped real-runtime tests as acceptance.
- Hermes v0.23.1 discovery/registration and actual-runtime adapter tests have passed.
  Re-test physical Ask, follow-up, Call, native-agent delegation, mute, interrupt,
  hang-up and reconnect on that version, plus OpenClaw regression on the same firmware.
- Check flashed firmware identity and verify defaults contain no private settings.
  The corrected build has only 464 bytes of application-partition headroom; this is
  flash capacity, not free runtime RAM. The final build must fit; larger features
  need measured size reduction or a deliberately reviewed partition migration.
- Review generated cover and all optional posters at actual size, with fictional data.
- After a tag build, verify all eight downloaded assets and receipts before publishing;
  verify same-origin website firmware and legacy-domain redirection afterward.
- Remove candidate banners only when publication succeeds. Preserve v0.6.0 history.

## Before community review

Obtain a fresh `update_once` authorization for project **2236**, verify `whoami`,
then read `projects` and `publishedRevision`. Current private status is not known.
A pending/draft revision or version conflict blocks submission; do not delete or
withdraw it automatically. Compare this release with the actual public revision.

Use the published full image, inspected 3:4 cover, both titles and separate UTF-8
description/instructions/changelog files. Validate first; submit only with explicit
authorization. Report pending review as pending, never as public.
