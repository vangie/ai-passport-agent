[English](README.md) · **简体中文**

# OpenClaw 网关兼容入口

后端唯一维护源是 [Valet Gateway](../../integrations/valet-gateway/tools/openclaw/README.zh_CN.md)。
先运行 `git submodule update --init --recursive`，从
`integrations/valet-gateway/tools/openclaw` 安装和打包，不再安装此兼容目录。
`doctor.mjs`、`manage-plugin.py`、`install-passport-relay.py` 会转到固定版本的实现。
已安装插件和设备凭据继续有效；源码迁移不会部署、重启或要求重新配对。
