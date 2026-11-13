import contextlib
import errno
import fcntl
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import threading


ROOT = Path(__file__).resolve().parents[1]
STATE = ROOT / 'build/web-preview.json'
LOCK = ROOT / 'build/web-preview.lock'
BUILD_LOCK = ROOT / 'build/web-build.lock'
CONFIG = Path.home() / '.config/ai-passport-agent/web-tunnel.json'
LEGACY_CONFIG = Path.home() / '.config/ai-passport-openclaw/web-tunnel.json'


def save_state(state):
    temporary = STATE.with_suffix('.tmp')
    temporary.write_text(json.dumps(state) + '\n')
    temporary.replace(STATE)


def show_status():
    try:
        state = json.loads(STATE.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        print('Preview is starting; its addresses will appear in the original terminal.', flush=True)
        return
    print(f"Local: {state['local_url']}", flush=True)
    if state.get('public_url'):
        print(f"Public HTTPS: {state['public_url']}", flush=True)
    else:
        print('No public HTTPS address is ready. Check the preview terminal.', flush=True)


@contextlib.contextmanager
def preview_lock():
    LOCK.parent.mkdir(parents=True, exist_ok=True)
    with LOCK.open('a') as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('Preview is already running; keeping its existing tunnel.', flush=True)
            show_status()
            yield False
            return
        STATE.unlink(missing_ok=True)
        try:
            yield True
        finally:
            STATE.unlink(missing_ok=True)


@contextlib.contextmanager
def build_lock():
    BUILD_LOCK.parent.mkdir(parents=True, exist_ok=True)
    with BUILD_LOCK.open('a') as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        yield


def publish(source, destination):
    files = sorted((path for path in source.rglob('*') if path.is_file()), key=lambda path: path == source / 'index.html')
    for path in files:
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.publish-', delete=False) as temporary:
            pending = Path(temporary.name)
        try:
            shutil.copyfile(path, pending)
            pending.replace(target)
        finally:
            pending.unlink(missing_ok=True)


def tunnel_settings(local_url):
    override = os.environ.get('PASSPORT_WEB_TUNNEL_CONFIG')
    path = Path(override).expanduser() if override else CONFIG
    if not override and not path.exists():
        path = LEGACY_CONFIG
    if not path.is_file():
        if override:
            raise ValueError('PASSPORT_WEB_TUNNEL_CONFIG does not point to a file')
        return {}, None
    settings = json.loads(path.read_text())
    if not isinstance(settings, dict):
        raise ValueError('Fixed tunnel configuration must be a JSON object')
    tunnel = settings.get('tunnel', '')
    hostname = settings.get('hostname', '')
    credential_path = settings.get('credentials_file', '')
    if not isinstance(tunnel, str) or not re.fullmatch(r'[a-f\d]{8}(?:-[a-f\d]{4}){3}-[a-f\d]{12}', tunnel):
        raise ValueError('Fixed tunnel configuration requires a tunnel UUID')
    if not isinstance(hostname, str) or len(hostname) > 253 or not re.fullmatch(r'(?:[a-z\d](?:[a-z\d-]{0,61}[a-z\d])?\.)+[a-z]{2,63}', hostname):
        raise ValueError('Fixed tunnel configuration requires a bare DNS hostname')
    if not isinstance(credential_path, str) or not credential_path:
        raise ValueError('Fixed tunnel configuration requires credentials_file')
    credentials = Path(credential_path).expanduser().resolve()
    if not credentials.is_file() or credentials.is_relative_to(ROOT.resolve()):
        raise ValueError('Tunnel credentials must be an existing file outside the repository')
    configuration = {
        'tunnel': tunnel,
        'credentials-file': str(credentials),
        'ingress': [{'hostname': hostname, 'service': local_url}, {'service': 'http_status:404'}],
    }
    return configuration, f'https://{hostname}'


def stop_tunnel(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


@contextlib.contextmanager
def tunnel(state):
    executable = shutil.which('cloudflared')
    if executable is None:
        print('cloudflared not found; serving locally only. Install it to enable public HTTPS.', flush=True)
        yield
        return
    configuration, fixed_url = tunnel_settings(state['local_url'])
    with tempfile.TemporaryDirectory(prefix='passport-web-tunnel-') as directory:
        config_path = Path(directory) / 'config.json'
        config_path.write_text(json.dumps(configuration))
        command = [executable, 'tunnel', '--config', str(config_path), '--no-autoupdate', '--metrics', '127.0.0.1:0']
        command += ['run'] if fixed_url else ['--url', state['local_url']]
        print(f'Fixed tunnel: {fixed_url}' if fixed_url else 'Starting Quick Tunnel; its address stays unchanged until this process stops.', flush=True)
        environment = {key: value for key, value in os.environ.items() if not key.startswith('TUNNEL_')}
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=environment)
        stopping = threading.Event()
        process_lock = threading.Lock()

        def available(url):
            if state.get('public_url') != url:
                state['public_url'] = url
                save_state(state)
                print(f'Public HTTPS: {url}' if url else 'Public HTTPS is unavailable; local preview remains available.', flush=True)

        def output():
            nonlocal process
            for attempt in range(3):
                current = process
                public_url = fixed_url
                connections = set()
                expired = False
                try:
                    for line in current.stdout:
                        print(line.rstrip(), flush=True)
                        match = re.search(r'https://[a-z\d-]+\.trycloudflare\.com\b', line)
                        if not fixed_url and match:
                            public_url = match.group()
                        connection = re.search(r'\bconnIndex=(\d+)\b', line)
                        index = connection.group(1) if connection else '0'
                        if 'Unauthorized: Tunnel not found' in line:
                            expired = True
                            available(None)
                            break
                        if 'Registered tunnel connection' in line:
                            connections.add(index)
                            if public_url:
                                available(public_url)
                        elif 'Connection terminated' in line or 'Unregistered tunnel connection' in line:
                            connections.discard(index)
                            if not connections:
                                available(None)
                    if expired:
                        stop_tunnel(current)
                    else:
                        current.wait()
                finally:
                    current.stdout.close()
                available(None)
                if stopping.is_set():
                    return
                if expired and not fixed_url and attempt < 2:
                    print('Quick Tunnel expired; creating a replacement address.', flush=True)
                    with process_lock:
                        if stopping.is_set():
                            return
                        try:
                            process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=environment)
                        except OSError as error:
                            print(f'Could not restart Quick Tunnel: {error}', flush=True)
                            return
                else:
                    print('Tunnel stopped. Local preview remains available; restart the preview to reconnect.', flush=True)
                    return

        reader = threading.Thread(target=output, daemon=True)
        reader.start()
        try:
            yield process
        finally:
            stopping.set()
            with process_lock:
                stop_tunnel(process)
            reader.join()


def serve(directory, port=4173):
    with preview_lock() as acquired:
        if not acquired:
            return
        handler = partial(SimpleHTTPRequestHandler, directory=str(directory))
        try:
            server = ThreadingHTTPServer(('127.0.0.1', port), handler)
        except OSError as error:
            if error.errno != errno.EADDRINUSE:
                raise
            server = ThreadingHTTPServer(('127.0.0.1', 0), handler)
            print(f'Port {port} is occupied; using free port {server.server_port}. The existing service is untouched.', flush=True)
        with server:
            state = {'local_url': f'http://127.0.0.1:{server.server_port}', 'public_url': None}
            save_state(state)
            print(f"Local: {state['local_url']}\nRun make web again to rebuild and reuse this preview. Stop: Ctrl+C.", flush=True)
            with tunnel(state):
                server.serve_forever()
