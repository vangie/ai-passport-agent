import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / 'tools/openclaw'
PACKAGE_FILES = {
    'package.json', 'openclaw.plugin.json', 'index.mjs', 'passport-plugin.mjs',
    'passport-proxy.mjs', 'passport-relay.mjs', 'passport-ima.mjs', 'doctor.mjs',
    'manage-plugin.py', 'install-passport-relay.py', 'README.md', 'README.zh_CN.md', 'LICENSE'
}


class PluginPackageTests(unittest.TestCase):
    def test_metadata_license_and_locked_dependencies(self):
        metadata = json.loads((PLUGIN / 'package.json').read_text())
        manifest = json.loads((PLUGIN / 'openclaw.plugin.json').read_text())
        lock = json.loads((PLUGIN / 'package-lock.json').read_text())
        self.assertEqual(metadata['name'], manifest['id'])
        self.assertEqual(metadata['name'], lock['name'])
        self.assertEqual(metadata['version'], lock['version'])
        self.assertEqual(metadata['license'], 'MIT')
        self.assertEqual((PLUGIN / 'LICENSE').read_bytes(), (ROOT / 'LICENSE').read_bytes())
        self.assertEqual(metadata['openclaw']['extensions'], ['./index.mjs'])
        self.assertEqual(set(metadata['files']) | {'package.json'}, PACKAGE_FILES)
        self.assertEqual(metadata['dependencies'], lock['packages']['']['dependencies'])
        for name, version in metadata['dependencies'].items():
            self.assertRegex(version, r'^\d+\.\d+\.\d+$')
            dependency = lock['packages']['node_modules/' + name]
            self.assertEqual(dependency['version'], version)
            self.assertTrue(dependency['integrity'].startswith('sha512-'))
            self.assertTrue(dependency['resolved'].startswith('https://registry.npmjs.org/'))
        self.assertNotIn('node_modules/openclaw', lock['packages'])

    def test_pack_contains_only_portable_project_sources(self):
        with tempfile.TemporaryDirectory(prefix='passport-plugin-package-') as temporary:
            sandbox = Path(temporary)
            source = sandbox / 'plugin'
            source.mkdir()
            for name in PACKAGE_FILES:
                shutil.copy2(PLUGIN / name, source / name)
            for name in ['.env', '.dev.vars', 'private-key.pem', 'openclaw.json']:
                (source / name).write_text('private fixture, must never be packaged')
            for directory in ['node_modules', '__pycache__', 'logs']:
                (source / directory).mkdir()
                (source / directory / 'private-fixture').write_text('not distributable')
            home = sandbox / 'empty-home'
            home.mkdir()
            (source / 'local-host-link').symlink_to(home, target_is_directory=True)
            result = subprocess.run(
                ['npm', 'pack', '--ignore-scripts', '--json', '--pack-destination', str(sandbox)],
                cwd=source, check=True, capture_output=True, text=True, timeout=60)
            report = json.loads(result.stdout)[0]
            self.assertEqual(set(item['path'] for item in report['files']), PACKAGE_FILES)
            with tarfile.open(sandbox / report['filename'], 'r:gz') as archive:
                members = archive.getmembers()
                self.assertEqual(len(members), len(PACKAGE_FILES))
                self.assertEqual({member.name for member in members}, {'package/' + name for name in PACKAGE_FILES})
                for member in members:
                    self.assertTrue(member.isfile())
                    name = member.name.removeprefix('package/')
                    self.assertEqual(archive.extractfile(member).read(), (PLUGIN / name).read_bytes())
                    if name.endswith('.mjs'):
                        subprocess.run(['node', '--check', str(source / name)], check=True,
                                       capture_output=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
