import contextlib
import errno
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import MagicMock, patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('web_preview', ROOT / 'tools/web_preview.py')
preview = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(preview)
QUICK_TUNNEL_DOMAIN = 'trycloudflare.com'


def quick_url(label):
    return f'https://{label}.{QUICK_TUNNEL_DOMAIN}'


class WebPreviewTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = Path(directory.name)
        self.config = self.directory / 'tunnel.json'
        self.credentials = self.directory / 'credentials.json'
        self.credentials.write_text('{}')
        self.settings = {'tunnel': '00000000-0000-0000-0000-000000000000',
                         'hostname': 'passport-dev.example',
                         'credentials_file': str(self.credentials)}
        self.output = io.StringIO()
        for target, value in [('CONFIG', self.config), ('STATE', self.directory / 'state.json'),
                              ('LOCK', self.directory / 'preview.lock'), ('BUILD_LOCK', self.directory / 'build.lock')]:
            replacement = patch.object(preview, target, value)
            replacement.start()
            self.addCleanup(replacement.stop)
        environment = patch.dict('os.environ', {'PATH': os.environ.get('PATH', '')}, clear=True)
        environment.start()
        self.addCleanup(environment.stop)

    def write_config(self):
        self.config.write_text(json.dumps(self.settings))

    def inline_reader(self, target, **_options):
        reader = MagicMock()
        reader.start.side_effect = target
        return reader

    def test_missing_config_selects_quick_tunnel(self):
        self.assertEqual(preview.tunnel_settings('http://127.0.0.1:4173'), ({}, None))

    def test_missing_explicit_config_does_not_fall_back_to_public_quick_tunnel(self):
        with patch.dict('os.environ', {'PASSPORT_WEB_TUNNEL_CONFIG': str(self.config)}):
            with self.assertRaisesRegex(ValueError, 'does not point to a file'):
                preview.tunnel_settings('http://127.0.0.1:4173')

    def test_fixed_config_forwards_only_the_preview_with_a_catch_all_404(self):
        self.write_config()
        configuration, url = preview.tunnel_settings('http://127.0.0.1:4180')
        self.assertEqual(url, 'https://passport-dev.example')
        self.assertEqual(configuration['credentials-file'], str(self.credentials.resolve()))
        self.assertEqual(configuration['ingress'], [
            {'hostname': 'passport-dev.example', 'service': 'http://127.0.0.1:4180'},
            {'service': 'http_status:404'},
        ])

    def test_invalid_configuration_never_silently_uses_a_temporary_url(self):
        for key, value in [('hostname', 'https://passport-dev.example/path'),
                           ('hostname', 'passport-dev.example\nother: value'),
                           ('tunnel', '--untrusted'), ('credentials_file', '/missing/credentials.json')]:
            with self.subTest(key=key, value=value):
                settings = dict(self.settings)
                settings[key] = value
                self.config.write_text(json.dumps(settings))
                with self.assertRaises(ValueError):
                    preview.tunnel_settings('http://127.0.0.1:4173')

    def test_credentials_cannot_be_stored_in_the_repository(self):
        self.write_config()
        with patch.object(preview, 'ROOT', self.directory):
            with self.assertRaisesRegex(ValueError, 'outside the repository'):
                preview.tunnel_settings('http://127.0.0.1:4173')

    def test_no_cloudflared_keeps_local_preview(self):
        with patch.object(preview.shutil, 'which', return_value=None), contextlib.redirect_stdout(self.output):
            with preview.tunnel({'local_url': 'http://127.0.0.1:4173'}):
                pass
        self.assertIn('locally only', self.output.getvalue())

    def test_repeated_start_reuses_lock_and_current_url_and_removes_stale_state(self):
        with contextlib.redirect_stdout(self.output):
            with preview.preview_lock() as first:
                self.assertTrue(first)
                preview.save_state({'local_url': 'http://127.0.0.1:4173', 'public_url': 'https://passport-dev.example'})
                with preview.preview_lock() as second:
                    self.assertFalse(second)
                self.assertTrue(preview.STATE.exists())
            self.assertFalse(preview.STATE.exists())
        self.assertIn('keeping its existing tunnel', self.output.getvalue())
        self.assertIn('https://passport-dev.example', self.output.getvalue())

    def test_occupied_port_automatically_selects_a_free_port(self):
        server = MagicMock()
        server.server_port = 4242
        with patch.object(preview, 'ThreadingHTTPServer', side_effect=[OSError(errno.EADDRINUSE, 'occupied'), server]) as factory, patch.object(preview, 'tunnel') as tunnel, contextlib.redirect_stdout(self.output):
            preview.serve(self.directory)
        self.assertEqual([call.args[0] for call in factory.call_args_list], [('127.0.0.1', 4173), ('127.0.0.1', 0)])
        self.assertEqual(tunnel.call_args.args[0]['local_url'], 'http://127.0.0.1:4242')
        server.serve_forever.assert_called_once()
        self.assertIn('existing service is untouched', self.output.getvalue())

    def test_unrelated_binding_errors_are_not_hidden(self):
        with patch.object(preview, 'ThreadingHTTPServer', side_effect=OSError(errno.EACCES, 'denied')):
            with self.assertRaises(PermissionError):
                preview.serve(self.directory)

    def test_missing_cloudflared_serves_locally(self):
        with patch.object(preview, 'ThreadingHTTPServer') as server, patch.object(preview.shutil, 'which', return_value=None), contextlib.redirect_stdout(self.output):
            server.return_value.server_port = 4173
            preview.serve(self.directory)
            server.assert_called_once()
            self.assertEqual(server.call_args.args[0], ('127.0.0.1', 4173))
            server.return_value.serve_forever.assert_called_once()
        self.assertIn('locally only', self.output.getvalue())

    def test_running_preview_is_reused_without_binding_or_starting_a_tunnel(self):
        with preview.preview_lock(), patch.object(preview, 'ThreadingHTTPServer') as server, patch.object(preview, 'tunnel') as tunnel, contextlib.redirect_stdout(self.output):
            preview.save_state({'local_url': 'http://127.0.0.1:4242', 'public_url': 'https://passport-dev.example'})
            preview.serve(self.directory)
            server.assert_not_called()
            tunnel.assert_not_called()

    def test_build_lock_serializes_builders_and_is_released_after_failure(self):
        with self.assertRaises(ValueError):
            with preview.build_lock(), preview.BUILD_LOCK.open('a') as other:
                with self.assertRaises(BlockingIOError):
                    preview.fcntl.flock(other, preview.fcntl.LOCK_EX | preview.fcntl.LOCK_NB)
                raise ValueError('build failed')
        with preview.BUILD_LOCK.open('a') as other:
            preview.fcntl.flock(other, preview.fcntl.LOCK_EX | preview.fcntl.LOCK_NB)

    def test_publish_keeps_old_assets_and_replaces_index_last(self):
        source = self.directory / 'staged'
        destination = self.directory / 'dist'
        (source / 'assets').mkdir(parents=True)
        (destination / 'assets').mkdir(parents=True)
        (source / 'index.html').write_text('new page')
        (source / 'assets/new.js').write_text('new code')
        (destination / 'index.html').write_text('old page')
        (destination / 'assets/old.js').write_text('old code')
        copied = []
        copy = preview.shutil.copyfile

        def record(path, target):
            self.assertEqual((destination / 'index.html').read_text(), 'old page')
            copied.append(path.relative_to(source).as_posix())
            return copy(path, target)

        with patch.object(preview.shutil, 'copyfile', side_effect=record):
            preview.publish(source, destination)
        self.assertEqual(copied, ['assets/new.js', 'index.html'])
        self.assertEqual((destination / 'index.html').read_text(), 'new page')
        self.assertEqual((destination / 'assets/old.js').read_text(), 'old code')
        self.assertEqual((destination / 'assets/new.js').read_text(), 'new code')
        self.assertEqual(list(destination.rglob('.publish-*')), [])

    def test_failed_publish_does_not_replace_the_working_page(self):
        source = self.directory / 'staged'
        destination = self.directory / 'dist'
        source.mkdir()
        destination.mkdir()
        (source / 'index.html').write_text('new page')
        (destination / 'index.html').write_text('old page')
        with patch.object(preview.shutil, 'copyfile', side_effect=OSError('copy failed')):
            with self.assertRaises(OSError):
                preview.publish(source, destination)
        self.assertEqual((destination / 'index.html').read_text(), 'old page')
        self.assertEqual(list(destination.rglob('.publish-*')), [])

    def test_tunnel_is_stopped_when_preview_exits(self):
        executable = self.directory / 'cloudflared'
        executable.write_text('#!/bin/sh\nexec tail -f /dev/null\n')
        executable.chmod(0o700)
        with patch.object(preview.shutil, 'which', return_value=str(executable)), contextlib.redirect_stdout(self.output):
            with preview.tunnel({'local_url': 'http://127.0.0.1:4173'}) as child:
                self.assertIsNone(child.poll())
            self.assertIsNotNone(child.poll())
            with self.assertRaises(KeyboardInterrupt):
                with preview.tunnel({'local_url': 'http://127.0.0.1:4173'}) as interrupted:
                    raise KeyboardInterrupt
            self.assertIsNotNone(interrupted.poll())

    def test_expired_quick_tunnel_clears_old_url_and_replaces_it(self):
        state = {'local_url': 'http://127.0.0.1:4173', 'public_url': None}
        observations = []

        def old_lines():
            yield quick_url('old-preview') + '\n'
            yield 'Registered tunnel connection connIndex=0\n'
            observations.append(state['public_url'])
            yield 'Register tunnel error Unauthorized: Tunnel not found connIndex=0\n'

        def new_lines():
            observations.append(state['public_url'])
            yield quick_url('new-preview') + '\n'
            yield 'Registered tunnel connection connIndex=0\n'
            observations.append(state['public_url'])

        first, second = MagicMock(), MagicMock()
        first.stdout, second.stdout = MagicMock(), MagicMock()
        first.stdout.__iter__.return_value = old_lines()
        second.stdout.__iter__.return_value = new_lines()
        first.poll.return_value = None
        with patch.object(preview.shutil, 'which', return_value='cloudflared'), patch.object(preview.subprocess, 'Popen', side_effect=[first, second]) as spawn, patch.object(preview.threading, 'Thread', side_effect=self.inline_reader), contextlib.redirect_stdout(self.output):
            with preview.tunnel(state):
                pass
        self.assertEqual(observations, [quick_url('old-preview'), None, quick_url('new-preview')])
        self.assertEqual(spawn.call_count, 2)
        self.assertEqual(spawn.call_args_list[0], spawn.call_args_list[1])
        first.terminate.assert_called_once()
        self.assertIsNone(state['public_url'])
        self.assertIsNone(json.loads(preview.STATE.read_text())['public_url'])

    def test_disconnect_keeps_url_only_while_another_connector_is_registered(self):
        state = {'local_url': 'http://127.0.0.1:4173', 'public_url': None}
        observations = []

        def lines():
            yield quick_url('preview') + '\n'
            yield 'Registered tunnel connection connIndex=0\n'
            yield 'Registered tunnel connection connIndex=1\n'
            yield 'Connection terminated connIndex=0\n'
            observations.append(state['public_url'])
            yield 'Connection terminated connIndex=1\n'
            observations.append(state['public_url'])
            yield 'Registered tunnel connection connIndex=0\n'
            observations.append(state['public_url'])

        child = MagicMock()
        child.stdout.__iter__.return_value = lines()
        with patch.object(preview.shutil, 'which', return_value='cloudflared'), patch.object(preview.subprocess, 'Popen', return_value=child), patch.object(preview.threading, 'Thread', side_effect=self.inline_reader), contextlib.redirect_stdout(self.output):
            with preview.tunnel(state):
                pass
        self.assertEqual(observations, [quick_url('preview'), None, quick_url('preview')])
        self.assertIsNone(state['public_url'])

    def test_repeated_expiration_is_bounded_and_does_not_leave_a_stale_url(self):
        state = {'local_url': 'http://127.0.0.1:4173', 'public_url': None}
        children = []
        for number in range(3):
            child = MagicMock()
            child.poll.return_value = None
            child.stdout = io.StringIO(quick_url(f'preview-{number}') + '\nRegistered tunnel connection connIndex=0\nUnauthorized: Tunnel not found\n')
            children.append(child)
        with patch.object(preview.shutil, 'which', return_value='cloudflared'), patch.object(preview.subprocess, 'Popen', side_effect=children) as spawn, patch.object(preview.threading, 'Thread', side_effect=self.inline_reader), contextlib.redirect_stdout(self.output):
            with preview.tunnel(state):
                pass
        self.assertEqual(spawn.call_count, 3)
        self.assertIsNone(state['public_url'])
        self.assertEqual(self.output.getvalue().count('creating a replacement address'), 2)

    def test_missing_named_tunnel_never_creates_a_quick_tunnel(self):
        self.write_config()
        child = MagicMock()
        child.poll.return_value = None
        child.stdout = io.StringIO('Registered tunnel connection connIndex=0\nUnauthorized: Tunnel not found\n')
        state = {'local_url': 'http://127.0.0.1:4173', 'public_url': None}
        with patch.object(preview.shutil, 'which', return_value='cloudflared'), patch.object(preview.subprocess, 'Popen', return_value=child) as spawn, patch.object(preview.threading, 'Thread', side_effect=self.inline_reader), contextlib.redirect_stdout(self.output):
            with preview.tunnel(state):
                pass
        spawn.assert_called_once()
        self.assertEqual(spawn.call_args.args[0][-1], 'run')
        self.assertIsNone(state['public_url'])
        self.assertNotIn('creating a replacement address', self.output.getvalue())

    def test_generated_named_configuration_is_accepted_by_cloudflared(self):
        executable = shutil.which('cloudflared')
        if not executable:
            self.skipTest('cloudflared is optional')
        self.write_config()
        configuration, _ = preview.tunnel_settings('http://127.0.0.1:4173')
        generated = self.directory / 'generated.json'
        generated.write_text(json.dumps(configuration))
        result = subprocess.run([executable, 'tunnel', '--config', str(generated), 'ingress', 'validate'], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
