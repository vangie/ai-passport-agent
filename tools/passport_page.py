#!/usr/bin/env python3
"""本机页面：PassportSim 的网页加上我们的 badge.pebundle。

安装版服务把页面编译进二进制，不读取 ?fw=badge。这里用它解出来的网页，
把固件名改成地址参数，并把自己的固件包放在同一目录。
"""

import functools
import http.server
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = Path.home() / ".local/share/passportsim/1.0.3/payload/web"
PAGE = ROOT / "sim" / "passport-web"
PORT = 8770
NEEDLE = 'Pn="official"'
PATCH = 'Pn=new URLSearchParams(location.search).get("fw")||"official"'


def prepare() -> None:
    if not WEB.is_dir():
        raise SystemExit("先安装 PassportSim")
    PAGE.mkdir(parents=True, exist_ok=True)
    for name in ("index.html", "main.js", "worker.js", "worklet.js", "styles.css",
                 "pemu_wasm.wasm", "favicon.svg", "favicon-32.png", "apple-touch-icon.png"):
        src = WEB / name
        if src.exists():
            shutil.copyfile(src, PAGE / name)
    bundle = ROOT / "build" / "badge.pebundle"
    if not bundle.exists():
        raise SystemExit("先运行 tools/pebundle.py")
    shutil.copyfile(bundle, PAGE / "badge.pebundle")
    main = PAGE / "main.js"
    text = main.read_text()
    if PATCH not in text:
        if NEEDLE not in text:
            raise SystemExit("页面里没有默认固件名")
        main.write_text(text.replace(NEEDLE, PATCH, 1))


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        super().end_headers()

    def log_message(self, fmt: str, *args: object) -> None:
        return


def main() -> None:
    prepare()
    os.chdir(PAGE)
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    print(f"http://127.0.0.1:{PORT}/?lang=zh-CN&fw=badge", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
