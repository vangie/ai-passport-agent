import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('passport_plugin_manager', ROOT / 'tools/openclaw/manage-plugin.py')
manager = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(manager)


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.dist = self.root / 'dist'
        self.dist.mkdir()
        self.backups = self.root / 'backups'
        self.backups.mkdir()
        self.handler = 'handlers-fixture.mjs'
        self.original = b'function createTalkRealtimeRelaySession(params) {}\n'
        self.patched = self.original + b'const passportFixture = true;\n'
        self.original_sha = hashlib.sha256(self.original).hexdigest()
        original_path = self.dist / '.passport-relay-backups' / self.original_sha
        original_path.mkdir(parents=True)
        (original_path / self.handler).write_bytes(self.original)
        (self.dist / self.handler).write_bytes(self.patched)
        (self.dist / 'passport-relay.mjs').write_text('fixture relay')
        (self.dist / 'passport-ima.mjs').write_text('fixture codec')
        self.receipt = {'handler': self.handler, 'original_sha256': self.original_sha,
                        'patched_sha256': manager.digest(self.dist / self.handler),
                        'module_sha256': manager.digest(self.dist / 'passport-relay.mjs'),
                        'codec_sha256': manager.digest(self.dist / 'passport-ima.mjs')}
        (self.dist / 'passport-relay-patch.json').write_text(json.dumps(self.receipt))
        self.args = argparse.Namespace(dist=self.dist, backup_dir=self.backups, backup=None)
        self.status = {'protocol': 1, 'providerRegistered': True, 'upstreamAvailable': True,
                       'path': '/passport/v1', 'connections': 0}
        real_run = subprocess.run

        def command(arguments, **options):
            if arguments[0] == 'node':
                return subprocess.CompletedProcess(arguments, 0)
            if arguments[0] == 'openclaw':
                return subprocess.CompletedProcess(arguments, 0, stdout='plugin log\n' + json.dumps(self.status))
            return real_run(arguments, **options, capture_output=True)

        self.runner = patch.object(manager.subprocess, 'run', side_effect=command)
        self.runner.start()
        self.addCleanup(self.runner.stop)

    def migrate(self):
        report = manager.migrate(self.args)
        self.assertEqual(report['status'], 'migrated-core-restored')
        self.assertEqual((self.dist / self.handler).read_bytes(), self.original)
        self.assertFalse((self.dist / 'passport-relay-patch.json').exists())
        self.args.backup = Path(report['rollback'])
        self.assertEqual(self.args.backup.stat().st_mode & 0o777, 0o700)
        return report

    def test_migration_idempotence_and_verified_rollback(self):
        self.assertTrue(manager.inspect(self.args)['legacyPatch'])
        self.migrate()
        self.assertFalse(manager.inspect(self.args)['legacyPatch'])
        self.assertEqual(manager.migrate(self.args)['status'], 'already-native')
        self.assertEqual(manager.rollback(self.args)['status'], 'legacy-patch-restored')
        self.assertEqual((self.dist / self.handler).read_bytes(), self.patched)
        self.assertTrue(manager.inspect(self.args)['legacyPatch'])

    def test_changed_patch_refused_without_overwrite(self):
        (self.dist / self.handler).write_bytes(b'changed upstream')
        with self.assertRaisesRegex(ValueError, 'changed'):
            manager.migrate(self.args)
        self.assertEqual((self.dist / self.handler).read_bytes(), b'changed upstream')
        self.assertEqual(list(self.backups.iterdir()), [])

    def test_active_connections_and_unavailable_plugin_refused(self):
        for setting, value in [('connections', 1), ('providerRegistered', False), ('upstreamAvailable', False), ('path', '/wrong')]:
            previous = self.status[setting]
            self.status[setting] = value
            with self.assertRaises(ValueError):
                manager.migrate(self.args)
            self.assertEqual((self.dist / self.handler).read_bytes(), self.patched)
            self.status[setting] = previous

    def test_upgrade_blocks_legacy_rollback(self):
        self.migrate()
        (self.dist / self.handler).write_bytes(b'new OpenClaw version')
        with self.assertRaisesRegex(ValueError, 'upgraded'):
            manager.rollback(self.args)
        self.assertEqual((self.dist / self.handler).read_bytes(), b'new OpenClaw version')

    def test_tampered_backup_and_extra_files_refused(self):
        self.migrate()
        (self.args.backup / 'passport-ima.mjs').write_text('tampered')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            manager.rollback(self.args)
        self.assertEqual((self.dist / self.handler).read_bytes(), self.original)
        manifest_path = self.args.backup / 'rollback.json'
        manifest = json.loads(manifest_path.read_text())
        manifest['files']['unexpected.mjs'] = '0' * 64
        manifest_path.write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, 'file set'):
            manager.rollback(self.args)

    def test_unsafe_receipt_path_refused(self):
        for handler in ['..', '.', '../escape', '/absolute']:
            self.receipt['handler'] = handler
            (self.dist / 'passport-relay-patch.json').write_text(json.dumps(self.receipt))
            with self.assertRaisesRegex(ValueError, 'Invalid'):
                manager.inspect(self.args)


if __name__ == '__main__':
    unittest.main()
