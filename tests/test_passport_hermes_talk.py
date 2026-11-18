"""Run the shared gateway regression suite pinned by this firmware."""
from pathlib import Path
import runpy
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from gateway_source import gateway_root

shared = runpy.run_path(str(gateway_root() / "tests/test_passport_hermes_talk.py"), run_name="shared_gateway_tests")
globals().update({key: value for key, value in shared.items() if not key.startswith("__")})
if __name__ == "__main__":
    unittest.main()
