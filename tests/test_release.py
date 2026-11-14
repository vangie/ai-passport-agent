import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import tarfile
import unittest
from unittest.mock import patch
import zipfile


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('release', ROOT / 'tools/release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / 'firmware'
        self.source.mkdir()
        app = bytes([0xe9]) + bytes(511)
        files = {'agent-talk.bin': app,
                 'agent-talk-full.bin': bytes(0x10000) + app,
                 'flash_args': b'fixture', 'bootloader/bootloader.bin': b'boot',
                 'partition_table/partition-table.bin': b'partition'}
        self.manifest = {'schema_version': 1, 'target': 'esp32c3',
                         'flash_size_bytes': 8 * 1024 * 1024,
                         'full_bin_sha256': hashlib.sha256(files['agent-talk-full.bin']).hexdigest(),
                         'image_offsets': {'agent-talk.bin': 0x10000}, 'files': {}}
        for name, data in files.items():
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            self.manifest['files'][name] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        self.write_manifest()

    def write_manifest(self):
        (self.source / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_bundle(self):
        app = self.root / 'Passport.app'
        release.mac_resources(app, self.source)
        resources = app / 'Contents/Resources/Passport'
        self.assertTrue((resources / 'tools/flash-app.sh').stat().st_mode & 0o111)
        self.assertTrue((resources / 'build/firmware' / self.manifest['full_bin_sha256'] / 'manifest.json').is_file())
        self.assertTrue((resources / 'licenses/Noto-SIL-OFL.txt').is_file())
        self.assertTrue((resources / 'licenses/Monocypher.txt').is_file())
        self.assertTrue((resources / 'licenses/Hermes-icon-MIT.txt').is_file())
        for notice in ['WebRTC-VAD.txt', 'WebRTC-VAD-PATENTS.txt', 'WebRTC-VAD-AUTHORS.txt']:
            self.assertTrue((resources / 'licenses' / notice).is_file())

    def test_reject_tampering(self):
        (self.source / 'agent-talk.bin').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            release.mac_resources(self.root / 'Passport.app', self.source)

    def test_reject_paths(self):
        for name in ['../outside', '/outside', 'nested/../../outside']:
            with self.subTest(name=name):
                self.manifest['files'][name] = {}
                self.write_manifest()
                with self.assertRaises(ValueError):
                    release.mac_resources(self.root / 'Passport.app', self.source)
                del self.manifest['files'][name]

    def test_reject_target(self):
        self.manifest['target'] = 'esp32s3'
        self.write_manifest()
        with self.assertRaises(ValueError):
            release.mac_resources(self.root / 'Passport.app', self.source)

    def ios_inputs(self):
        names = ['ios/project.yml', 'ios/Passport/PassportApp.swift',
                 'ios/Passport/Info.plist', 'shared/PairPrompt.swift', 'shared/AppLocalization.swift',
                 'shared/en.lproj/Localizable.strings', 'shared/zh-Hans.lproj/Localizable.strings',
                 'ios/Passport/en.lproj/InfoPlist.strings', 'ios/Passport/zh-Hans.lproj/InfoPlist.strings',
                 'docs/release/ios-install.md', 'docs/release/ios-install.zh_CN.md', 'LICENSE']
        for name in names:
            source = self.root / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text('fixture')
        return names

    def test_ios_source_allowlist(self):
        names = self.ios_inputs() + ['ios/Passport/.env', 'ios/Passport/private.mobileprovision',
                                   'ios/Passport/xcuserdata/private.json', 'shared/private.json']
        for name in names:
            if name.endswith('.strings'):
                source = self.root / name
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text('"key" = "translation";')
        (self.root / 'docs/release/ios-install.md').write_text('[Chinese](ios-install.zh_CN.md)')
        (self.root / 'docs/release/ios-install.zh_CN.md').write_text('[English](ios-install.md)')
        destination = self.root / 'source.zip'
        with patch.object(release, 'ROOT', self.root), patch.object(
                release.subprocess, 'check_output', return_value='\0'.join(names).encode()):
            release.ios_source(destination)
        with zipfile.ZipFile(destination) as archive:
            prefix = 'ai-passport-agent-ios-source/'
            self.assertEqual(set(archive.namelist()), {prefix + name for name in [
                'ios/project.yml', 'ios/Passport/PassportApp.swift', 'ios/Passport/Info.plist',
                'shared/PairPrompt.swift', 'shared/AppLocalization.swift', 'README.md', 'README.zh_CN.md', 'LICENSE',
                'shared/en.lproj/Localizable.strings', 'shared/zh-Hans.lproj/Localizable.strings',
                'ios/Passport/en.lproj/InfoPlist.strings', 'ios/Passport/zh-Hans.lproj/InfoPlist.strings']})
            self.assertEqual(archive.read(prefix + 'README.md'), b'[Chinese](README.zh_CN.md)')
            self.assertEqual(archive.read(prefix + 'README.zh_CN.md'), b'[English](README.md)')
        with patch.object(release, 'ROOT', self.root), patch.object(
                release.subprocess, 'check_output', return_value='\0'.join(names).encode()):
            with self.assertRaises(FileExistsError):
                release.ios_source(destination)

    def test_ios_source_rejects_missing_and_symlink(self):
        names = self.ios_inputs()
        with patch.object(release, 'ROOT', self.root), patch.object(
                release.subprocess, 'check_output', return_value='ios/project.yml\0'.encode()):
            with self.assertRaises(ValueError):
                release.ios_source(self.root / 'incomplete.zip')
        source = self.root / 'ios/Passport/PassportApp.swift'
        source.unlink()
        source.symlink_to(self.root / 'LICENSE')
        with patch.object(release, 'ROOT', self.root), patch.object(
                release.subprocess, 'check_output', return_value='\0'.join(names).encode()):
            with self.assertRaises(ValueError):
                release.ios_source(self.root / 'symlink.zip')

    def test_release_versions_match(self):
        version = json.loads((ROOT / 'tools/openclaw/package.json').read_text())['version']
        lock = json.loads((ROOT / 'tools/openclaw/package-lock.json').read_text())
        self.assertEqual(lock['version'], version)
        self.assertEqual(lock['packages']['']['version'], version)
        for name in ['index.mjs', 'doctor.mjs']:
            self.assertIn(f"pluginVersion: '{version}'", (ROOT / 'tools/openclaw' / name).read_text())
        self.assertEqual(release.check_version(f'v{version}'), version)
        with self.assertRaises(ValueError):
            release.check_version('v99.0.0')

    def release_identity(self):
        for name in ['tools/openclaw/package.json', 'tools/openclaw/package-lock.json',
                     'tools/openclaw/index.mjs', 'tools/openclaw/doctor.mjs',
                     'tools/hermes/plugin.yaml', 'web/package.json', 'web/package-lock.json',
                     'docs/release/notes.md', 'docs/release/notes.zh_CN.md',
                     'docs/CHANGELOG.md', 'docs/CHANGELOG.zh_CN.md']:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / name).read_bytes())
        return json.loads((ROOT / 'tools/openclaw/package.json').read_text())['version']

    def release_downloads(self):
        version = self.release_identity()
        downloads = self.root / 'downloads'
        for name in release.asset_names(version) - {'LICENSE.txt'}:
            if name.endswith('.tgz'):
                relative = f'release-plugin/{name}'
            elif name.startswith('ai-passport-hermes-'):
                relative = f'release-hermes/{name}'
            elif '-firmware.tar.gz' in name:
                relative = f'release-firmware/{name}'
            elif name.endswith('.bin'):
                relative = f'release-firmware/firmware/{name}'
            else:
                self.fail(f'Unexpected release asset: {name}')
            path = downloads / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f'fixture: {name}')
        (self.root / 'LICENSE').write_text('MIT fixture')
        return downloads, f'v{version}', 'a' * 40

    def test_release_identity_rejects_mismatch(self):
        version = self.release_identity()
        for name in ['tools/openclaw/package-lock.json', 'tools/openclaw/index.mjs',
                     'tools/hermes/plugin.yaml', 'web/package.json', 'web/package-lock.json',
                     'docs/release/notes.md',
                     'docs/CHANGELOG.zh_CN.md']:
            path = self.root / name
            original = path.read_text()
            path.write_text(original.replace(version, '0.0.0'))
            with self.subTest(name=name), patch.object(release, 'ROOT', self.root):
                with self.assertRaises(ValueError):
                    release.check_version()
            path.write_text(original)

    def test_release_assets_and_provenance(self):
        source, tag, commit = self.release_downloads()
        destination = self.root / 'dist'
        with patch.object(release, 'ROOT', self.root):
            release.prepare_assets(source, destination, tag, commit)
            release.verify_assets(destination, tag, commit)
            self.assertEqual(len(list(destination.iterdir())), 8)
            with self.assertRaises(ValueError):
                release.prepare_assets(source, destination, tag, commit)
            with self.assertRaises(ValueError):
                release.verify_assets(destination, tag, 'b' * 40)
            (destination / 'agent-talk.bin').write_text('tampered')
            with self.assertRaises(ValueError):
                release.verify_assets(destination, tag, commit)

    def test_release_assets_reject_missing_empty_and_symlink(self):
        source, tag, commit = self.release_downloads()
        path = source / 'release-firmware/ai-passport-agent-firmware.tar.gz'
        for invalid in ['missing', 'empty', 'symlink']:
            if path.exists():
                path.unlink()
            if invalid == 'empty':
                path.touch()
            elif invalid == 'symlink':
                path.symlink_to(self.root / 'LICENSE')
            with self.subTest(invalid=invalid), patch.object(release, 'ROOT', self.root):
                with self.assertRaises(ValueError):
                    release.prepare_assets(source, self.root / 'dist', tag, commit)
                self.assertFalse((self.root / 'dist').exists())

    def test_native_apps_do_not_gate_release_identity(self):
        version = self.release_identity()
        for name in ['mac/project.yml', 'ios/project.yml']:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('MARKETING_VERSION: "0.0.0"\n')
        with patch.object(release, 'ROOT', self.root):
            self.assertEqual(release.check_version(f'v{version}'), version)
            for name in ['mac/project.yml', 'ios/project.yml']:
                (self.root / name).unlink()
            self.assertEqual(release.check_version(f'v{version}'), version)

    def test_release_assets_exclude_native_apps(self):
        version = self.release_identity()
        self.assertEqual(release.asset_names(version), {
            f'ai-passport-openclaw-{version}.tgz', 'ai-passport-agent-firmware.tar.gz',
            f'ai-passport-hermes-{version}.tar.gz',
            'agent-talk-full.bin', 'agent-talk.bin', 'LICENSE.txt',
        })

    def test_hermes_package_is_allowlisted_and_reproducible(self):
        version = self.release_identity()
        for name in release.HERMES_FILES:
            path = self.root / 'tools/hermes' / name
            if not path.exists():
                path.write_text(f'fixture {name}')
        (self.root / 'LICENSE').write_text('MIT fixture')
        (self.root / 'tools/hermes/private.key').write_text('not for release')
        (self.root / 'tools/hermes/devices.sqlite3').write_text('private state')
        with patch.object(release, 'ROOT', self.root):
            release.hermes_package(self.root / 'first')
            release.hermes_package(self.root / 'second')
            with self.assertRaises(FileExistsError):
                release.hermes_package(self.root / 'first')
        name = f'ai-passport-hermes-{version}.tar.gz'
        first = self.root / 'first' / name
        self.assertEqual(first.read_bytes(), (self.root / 'second' / name).read_bytes())
        with tarfile.open(first) as archive:
            self.assertEqual(set(archive.getnames()),
                             {f'ai-passport-hermes/{name}' for name in release.HERMES_FILES | {'LICENSE'}})
            self.assertTrue(all(member.isfile() and member.mode == 0o644 for member in archive))
        path = self.root / 'tools/hermes/adapter.py'
        for invalid in ['missing', 'empty', 'symlink']:
            if path.exists():
                path.unlink()
            if invalid == 'empty':
                path.touch()
            elif invalid == 'symlink':
                path.symlink_to(self.root / 'LICENSE')
            with self.subTest(invalid=invalid), patch.object(release, 'ROOT', self.root):
                with self.assertRaises(ValueError):
                    release.hermes_package(self.root / invalid)
                self.assertFalse((self.root / invalid).exists())

    def test_ci_does_not_build_or_package_native_apps(self):
        self.assertFalse((ROOT / '.github/workflows/ios.yml').exists())
        for path in (ROOT / '.github/workflows').glob('*.yml'):
            workflow = path.read_text()
            for command in ['xcodebuild', 'xcodegen', 'release.py ios-source',
                            'release.py mac-resources', 'workflows/ios.yml',
                            'release-ios', 'release-mac']:
                with self.subTest(workflow=path.name, command=command):
                    self.assertNotIn(command, workflow)

    def test_release_verification_rejects_incomplete_or_extra_assets(self):
        source, tag, commit = self.release_downloads()
        destination = self.root / 'dist'
        with patch.object(release, 'ROOT', self.root):
            release.prepare_assets(source, destination, tag, commit)
            (destination / 'private.log').write_text('unexpected')
            with self.assertRaises(ValueError):
                release.verify_assets(destination, tag, commit)
            (destination / 'private.log').unlink()
            checksum = destination / 'SHA256SUMS.txt'
            original = checksum.read_text()
            for malformed in [original.splitlines()[0] + '\n', original + original.splitlines()[0] + '\n']:
                checksum.write_text(malformed)
                with self.assertRaises(ValueError):
                    release.verify_assets(destination, tag, commit)


if __name__ == '__main__':
    unittest.main()
