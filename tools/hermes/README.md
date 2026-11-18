[简体中文](README.zh_CN.md) · **English**

# Hermes gateway compatibility path

The maintained adapter is [Valet Gateway](../../integrations/valet-gateway/tools/hermes/README.md).
Run `git submodule update --init --recursive`, then install the complete
`integrations/valet-gateway/tools/hermes` directory under the existing
`plugins/ai-passport-hermes` path. Do not install this documentation-only folder.
Plugin ID, protocol and stored device credentials are unchanged. Firmware
updates do not automatically deploy or restart the gateway.
