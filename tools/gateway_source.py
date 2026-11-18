"""Resolve the reviewed shared gateway source; never fetch or upgrade implicitly."""
from pathlib import Path
import subprocess

SUBMODULE = Path("integrations/valet-gateway")
PIN = "40cbd826504bbf65a26f50bcea604ab5a0335351"
REPOSITORY = "https://github.com/vangie/valet-gateway"


def gateway_root(root=None):
    root = Path(root) if root is not None else Path(__file__).resolve().parents[1]
    gateway = root / SUBMODULE
    required = ("tools/openclaw/package.json", "tools/openclaw/index.mjs",
                "tools/hermes/plugin.yaml", "tools/hermes/__init__.py", "LICENSE")
    if not all((gateway / name).is_file() for name in required):
        raise ValueError("Shared gateway source is missing. Run: git submodule update --init --recursive")
    if (gateway / ".git").exists():
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=gateway, text=True).strip()
        if actual != PIN:
            raise ValueError("Shared gateway checkout differs from the reviewed pin. Run: git submodule update --init --recursive")
        if subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--'], cwd=gateway).returncode:
            raise ValueError("Shared gateway has local source changes; review and pin a tested commit before packaging")
    return gateway


def repository_files(root, *, untracked=True):
    """Expand only tracked submodule sources, never caches, .git or runtime state."""
    command = ['git', 'ls-files', '--cached', '-z']
    if untracked:
        command.extend(['--others', '--exclude-standard'])
    names = subprocess.check_output(command, cwd=root).decode().split('\0')
    files = {name for name in names if name and name != SUBMODULE.as_posix()}
    gateway = gateway_root(root)
    shared = subprocess.check_output(['git', 'ls-files', '-z'], cwd=gateway).decode().split('\0')
    files.update(f'{SUBMODULE}/{name}' for name in shared if name)
    return sorted(files)


if __name__ == "__main__":
    import sys
    try:
        if sys.argv[1:] == ['files']:
            sys.stdout.buffer.write(('\0'.join(repository_files(Path(__file__).resolve().parents[1])) + '\0').encode())
        else:
            print(gateway_root())
    except ValueError as error:
        sys.exit(str(error))
