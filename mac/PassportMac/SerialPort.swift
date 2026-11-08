import Darwin
import Foundation

/// 工牌插在电脑上时，配置走这条串口，命令和蓝牙是同一套。
enum SerialPort {
    /// 打开第一只能回答 HELLO 的口，并把同一个句柄交给配置连接继续使用。
    /// 不能只做一次探测后关闭，否则后续 SAVE 没有连接可以发送。
    static func openResponding() -> (Int32, String, String)? {
        let ordered = candidates().sorted {
            ($0.contains("usbmodem") ? 0 : 1) < ($1.contains("usbmodem") ? 0 : 1)
        }
        for path in ordered {
            guard let fd = openConfigured(path) else { continue }
            if let hello = probe(fd) {
                return (fd, path, hello)
            }
            close(fd)
        }
        return nil
    }

    private static func openConfigured(_ path: String) -> Int32? {
        let fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard fd >= 0 else { return nil }
        var attributes = termios()
        // USB Serial/JTAG 不是传统 UART，部分 macOS 驱动不提供 termios。
        // 能配置时配置；不能配置时仍保留句柄，让 USB JTAG 自己处理速率。
        if tcgetattr(fd, &attributes) == 0 {
            cfmakeraw(&attributes)
            cfsetspeed(&attributes, speed_t(B115200))
            attributes.c_cflag |= tcflag_t(CLOCAL | CREAD)
            _ = tcsetattr(fd, TCSANOW, &attributes)
            _ = tcflush(fd, TCIOFLUSH)
        }
        return fd
    }

    static func completedHello(in output: Data) -> String? {
        // A prefix alone may arrive before its firmware identity. Preserve the
        // complete response rather than consuming it inside the port probe.
        let prefix = Data("HELLO ai-passport-openclaw ".utf8)
        guard let start = output.range(of: prefix)?.lowerBound,
              let end = output[start...].firstIndex(of: 0x0a) else { return nil }
        let line = String(decoding: output[start..<end], as: UTF8.self)
            .trimmingCharacters(in: .controlCharacters)
        let parts = line.split(separator: " ")
        guard parts.count >= 6, UInt(parts[2]) != nil, Int(parts[3]) != nil, Int(parts[4]) != nil else { return nil }
        return line
    }

    private static func probe(_ fd: Int32) -> String? {
        let command = Array("HELLO\n".utf8)
        _ = command.withUnsafeBytes { raw in
            Darwin.write(fd, raw.baseAddress, raw.count)
        }
        var output = Data()
        var buffer = [UInt8](repeating: 0, count: 1024)
        let deadline = Date().addingTimeInterval(8)
        var nextHello = Date().addingTimeInterval(1)
        while Date() < deadline {
            // USB can reappear before the application installs its receive driver.
            if Date() >= nextHello {
                _ = command.withUnsafeBytes { Darwin.write(fd, $0.baseAddress, $0.count) }
                nextHello = Date().addingTimeInterval(1)
            }
            var descriptor = pollfd(fd: fd, events: Int16(POLLIN), revents: 0)
            let wait = Darwin.poll(&descriptor, 1, 250)
            if wait < 0 {
                if errno == EINTR { continue }
                return nil
            }
            if wait == 0 { continue }
            let count = Darwin.read(fd, &buffer, buffer.count)
            if count > 0 {
                output.append(contentsOf: buffer.prefix(count))
                if let hello = completedHello(in: output) { return hello }
                if output.count > 16384 { output = output.suffix(8192) }
            } else if count == 0 {
                return nil
            } else if errno != EAGAIN && errno != EINTR {
                return nil
            }
        }
        return nil
    }

    static func candidates() -> [String] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: "/dev")) ?? []
        return names
            .filter { $0.hasPrefix("cu.usbmodem") || $0.hasPrefix("cu.usbserial") || $0.hasPrefix("cu.wchusbserial") }
            .sorted()
            .map { "/dev/" + $0 }
    }

}
