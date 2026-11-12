import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('public_audit', ROOT / 'tools/check_public.py')
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class PublicAuditTests(unittest.TestCase):
    def test_rejects_tokens_without_echoing_them(self):
        token = 'ghp_' + 'a' * 36
        errors = audit.findings('main/config.c', token)
        self.assertTrue(errors)
        self.assertNotIn(token, '\n'.join(errors))

    def test_runtime_endpoints_require_placeholders(self):
        self.assertTrue(audit.findings('shared/config.swift', 'wss://gateway.customer.net'))
        self.assertTrue(audit.findings('main/config.c', '10.23.45.67'))
        self.assertEqual(audit.findings('shared/config.swift', 'wss://gateway.example'), [])
        self.assertEqual(audit.findings('main/config.c', 'http://192.168.4.1'), [])
        self.assertEqual(audit.findings('tools/openclaw/passport-proxy.mjs', 'ws://127.0.0.1 192.0.2.1'), [])

    def test_local_identity_rejected(self):
        path = '/Users/' + 'fixture/' + 'private'
        self.assertTrue(audit.findings('assets/font.c', path))
        team = 'DEVELOPMENT_TEAM: ' + 'A' * 10
        self.assertTrue(audit.findings('mac/project.yml', team))

    def test_browser_endpoints_require_placeholders(self):
        self.assertTrue(audit.findings('web/src/main.js', 'wss://gateway.customer.net'))
        self.assertTrue(audit.findings('web/public/settings.json', '10.23.45.67'))
        self.assertEqual(audit.findings('web/src/main.js', 'wss://gateway.example'), [])
        self.assertEqual(audit.findings('web/dist/index.html', 'http://www.w3.org'), [])
        self.assertEqual(audit.findings('web/src/main.js', 'https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055'), [])
        self.assertTrue(audit.findings('web/src/main.js', 'https://apps.apple.com.customer.net/pair'))


if __name__ == '__main__':
    unittest.main()
