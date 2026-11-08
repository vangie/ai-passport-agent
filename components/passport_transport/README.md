<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Passport WebSocket transport

Application-specific fork of Espressif ESP-IDF v5.5.3 `components/tcp_transport/transport_ws.c` and its public header, under Apache-2.0 (see LICENSE). The original SDK and other transports remain unchanged.

This variant retains partial frame headers, payloads and control messages across read timeouts; consumes HTTP-upgrade leftovers before polling the socket; finishes short writes before starting another frame; and rejects masked, reserved or oversized server frame headers. Receive state adds less than 200 bytes per connection. TLS certificate validation and WebSocket accept validation use the original ESP-IDF implementation. TCP_NODELAY is applied before HTTP upgrade.

Small outgoing frames reuse the existing 1024-byte upgrade buffer when it contains no unread incoming data, combining the masked header and payload into one parent write instead of two TLS writes. Larger frames and frames sent with buffered receive data use the short-write-safe fallback. The caller's payload is restored after masking, including write failures. This does not allocate another frame buffer or enlarge the TCP window.

`passport_transport_ws_last_receive_us` exposes the last monotonic receive timestamp, including control-frame traffic consumed internally. The persistent gateway uses this to detect a silent socket without mistaking valid ping/pong-only traffic for a dead connection.

The component deliberately uses the pinned SDK's private transport header. Review that interface when upgrading ESP-IDF. Host tests compile the actual component reader/writer against fault-injected I/O, including every byte split of normal and extended headers, partial ping payloads, upgrade leftovers, close frames, combined writes, write failures and one-byte writes. Run them through `tools/validate.sh --static`; firmware and device tests remain separate.
