import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('web_tools', ROOT / 'tools/web.py')
web = importlib.util.module_from_spec(SPEC)
with patch.object(sys, 'path', [str(ROOT / 'tools'), *sys.path]):
    SPEC.loader.exec_module(web)


class WebFirmwareTests(unittest.TestCase):
    def test_prepare_builds_distinct_bilingual_backend_prompts(self):
        for source in ['assets/images/passport-lobster.svg', 'shared/PairPrompt.swift',
                       'web/prompts/hermes-zh.txt', 'web/prompts/hermes-en.txt']:
            destination = self.root / source
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / source, destination)
        web.prepare()
        for language in ['zh', 'en']:
            with self.subTest(language=language):
                openclaw = (self.root / f'web/public/setup-{language}.txt').read_text()
                hermes = (self.root / f'web/public/setup-hermes-{language}.txt').read_text()
                self.assertIn('openclaw plugins install', openclaw)
                self.assertIn('openclaw qr --json', openclaw)
                self.assertNotIn('hermes plugins enable', openclaw)
                self.assertIn('hermes plugins enable ai-passport-hermes', hermes)
                self.assertIn('hermes plugins doctor', hermes)
                self.assertIn('/passport_auth.py --home', hermes)
                self.assertIn('gateway.platforms.ai_passport', hermes)
                self.assertIn('/passport/v1', hermes)
                self.assertIn('127.0.0.1', hermes)
                self.assertIn('v0.6.0', hermes)
                self.assertNotIn('openclaw gateway call', hermes)
                self.assertNotIn('openclaw qr --json', hermes)
                self.assertEqual(hermes, (self.root / f'web/prompts/hermes-{language}.txt').read_text())

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        for name, value in [('ROOT', self.root), ('WEB', self.root / 'web')]:
            replacement = patch.object(web, name, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        self.destination = self.root / 'web/public/firmware'
        self.destination.mkdir(parents=True)
        self.manifest = self.destination / 'manifest.json'
        self.manifest.write_text(json.dumps({'schema': 1, 'version': 'v1.2.3'}))

    def image(self, version='v1.2.3-4-gabcdef0-dirty', build='12' * 32):
        image = bytearray(288)
        image[0] = 0xe9
        image[12] = 5
        image[32:36] = b'\x32\x54\xcd\xab'
        encoded = version.encode('ascii')
        image[48:48 + len(encoded)] = encoded
        image[176:208] = bytes.fromhex(build)
        return bytes(image)

    def archive(self, image, name='archive'):
        directory = self.root / 'build/firmware' / name
        directory.mkdir(parents=True)
        (directory / 'FoloToy-AI-Passport.bin').write_bytes(image)
        receipt = {'app_elf_sha256': image[176:208].hex(), 'files': {
            'FoloToy-AI-Passport.bin': {'size': len(image), 'sha256': hashlib.sha256(image).hexdigest()}}}
        (directory / 'manifest.json').write_text(json.dumps(receipt))
        return directory

    def release(self, directory, tag='v1.2.3', build='34' * 32):
        directory.mkdir(parents=True)
        image = self.image(tag, build)
        full = bytearray(0x10000) + image
        struct.pack_into('<HBBII', full, 0x8000, 0x50aa, 0, 0, 0x10000, 0x340000)
        struct.pack_into('<HBBII', full, 0x8020, 0x50aa, 1, 2, 0x350000, 0x10000)
        receipt = {'schema_version': 1, 'tag': tag, 'commit_sha': 'a' * 40, 'assets': {}}
        for filename, data in [('FoloToy-AI-Passport.bin', image), ('FoloToy-AI-Passport-full.bin', full)]:
            (directory / filename).write_bytes(data)
            receipt['assets'][filename] = {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        (directory / 'release-manifest.json').write_text(json.dumps(receipt))
        return directory

    def test_local_catalog_identifies_development_without_replacing_firmware(self):
        image = self.image()
        self.archive(image)
        (self.destination / 'FoloToy-AI-Passport.bin').write_bytes(b'official firmware')
        web.stage_local_build_catalog()
        manifest = json.loads(self.manifest.read_text())
        self.assertEqual(manifest['knownBuilds'], {
            image[176:184].hex(): {'version': 'v1.2.3', 'channel': 'development'}})
        self.assertEqual((self.destination / 'FoloToy-AI-Passport.bin').read_bytes(), b'official firmware')

    def test_local_catalog_does_not_publish_arbitrary_descriptor_text(self):
        image = self.image('fixture-private-metadata')
        self.archive(image)
        web.stage_local_build_catalog()
        self.assertNotIn('fixture-private-metadata', self.manifest.read_text())
        record = json.loads(self.manifest.read_text())['knownBuilds'][image[176:184].hex()]
        self.assertEqual(record, {'version': '', 'channel': 'development'})

    def test_local_catalog_ignores_corrupt_and_stale_records(self):
        directory = self.archive(self.image())
        (directory / 'FoloToy-AI-Passport.bin').write_bytes(b'corrupt')
        self.manifest.write_text(json.dumps({'knownBuilds': {'old': {'version': '', 'channel': 'development'}}}))
        web.stage_local_build_catalog()
        self.assertEqual(json.loads(self.manifest.read_text())['knownBuilds'], {})

    def test_local_catalog_rejects_wrong_embedded_build(self):
        directory = self.archive(self.image())
        path = directory / 'manifest.json'
        receipt = json.loads(path.read_text())
        receipt['app_elf_sha256'] = 'f' * 64
        path.write_text(json.dumps(receipt))
        web.stage_local_build_catalog()
        self.assertEqual(json.loads(self.manifest.read_text())['knownBuilds'], {})

    def test_local_catalog_preserves_verified_release_identity(self):
        image = self.image()
        self.archive(image)
        records = {image[176:184].hex(): {'version': 'v1.2.3', 'channel': 'release'}}
        self.manifest.write_text(json.dumps({'knownBuilds': records}))
        web.stage_local_build_catalog()
        self.assertEqual(json.loads(self.manifest.read_text())['knownBuilds'], records)

    def test_release_history_is_derived_from_verified_images(self):
        current = self.release(self.root / 'release')
        history = self.root / 'history'
        self.release(history / 'v1.2.2', 'v1.2.2', '56' * 32)
        with contextlib.redirect_stdout(io.StringIO()):
            web.stage_firmware(current, history)
        manifest = json.loads(self.manifest.read_text())
        self.assertEqual(manifest['buildId'], '34' * 8)
        self.assertEqual(manifest['knownBuilds'], {'56' * 8: {'version': 'v1.2.2', 'channel': 'release'}})
        self.assertEqual(manifest['version'], 'v1.2.3')

    def test_release_history_rejects_corrupt_image(self):
        directory = self.release(self.root / 'release')
        (directory / 'FoloToy-AI-Passport.bin').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'Invalid release identity image'):
            web.release_identity(directory)

    def test_retired_releases_keep_version_recognition(self):
        directory = self.release(self.root / 'release')
        retained = {'56' * 8: {'version': 'v1.2.1', 'channel': 'release'}}
        (self.root / 'web/release-builds.json').write_text(json.dumps(retained))
        with contextlib.redirect_stdout(io.StringIO()):
            web.stage_firmware(directory)
        self.assertEqual(json.loads(self.manifest.read_text())['knownBuilds'], retained)

    def test_retained_catalog_rejects_nonrelease_metadata(self):
        directory = self.release(self.root / 'release')
        catalog = self.root / 'web/release-builds.json'
        for build_id, identity in [('bad', {'version': 'v1.2.1', 'channel': 'release'}),
                                   ('56' * 8, {'version': 'private-address', 'channel': 'release'}),
                                   ('56' * 8, {'version': 'v1.2.1', 'channel': 'development'}),
                                   ('56' * 8, {'version': 'v1.2.1', 'channel': 'release', 'extra': 'data'})]:
            with self.subTest(build_id=build_id, identity=identity):
                catalog.write_text(json.dumps({build_id: identity}))
                with self.assertRaisesRegex(ValueError, 'Invalid retained release identity'):
                    web.stage_firmware(directory)

    def test_legacy_release_uses_checksum_and_tag_without_manifest(self):
        directory = self.root / 'history/v1.2.1'
        directory.mkdir(parents=True)
        image = self.image('v1.2.1')
        (directory / 'FoloToy-AI-Passport.bin').write_bytes(image)
        checksum = hashlib.sha256(image).hexdigest()
        path = directory / 'SHA256SUMS.txt'
        path.write_text(f'{checksum}  FoloToy-AI-Passport.bin\n')
        self.assertEqual(web.release_identity(directory), (image[176:184].hex(), {'version': 'v1.2.1', 'channel': 'release'}))
        path.write_text(f'{checksum}  ./FoloToy-AI-Passport.bin\n')
        self.assertEqual(web.release_identity(directory)[1]['version'], 'v1.2.1')
        path.write_text(f'{"0" * 64}  FoloToy-AI-Passport.bin\n')
        with self.assertRaisesRegex(ValueError, 'legacy release identity checksum'):
            web.release_identity(directory)

    def test_release_staging_removes_local_development_catalog(self):
        self.archive(self.image())
        web.stage_local_build_catalog()
        directory = self.release(self.root / 'release')
        with contextlib.redirect_stdout(io.StringIO()):
            web.stage_firmware(directory)
        self.assertEqual(json.loads(self.manifest.read_text())['knownBuilds'], {})


if __name__ == '__main__':
    unittest.main()
