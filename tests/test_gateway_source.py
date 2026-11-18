"""Migration contracts: reviewed source, complete archives and legacy entry points."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import gateway_source as gateway
import release


class GatewaySourceTests(unittest.TestCase):
    def test_reviewed_checkout_and_unchanged_protocol_identity(self):
        source = gateway.gateway_root(ROOT)
        self.assertEqual(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source,
                                                text=True).strip(), gateway.PIN)
        index = subprocess.check_output(['git', 'ls-files', '--stage', '--', str(gateway.SUBMODULE)],
                                        cwd=ROOT, text=True)
        self.assertTrue(index.startswith('160000 ' + gateway.PIN + ' '))
        metadata = json.loads((source / 'tools/openclaw/package.json').read_text())
        manifest = json.loads((source / 'tools/openclaw/openclaw.plugin.json').read_text())
        self.assertEqual(metadata['name'], 'ai-passport-openclaw')
        self.assertEqual(manifest['id'], metadata['name'])
        self.assertIn('name: ai-passport-hermes', (source / 'tools/hermes/plugin.yaml').read_text())
        for family in ('ai-passport', 'e-ink4-plus'):
            self.assertIn(family, (source / 'tools/openclaw/passport-proxy.mjs').read_text())
            self.assertIn(family, (source / 'tools/hermes/passport_auth.py').read_text())

    def test_missing_checkout_is_actionable_in_all_entry_points(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, 'git submodule update --init --recursive'):
                gateway.gateway_root(root)
            (root / 'tools/openclaw').mkdir(parents=True)
            for name in ('gateway_source.py', 'openclaw/doctor.mjs', 'openclaw/manage-plugin.py',
                         'openclaw/install-passport-relay.py'):
                (root / 'tools' / name).write_bytes((ROOT / 'tools' / name).read_bytes())
            for command in ([sys.executable, 'tools/gateway_source.py'],
                            ['node', 'tools/openclaw/doctor.mjs'],
                            [sys.executable, 'tools/openclaw/manage-plugin.py', '--help']):
                result = subprocess.run(command, cwd=root, capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('git submodule update --init --recursive', result.stderr)
                self.assertNotIn('Traceback', result.stderr)

    def test_protocol_sources_are_not_duplicated(self):
        self.assertFalse((ROOT / 'tools/openclaw/passport-proxy.mjs').exists())
        self.assertFalse((ROOT / 'tools/hermes/adapter.py').exists())
        files = gateway.repository_files(ROOT)
        self.assertIn('integrations/valet-gateway/tools/hermes/adapter.py', files)
        self.assertNotIn('integrations/valet-gateway', files)
        self.assertFalse(any('/node_modules/' in name or '/.git/' in name for name in files))

    def test_complete_source_archive_contains_both_adapters_without_git_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            release.source_package(root / 'package')
            archive = root / 'package/ai-passport-agent-source.tar.gz'
            with tarfile.open(archive) as bundle:
                names = bundle.getnames()
                for name in ('tools/openclaw/index.mjs', 'tools/openclaw/package-lock.json',
                             'tools/openclaw/openclaw.plugin.json', 'tools/hermes/adapter.py',
                             'tools/hermes/passport_auth.py', 'tools/hermes/plugin.yaml'):
                    self.assertIn('ai-passport-agent/integrations/valet-gateway/' + name, names)
                self.assertFalse(any('/.git/' in name or '/node_modules/' in name for name in names))
                self.assertTrue(all(item.isfile() for item in bundle))
                self.assertIn('ai-passport-agent/tools/gateway_source.py', names)
                bundle.extractall(root / 'unpacked', filter='data')
            source = root / 'unpacked/ai-passport-agent'
            plugin = source / 'integrations/valet-gateway/tools/openclaw'
            subprocess.run(['node', '--check', 'index.mjs'], cwd=plugin, check=True)
            subprocess.run(['node', 'tests/test_passport_plugin.mjs'], cwd=source, check=True,
                           stdout=subprocess.DEVNULL)
            with patch.object(release, 'ROOT', source):
                self.assertEqual(release.check_version(), json.loads((ROOT / 'web/package.json').read_text())['version'])
                release.hermes_package(root / 'hermes')
            with tarfile.open(root / 'hermes/ai-passport-hermes-0.7.5.tar.gz') as bundle:
                for item in bundle:
                    self.assertTrue(item.isfile())
                    self.assertEqual(bundle.extractfile(item).read(),
                                     (source / 'integrations/valet-gateway/tools/hermes' / Path(item.name).name).read_bytes()
                                     if Path(item.name).name != 'LICENSE' else (source / 'LICENSE').read_bytes())

    def test_legacy_python_commands_forward(self):
        for name in ('manage-plugin.py', 'install-passport-relay.py'):
            subprocess.run([sys.executable, str(ROOT / 'tools/openclaw' / name), '--help'],
                           check=True, capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
