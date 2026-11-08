<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# Passport WebSocket 传输

这是针对本应用维护的 Espressif ESP-IDF v5.5.3 `components/tcp_transport/transport_ws.c` 及其公开头文件分支，使用 Apache-2.0 许可证（见 LICENSE）。不修改原始 SDK 或其他传输实现。

此版本在读取超时后保留半截帧头、负载和控制消息；优先读取 HTTP 升级后缓存的数据；补完短写后才开始下一帧；拒绝服务器发来的带掩码、保留标志或过大帧头。每个连接新增的接收状态不到 200 字节。TLS 证书验证和 WebSocket accept 验证沿用 ESP-IDF 原实现。HTTP 升级前启用 TCP_NODELAY。

小型发送帧在没有未读接收数据时复用现有的 1024 字节升级缓冲，将带掩码的帧头和负载合并为一次底层写入，而不是两次 TLS 写入。较大帧或存在接收缓存时仍使用支持短写的原路径。掩码处理后会还原调用方的负载，包括写入失败的情况。不新增帧缓冲，也不扩大 TCP 窗口。

`passport_transport_ws_last_receive_us` 提供最后一次接收的单调时钟时间，包括内部处理的控制帧。常驻网关用它识别失去响应的连接，避免把仅有正常 ping/pong 往来的空闲连接误判为掉线。

该组件使用固定 SDK 的私有传输头文件。升级 ESP-IDF 时需要复核接口。主机测试使用真实组件的读写代码和可注入故障的 I/O，覆盖普通及扩展帧头的逐字节切分、分段心跳负载、升级缓存、关闭帧、合并写入、写入失败及每次仅写一字节。通过 `tools/validate.sh --static` 运行；固件构建及真机验证另行执行。
