import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_web_public import audit_site


class WebPublicTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        (self.root / 'index.html').write_text('<title>AI Passport</title>')

    def firmware(self, stored=255, payload=b''):
        directory = self.root / 'firmware'
        directory.mkdir(exist_ok=True)
        image = bytearray(b'\xff' * 0x11000)
        struct.pack_into('<HBBII', image, 0x8000, 0x50aa, 1, 2, 0x9000, 0x6000)
        image[0x9000] = stored
        image[0x10000:0x10000 + len(payload)] = payload
        manifest = {'knownBuilds': {}}
        for kind, filename, data in [('application', 'FoloToy-AI-Passport.bin', image[0x10000:]),
                                     ('full', 'FoloToy-AI-Passport-full.bin', image)]:
            (directory / filename).write_bytes(data)
            manifest[kind] = {'file': filename, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        (directory / 'manifest.json').write_text(json.dumps(manifest))

    def test_clean_site_and_library_key_markers_are_allowed(self):
        marker = b'-----BEGIN ' + b'PRIVATE KEY-----'
        self.firmware(payload=marker)
        self.assertEqual(audit_site(self.root, True), [])

    def test_all_backend_prompts_are_allowed_and_still_scanned(self):
        for filename in ['setup-zh.txt', 'setup-en.txt', 'setup-hermes-zh.txt', 'setup-hermes-en.txt']:
            with self.subTest(filename=filename):
                path = self.root / filename
                path.write_text('https://github.com/vangie/ai-passport-agent wss://gateway.example')
                self.assertEqual(audit_site(self.root), [])
                path.write_text('wss://gateway.customer.net')
                self.assertTrue(audit_site(self.root))
                path.unlink()

    def test_rejects_secrets_and_personal_urls_without_echoing(self):
        token = 'ghp_' + 'a' * 36
        for text in [token, 'wss://gateway.customer.net']:
            with self.subTest(text=text):
                (self.root / 'setup-en.txt').write_text(text)
                errors = audit_site(self.root)
                self.assertTrue(errors)
                self.assertNotIn(text, '\n'.join(errors))

    def test_rejects_private_artifacts_and_symlinks(self):
        (self.root / '.env').write_text('private')
        (self.root / 'settings.json').write_text('{}')
        (self.root / 'alias').symlink_to(self.root / 'index.html')
        self.assertEqual(len(audit_site(self.root)), 3)

    def test_rejects_device_data_and_complete_private_keys(self):
        self.firmware(stored=0)
        self.assertTrue(audit_site(self.root, True))
        key = b'-----BEGIN ' + b'PRIVATE KEY-----\n' + b'A' * 64 + b'\n-----END ' + b'PRIVATE KEY-----'
        self.firmware(payload=key)
        self.assertTrue(audit_site(self.root, True))

    def test_rejects_missing_firmware_tampering_and_local_catalog(self):
        self.assertTrue(audit_site(self.root, True))
        self.firmware()
        (self.root / 'firmware/FoloToy-AI-Passport.bin').write_bytes(b'changed')
        self.assertTrue(audit_site(self.root, True))
        self.firmware()
        path = self.root / 'firmware/manifest.json'
        manifest = json.loads(path.read_text())
        manifest['knownBuilds'] = {'12' * 8: {'channel': 'development'}}
        path.write_text(json.dumps(manifest))
        self.assertTrue(audit_site(self.root, True))


if __name__ == '__main__':
    unittest.main()
