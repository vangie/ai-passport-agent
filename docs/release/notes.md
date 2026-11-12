[Simplified Chinese](notes.zh_CN.md) · **English**

# AI Passport OpenClaw v0.5.1

## Highlights

- **Browser-first setup:** install/update firmware and configure Passport at
  <https://ai-passport-openclaw.codelife.me/>. Desktop Chrome/Edge uses USB;
  compatible Web Bluetooth browsers configure wirelessly. iPhone users can use
  Bluefy for Bluetooth settings, not firmware flashing.
- **One coherent configuration page:** saved Wi-Fi, assistant profiles and their
  routes, language and volume; global Save/Discard, retained drafts after errors,
  progressive Wi-Fi scanning and an agent-assisted OpenClaw setup prompt.
- **Clear firmware updates:** Keep settings is on by default for compatible
  updates; first installation/full replacement is an explicit destructive choice.
  Firmware and dependencies are served from the website, not runtime GitHub
  download redirects. Retired release identities remain recognizable.
- **Bluetooth handover and screen refinements:** encrypted client takeover after
  current commands finish, session-scoped replies, advertising recovery, distinct
  connection icons, a full-width status row and compact aligned key labels.
- **Lean releases:** firmware and the upgradeable OpenClaw plugin only. Native
  client source is retained but is no longer built, packaged or promoted.
- Fixed a ShellCheck portability failure in the Pages artifact audit. The v0.5.0
  candidate was stopped before publication; its tag is retained, not rewritten.

## Install

Start with the [web studio](https://ai-passport-openclaw.codelife.me/) and the
[quick start](../../README.md#get-started). Fresh installations/full replacement
may reset settings; compatible application updates preserve them. Configuration
saves never reflash firmware. A new network must be reachable during first save.

Manual downloads: `FoloToy-AI-Passport.bin` is application-only at `0x10000`;
`FoloToy-AI-Passport-full.bin` is merged firmware at `0x0`. Do not interchange
these offsets. The firmware tarball includes installation parameters. Install
`ai-passport-openclaw-0.5.1.tgz` on the gateway host, not the configuration computer.

## Integrity and compatibility

Seven release assets are checked before publication with a source-commit receipt,
SHA-256 checksums and draft-upload/download verification. See `SHA256SUMS.txt`.
No personal endpoints, Wi-Fi passwords, provider keys or device credentials are
distributed. The website has no application backend; settings go to the device.

OpenClaw 2026.10.5 remains the tested plugin baseline. Call requires a configured
realtime voice provider; Ask can be used independently. Build, host and browser
tests are not physical audio, battery, radio-coexistence or every-browser acceptance.
