import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
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
        files = {'FoloToy-AI-Passport.bin': app,
                 'FoloToy-AI-Passport-full.bin': bytes(0x10000) + app,
                 'flash_args': b'fixture', 'bootloader/bootloader.bin': b'boot',
                 'partition_table/partition-table.bin': b'partition'}
        self.manifest = {'schema_version': 1, 'target': 'esp32c3',
                         'flash_size_bytes': 8 * 1024 * 1024,
                         'full_bin_sha256': hashlib.sha256(files['FoloToy-AI-Passport-full.bin']).hexdigest(),
                         'image_offsets': {'FoloToy-AI-Passport.bin': 0x10000}, 'files': {}}
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

    def test_reject_tampering(self):
        (self.source / 'FoloToy-AI-Passport.bin').write_bytes(b'corrupt')
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
                 'ios/Passport/Info.plist', 'shared/PairPrompt.swift',
                 'docs/release/ios-install.md', 'docs/release/ios-install.zh_CN.md', 'LICENSE']
        for name in names:
            source = self.root / name
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text('fixture')
        return names

    def test_ios_source_allowlist(self):
        names = self.ios_inputs() + ['ios/Passport/.env', 'ios/Passport/private.mobileprovision',
                                   'ios/Passport/xcuserdata/private.json', 'shared/private.json']
        (self.root / 'docs/release/ios-install.md').write_text('[Chinese](ios-install.zh_CN.md)')
        (self.root / 'docs/release/ios-install.zh_CN.md').write_text('[English](ios-install.md)')
        destination = self.root / 'source.zip'
        with patch.object(release, 'ROOT', self.root), patch.object(
                release.subprocess, 'check_output', return_value='\0'.join(names).encode()):
            release.ios_source(destination)
        with zipfile.ZipFile(destination) as archive:
            prefix = 'ai-passport-openclaw-ios-source/'
            self.assertEqual(set(archive.namelist()), {prefix + name for name in [
                'ios/project.yml', 'ios/Passport/PassportApp.swift', 'ios/Passport/Info.plist',
                'shared/PairPrompt.swift', 'README.md', 'README.zh_CN.md', 'LICENSE']})
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
        for name in ['mac/project.yml', 'ios/project.yml']:
            self.assertIn(f'MARKETING_VERSION: "{version}"', (ROOT / name).read_text())


if __name__ == '__main__':
    unittest.main()
