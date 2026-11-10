import Foundation
import CryptoKit

/// 刚编出来的应用程序。只和工牌上报的标识比较，不一致才写。
struct BuiltFirmware {
    var id: String
    var path: String
    var sha256: String
    var projectRoot: String? = nil
    /// 应用程序从这里开始，写不到 cardid。
    static let appOffset = 0x10000
    static let cardid = 0x350000

    private struct Manifest: Decodable {
        struct File: Decodable { let sha256: String; let size: Int }
        let target: String
        let full_bin_sha256: String
        let app_elf_sha256: String
        let files: [String: File]
        let image_offsets: [String: Int]
    }

    private static func hash(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    static func locate(root: String = #filePath,
                       resources: URL? = Bundle.main.resourceURL?.appendingPathComponent("Passport")) -> BuiltFirmware? {
        if let resources, FileManager.default.fileExists(atPath: resources.path) {
            return locateProject(resources)
        }
        let project = URL(fileURLWithPath: root)
            .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        return locateProject(project)
    }

    private static func locateProject(_ project: URL) -> BuiltFirmware? {
        // Only the complete gate publishes this merged image and its matching
        // archive. The incremental build/application may contain older code.
        guard let full = try? Data(contentsOf: project.appendingPathComponent("build/FoloToy-AI-Passport-full.bin")) else { return nil }
        let fullHash = hash(full)
        let archive = project.appendingPathComponent("build/firmware/\(fullHash)")
        let bin = archive.appendingPathComponent("FoloToy-AI-Passport.bin")
        guard let raw = try? Data(contentsOf: archive.appendingPathComponent("manifest.json")),
              let manifest = try? JSONDecoder().decode(Manifest.self, from: raw),
              manifest.target == "esp32c3", manifest.full_bin_sha256 == fullHash,
              manifest.image_offsets["FoloToy-AI-Passport.bin"] == appOffset,
              let record = manifest.files["FoloToy-AI-Passport.bin"],
              let data = try? Data(contentsOf: bin), record.size == data.count,
              record.sha256 == hash(data), let id = identity(in: data),
              id == String(manifest.app_elf_sha256.prefix(16)),
              full.count >= appOffset + data.count,
              full[appOffset ..< appOffset + data.count] == data else { return nil }
        return BuiltFirmware(id: id, path: bin.path, sha256: record.sha256, projectRoot: project.path)
    }

    /// ESP image header (24), segment header (8), then esp_app_desc_t.
    /// The descriptor's ELF SHA begins at byte 144; compare exactly what the passport reports.
    static func identity(in image: Data) -> String? {
        guard image.count >= 288, appOffset + image.count <= cardid,
              image[0] == 0xe9, image[12] == 5,
              Array(image[32..<36]) == [0x32, 0x54, 0xcd, 0xab] else { return nil }
        let digest = image[176..<184]
        guard digest.contains(where: { $0 != 0 }) else { return nil }
        return digest.map { String(format: "%02x", $0) }.joined()
    }

    func matches(_ reported: String) -> Bool { reported == id }
}

/// 用 USB 只写应用程序。工牌重启后，原来的配置连接会断一下。
enum FirmwareFlasher {
    static func write(port: String, built: BuiltFirmware, timeout: TimeInterval = 90) -> String? {
        let root = built.projectRoot.map { URL(fileURLWithPath: $0) }
            ?? URL(fileURLWithPath: built.path).deletingLastPathComponent().deletingLastPathComponent()
        let tool = root.appendingPathComponent("tools/flash-app.sh")
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try FileManager.default.copyItem(at: URL(fileURLWithPath: built.path),
                                            to: directory.appendingPathComponent("application.bin"))
        } catch { return "无法准备固件文件" }
        defer { try? FileManager.default.removeItem(at: directory) }
        let log = directory.appendingPathComponent("flash.log")
        FileManager.default.createFile(atPath: log.path, contents: nil)
        guard let output = try? FileHandle(forWritingTo: log) else { return "无法创建刷机日志" }
        defer { try? output.close() }
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/bin/bash")
        task.arguments = [tool.path, port, directory.appendingPathComponent("application.bin").path,
                          built.sha256]
        // esptool can fill a pipe while its parent waits. A file keeps output flowing.
        task.standardError = output
        task.standardOutput = output
        do {
            try task.run()
            let deadline = Date().addingTimeInterval(timeout)
            while task.isRunning && Date() < deadline {
                Thread.sleep(forTimeInterval: 0.1)
            }
            if task.isRunning {
                task.terminate()
                let stop = Date().addingTimeInterval(2)
                while task.isRunning && Date() < stop { Thread.sleep(forTimeInterval: 0.05) }
                if task.isRunning { kill(task.processIdentifier, SIGKILL) }
                task.waitUntilExit()
                return "刷机超时，已停止本次写入"
            }
        } catch {
            return "刷机工具没有跑起来"
        }
        if task.terminationStatus == 0 { return nil }
        let text = (try? String(contentsOf: log, encoding: .utf8)) ?? ""
        return text.split(separator: "\n").last.map(String.init) ?? "固件没写进去"
    }

    /// 工牌自己的 USB 下载口。外接串口芯片只能传配置，不能刷固件。
    static func downloadPort() -> String? {
        SerialPort.candidates().first { $0.contains("usbmodem") }
    }
}
