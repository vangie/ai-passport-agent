import Darwin
import Foundation

/// One owner closes the descriptor. Cancelling a reader must finish before esptool opens it.
final class ConfigConnection {
    private let fd: Int32
    private let lock = NSLock()
    private var running = true
    private let stopped = DispatchSemaphore(value: 0)
    private var started = false

    init(fd: Int32) { self.fd = fd }

    func start(line: @escaping (String) -> Void, ended: @escaping () -> Void) {
        started = true
        DispatchQueue.global(qos: .utility).async {
            defer {
                self.lock.lock()
                self.running = false
                Darwin.close(self.fd)
                self.lock.unlock()
                self.stopped.signal()
                ended()
            }
            var chunk = [UInt8](repeating: 0, count: 2048)
            var pending = Data()
            while self.isRunning {
                var descriptor = pollfd(fd: self.fd, events: Int16(POLLIN), revents: 0)
                let ready = Darwin.poll(&descriptor, 1, 200)
                if ready < 0 {
                    if errno == EINTR { continue }
                    break
                }
                if ready == 0 { continue }
                let n = Darwin.read(self.fd, &chunk, chunk.count)
                if n < 0 {
                    if errno == EAGAIN || errno == EINTR { continue }
                    break
                }
                if n == 0 { break }
                pending.append(contentsOf: chunk.prefix(n))
                while let newline = pending.firstIndex(of: 0x0a) {
                    line(String(decoding: pending[..<newline], as: UTF8.self)
                        .trimmingCharacters(in: .controlCharacters))
                    pending.removeSubrange(...newline)
                }
                if pending.count > 65536 { break }
            }
        }
    }

    private var isRunning: Bool {
        lock.lock()
        defer { lock.unlock() }
        return running
    }

    @discardableResult func send(_ line: String) -> Bool {
        lock.lock()
        defer { lock.unlock() }
        guard running else { return false }
        let data = Array((line + "\n").utf8)
        return data.withUnsafeBytes { raw in
            var offset = 0
            let deadline = Date().addingTimeInterval(2)
            while offset < raw.count && Date() < deadline {
                let n = Darwin.write(fd, raw.baseAddress!.advanced(by: offset), raw.count - offset)
                if n > 0 { offset += n }
                else if n < 0 && errno == EINTR { continue }
                else if n < 0 && errno == EAGAIN {
                    var descriptor = pollfd(fd: fd, events: Int16(POLLOUT), revents: 0)
                    _ = Darwin.poll(&descriptor, 1, 100)
                } else { return false }
            }
            return offset == raw.count
        }
    }

    func stop() {
        lock.lock()
        running = false
        lock.unlock()
        if started { stopped.wait() }
        else { Darwin.close(fd) }
    }
}
