[简体中文](README.zh_CN.md) · **English**

# OpenClaw gateway compatibility path

The maintained adapter is [Valet Gateway](../../integrations/valet-gateway/tools/openclaw/README.md).
Initialize it with `git submodule update --init --recursive`. Install and package
from `integrations/valet-gateway/tools/openclaw`, not this compatibility directory.
`doctor.mjs`, `manage-plugin.py` and `install-passport-relay.py` forward to the
pinned implementation. Existing installed plugins and device credentials keep
working; this source move does not deploy, restart or re-pair anything.
