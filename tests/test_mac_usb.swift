import CryptoKit
import Darwin
import Foundation

@main struct USBRegressionTests {
    static func main() throws {
        var application = Data(repeating: 0, count: 512)
        application[0] = 0xe9
        application[12] = 5
        application.replaceSubrange(32..<36, with: [0x32, 0x54, 0xcd, 0xab])
        application.replaceSubrange(176..<184, with: [1, 2, 3, 4, 5, 6, 7, 8])
        precondition(BuiltFirmware.identity(in: application) == "0102030405060708")
        precondition(BuiltFirmware.identity(in: Data([0xe9])) == nil)
        precondition(ProxySettings(enabled: true, host: "proxy.example", port: 443, secret: "pw").valid)
        precondition(ProxySettings(enabled: true, host: "203.0.113.10", port: 443, hasSecret: true).valid)
        precondition(!ProxySettings(enabled: true, host: "proxy.example", port: 443).valid)
        precondition(!ProxySettings(enabled: true, host: "bad\r\nhost", port: 443, secret: "pw").valid)
        precondition(!ProxySettings(enabled: true, host: "proxy.example", port: 0, secret: "pw").valid)
        precondition(ProxySettings().valid)
        let hello = "HELLO ai-passport-openclaw 26 2 1 zh 0102030405060708 http-connect"
        let helloBytes = Data(("I (1) device log\n" + hello + "\r\n").utf8)
        for cut in 0..<helloBytes.count {
            precondition(SerialPort.completedHello(in: helloBytes.prefix(cut)) == nil,
                         "A split HELLO must retain its fingerprint until the line ends")
        }
        precondition(SerialPort.completedHello(in: helloBytes) == hello)
        application[12] = 9
        precondition(BuiltFirmware.identity(in: application) == nil)
        application[12] = 5

        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        try FileManager.default.createDirectory(at: root.appendingPathComponent("tools"),
                                                withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: root.appendingPathComponent("build"),
                                                withIntermediateDirectories: true)
        let path = root.appendingPathComponent("build/application.bin")
        try application.write(to: path)
        let built = BuiltFirmware(id: "0102030405060708", path: path.path,
                                  sha256: SHA256.hash(data: application).map { String(format: "%02x", $0) }.joined())
        precondition(built.matches("0102030405060708"))
        precondition(!built.matches("dev") && !built.matches("-") && !built.matches("other-device"))
        try checkVerifiedArchive(root: root, application: application)
        let script = root.appendingPathComponent("tools/flash-app.sh")
        // More than a pipe can buffer: waiting for process exit must not deadlock.
        try "head -c 1048576 /dev/zero\nexit 0\n".write(to: script, atomically: true, encoding: .utf8)
        precondition(FirmwareFlasher.write(port: "/dev/test-only", built: built, timeout: 5) == nil)
        try "exec /bin/sleep 10\n".write(to: script, atomically: true, encoding: .utf8)
        precondition(FirmwareFlasher.write(port: "/dev/test-only", built: built, timeout: 0.2) != nil)

        var sockets: [Int32] = [0, 0]
        precondition(socketpair(AF_UNIX, SOCK_STREAM, 0, &sockets) == 0)
        let link = ConfigConnection(fd: sockets[0])
        let received = DispatchSemaphore(value: 0)
        link.start(line: { line in
            precondition(line == "HELLO 中文")
            received.signal()
        }, ended: {})
        let bytes = Array("HELLO 中文\n".utf8)
        bytes.withUnsafeBytes { buffer in
            _ = Darwin.write(sockets[1], buffer.baseAddress, 7)
            _ = Darwin.write(sockets[1], buffer.baseAddress!.advanced(by: 7), buffer.count - 7)
        }
        precondition(received.wait(timeout: .now() + 2) == .success)
        precondition(link.send("SAVE 1 Cafe pass"))
        var readback = [UInt8](repeating: 0, count: 128)
        let count = Darwin.read(sockets[1], &readback, readback.count)
        precondition(String(decoding: readback.prefix(count), as: UTF8.self) == "SAVE 1 Cafe pass\n")
        link.stop()
        precondition(!link.send("SAVE must-not-send"))
        precondition(fcntl(sockets[0], F_GETFD) == -1)
        Darwin.close(sockets[1])
        print("Mac USB: firmware identity, split UTF-8, live writes, port release, subprocess output and timeout PASS")
    }

    static func checkVerifiedArchive(root: URL, application: Data) throws {
        func hash(_ data: Data) -> String {
            SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        }
        let source = root.appendingPathComponent("mac/PassportMac/FirmwareUpdate.swift").path
        var old = application
        old[176] = 0xaa
        try old.write(to: root.appendingPathComponent("build/FoloToy-AI-Passport.bin"))
        precondition(BuiltFirmware.locate(root: source) == nil, "An incremental image is not a verified update")
        var full = Data(repeating: 0, count: BuiltFirmware.appOffset)
        full.append(application)
        let fullHash = hash(full)
        let archive = root.appendingPathComponent("build/firmware/\(fullHash)")
        try FileManager.default.createDirectory(at: archive, withIntermediateDirectories: true)
        let fullPath = root.appendingPathComponent("build/FoloToy-AI-Passport-full.bin")
        try full.write(to: fullPath)
        let bin = archive.appendingPathComponent("FoloToy-AI-Passport.bin")
        try application.write(to: bin)
        let manifest: [String: Any] = [
            "target": "esp32c3", "full_bin_sha256": fullHash,
            "app_elf_sha256": "0102030405060708" + String(repeating: "0", count: 48),
            "image_offsets": ["FoloToy-AI-Passport.bin": BuiltFirmware.appOffset],
            "files": ["FoloToy-AI-Passport.bin": ["size": application.count, "sha256": hash(application)]],
        ]
        let manifestPath = archive.appendingPathComponent("manifest.json")
        try JSONSerialization.data(withJSONObject: manifest).write(to: manifestPath)
        guard let verified = BuiltFirmware.locate(root: source) else { preconditionFailure("Verified archive not found") }
        precondition(verified.path == bin.path && verified.projectRoot == root.path)
        precondition(verified.matches("0102030405060708") && !verified.matches("aa02030405060708"),
                     "A stale incremental image must never replace the gate output")
        var corrupted = application
        corrupted[200] ^= 1
        try corrupted.write(to: bin)
        precondition(BuiltFirmware.locate(root: source) == nil, "Reject a modified application")
        try application.write(to: bin)
        let bundled = root.appendingPathComponent("packaged/Passport")
        try FileManager.default.createDirectory(at: bundled, withIntermediateDirectories: true)
        try FileManager.default.copyItem(at: root.appendingPathComponent("build"),
                                        to: bundled.appendingPathComponent("build"))
        guard let packaged = BuiltFirmware.locate(root: "/missing/source/mac/PassportMac/FirmwareUpdate.swift", resources: bundled)
        else { preconditionFailure("Bundled firmware must work without a source checkout") }
        precondition(packaged.projectRoot == bundled.path && packaged.matches("0102030405060708"))
        try corrupted.write(to: bundled.appendingPathComponent("build/firmware/\(fullHash)/FoloToy-AI-Passport.bin"))
        precondition(BuiltFirmware.locate(root: source, resources: bundled) == nil,
                     "A corrupt bundle must not fall back to another firmware")
        full[BuiltFirmware.appOffset + 200] ^= 1
        try full.write(to: fullPath)
        precondition(BuiltFirmware.locate(root: source) == nil, "Reject an inconsistent merged image")
        print("Mac firmware source: verified archive, stale-cache rejection and tamper checks PASS")
    }
}
