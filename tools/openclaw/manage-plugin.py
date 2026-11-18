"""Compatibility command; implementation lives in the shared gateway checkout."""
from pathlib import Path
import runpy
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gateway_source import gateway_root

if __name__ == "__main__":
    try:
        source = gateway_root() / "tools/openclaw/manage-plugin.py"
    except ValueError as error:
        sys.exit(str(error))
    runpy.run_path(str(source), run_name="__main__")
