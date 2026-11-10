#!/usr/bin/env python3
"""Keep Passport product names, storage and protocol identities consistent."""

from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
LEGACY_NAME = re.compile("badge", re.IGNORECASE)
SOURCE_ROOTS = {"app", "main", "components", "ios", "mac", "shared", "sim", "tools", "tests", "assets", ".github"}


class PassportNamingTests(unittest.TestCase):
    def test_source_names_and_references(self):
        result = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )
        for relative in sorted(set(result.stdout.splitlines())):
            path = ROOT / relative
            if not path.is_file():
                continue
            if Path(relative).parts[0] not in SOURCE_ROOTS and relative not in {
                "README.md", "README.zh_CN.md", "Makefile", ".gitignore", "CMakeLists.txt",
            }:
                continue
            with self.subTest(path=relative):
                self.assertIsNone(LEGACY_NAME.search(relative), "old product path remains")
                if path == Path(__file__).resolve():
                    continue
                try:
                    source = path.read_text(encoding="utf-8")
                except UnicodeDecodeError:
                    continue
                self.assertIsNone(LEGACY_NAME.search(source), "old product name remains")

    def test_product_identifiers(self):
        expected = {
            "ios/project.yml": "PRODUCT_BUNDLE_IDENTIFIER: ai.mirasim.passport",
            "tools/iphone.sh": "xcrun simctl launch booted ai.mirasim.passport",
            "main/passport_nvs.c": 'static const char *k_ns = "passport";',
            "app/passport_store.c": "#define PASSPORT_BLOB_MAGIC 0x31505350u",
            "main/passport_gateway.c": 'nvs_open("passport_id", NVS_READWRITE, &handle)',
        }
        for relative, value in expected.items():
            with self.subTest(path=relative):
                self.assertIn(value, (ROOT / relative).read_text(encoding="utf-8"))
        gateway = (ROOT / "main/passport_gateway.c").read_text(encoding="utf-8")
        self.assertIn('"agent:passport:passport-%s"', gateway)
        self.assertIn('cJSON_AddStringToObject(params, "agentId", "passport")', gateway)

    def test_nvs_namespace_lengths(self):
        for relative in ("main/passport_nvs.c", "main/passport_gateway.c"):
            source = (ROOT / relative).read_text(encoding="utf-8")
            namespaces = re.findall(r'nvs_open\("([^"\n]+)"|k_ns\s*=\s*"([^"\n]+)"', source)
            self.assertTrue(namespaces)
            for direct, constant in namespaces:
                with self.subTest(path=relative, namespace=direct or constant):
                    self.assertLessEqual(len((direct or constant).encode("utf-8")), 15)


if __name__ == "__main__":
    unittest.main()
