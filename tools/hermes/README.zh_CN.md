[English](README.md) · **简体中文**

# Hermes 网关兼容入口

后端唯一维护源是 [Valet Gateway](../../integrations/valet-gateway/tools/hermes/README.zh_CN.md)。
先运行 `git submodule update --init --recursive`，再将完整的
`integrations/valet-gateway/tools/hermes` 安装到原来的 `plugins/ai-passport-hermes`。
本目录仅提供迁移说明，不能作为插件安装。插件 ID、协议和设备凭据保持兼容。
更新固件不会自动替换或重启网关。
