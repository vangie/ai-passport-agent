import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


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


if __name__ == '__main__':
    unittest.main()
