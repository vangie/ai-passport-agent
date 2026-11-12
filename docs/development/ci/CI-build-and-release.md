<p align="right"><a href="CI-build-and-release.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# CI and Release

The default delivery path is firmware, the OpenClaw plugin and the browser
configuration studio. Native apps remain in `mac/`, `ios/` and `shared/` as
source-only alternatives. CI does not build, sign, run Simulator UI tests or
package them. Their marketing versions no longer gate releases. Local development
scripts and native source unit tests remain available.

## Pull requests and main

- `static-checks.yml`: repository rules, private endpoint audit, pinned Gitleaks,
  actionlint and C/Python/Swift host tests; no app build or XcodeGen.
- `firmware-checks.yml`: ESP-IDF 5.5.3 build and verified merged firmware.
- `openclaw-plugin.yml`: Node 22/24/26 locked dependencies, plugin tests and actual
  npm tarball inspection.
- `web.yml`: protocol/UI tests, static site build and GitHub Pages deployment.
  Published firmware is verified and served from the same origin.

Jobs need no personal gateway, model, Wi-Fi, tunnel or Apple credentials.
Passing CI is not on-device acceptance or a live-provider test.

## Tag release

Tags use `v<semver>` (for example `v0.5.1`). Review bilingual changelogs and release
notes, privacy checks and hardware compatibility before tagging. Never upload
private NVS, pairing material, configuration, local logs or old local firmware.

`.github/workflows/release.yml` builds from the pushed tag:

1. Audit repository, private endpoints, secrets and workflows. The tag, plugin
   package/runtime and lockfile versions, bilingual notes and dated changelogs
   must agree. Native app versions are independent.
2. Run host checks through the reusable static workflow, without app builds.
3. Build fresh firmware, verify layout/hashes and package from its verified
   content-addressed archive. Public artifacts exclude ELF/MAP.
4. Build the plugin package using locked dependencies.
5. After audit, host tests, firmware and plugin succeed, collect five files:
   plugin TGZ, firmware TAR.GZ, application and merged BINs, and `LICENSE.txt`.
   Reject missing, empty or unsafe inputs. Add `release-manifest.json` with source
   commit, version, sizes and SHA-256 hashes, then `SHA256SUMS.txt`: seven files total.
6. Create or resume the same tag's draft, upload, download and verify the full
   filename set, provenance and hashes before publishing as latest. Failures leave
   a draft; reruns never overwrite an already published release.

No macOS ZIP, iOS source kit, Simulator ZIP or IPA is required or published.
Native code remains in the repository and GitHub's source archives. Existing
historical tag workflows are not rewritten. Retired GitHub Releases may be removed
after the replacement is verified; retain their tags and verified entries in
`web/release-builds.json` so installed firmware still has a recognizable version.
Optional `mac-resources`
and `ios-source` helpers are local tools, not CI steps.

Align plugin versions and bilingual notes/changelogs, then run
`python3 tools/release.py check-version --tag v0.5.1` with the intended version.
Run the static gate and relevant firmware/plugin/web tests, merge reviewed source
and push an immutable tag at that exact commit. Monitor Release to completion;
a tag or successful build alone is not a release. Inspect a failed draft and
rerun its failed job rather than moving a tag or creating another release for a
retry. Roll back with previous compatible artifacts, not rewritten public assets.

Actions are pinned to full SHAs. Permissions default to read-only; only publishing
receives `contents: write`. Manual dispatch validates/builds but does not publish.
The separate Web workflow deploys Pages; no workflow modifies an OpenClaw installation.

## Local native development

Use `make sim-mac` or `make sim-iphone` only for local native development.
CI needs no Apple signing identity and never imports one from a developer.
iPhone installation needs Xcode and your own signing team; see
[iOS installation](../../release/ios-install.md). Local app builds are not release
artifacts or complete-app CI checks.

Browser USB flashing needs no local Python or esptool. Command-line flashing
still requires them. The local application helper validates the image and ESP32-C3
before writing at `0x10000`; only compatible partitions preserve settings.
A merged image belongs at `0x0` and may reset configuration.

See [Release notes](../../release/notes.md), [firmware layout](../engineering/firmware-layout.md)
and the [project README](../../../README.md).
