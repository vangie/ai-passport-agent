import Foundation

/// 从这台 Mac 连一下 OpenClaw，只看地址通不通、token 对不对。
/// 不带设备身份、只要空权限，网关不会留下待批准的设备。
enum ProbeResult: Equatable {
    case testing
    case ok(version: String)
    case badToken
    case unreachable(String)
    case notOpenClaw
}

final class OpenClawProbe: NSObject, URLSessionWebSocketDelegate {
    private var task: URLSessionWebSocketTask?
    private var session: URLSession?
    private var finish: ((ProbeResult) -> Void)?
    private var timer: Timer?

    func run(url: String, token: String, done: @escaping (ProbeResult) -> Void) {
        cancel()
        guard let target = URL(string: url), target.scheme == "ws" || target.scheme == "wss" else {
            done(.unreachable(L10n.string("地址要以 wss:// 开头")))
            return
        }
        finish = done
        let session = URLSession(configuration: .ephemeral, delegate: self, delegateQueue: .main)
        let task = session.webSocketTask(with: target)
        self.session = session
        self.task = task
        timer = Timer.scheduledTimer(withTimeInterval: 8, repeats: false) { [weak self] _ in
            self?.end(.unreachable(L10n.string("8 秒内没有回应")))
        }
        task.resume()
        receive(token: token)
    }

    func cancel() {
        timer?.invalidate()
        timer = nil
        task?.cancel(with: .goingAway, reason: nil)
        session?.invalidateAndCancel()
        task = nil
        session = nil
        finish = nil
    }

    private func receive(token: String) {
        task?.receive { [weak self] result in
            guard let self else { return }
            switch result {
            case .failure(let error):
                self.end(.unreachable(Self.describe(error)))
            case .success(let message):
                guard case .string(let text) = message,
                      let frame = try? JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any] else {
                    self.end(.notOpenClaw)
                    return
                }
                if frame["event"] as? String == "connect.challenge" {
                    self.connect(token: token)
                    self.receive(token: token)
                } else if frame["type"] as? String == "res" {
                    self.end(Self.verdict(frame))
                } else {
                    self.receive(token: token)
                }
            }
        }
    }

    private func connect(token: String) {
        let frame: [String: Any] = [
            "type": "req",
            "id": "probe-1",
            "method": "connect",
            "params": [
                "minProtocol": 3,
                "maxProtocol": 4,
                "client": ["id": "cli", "version": "0.1", "platform": "macos", "mode": "probe"],
                "role": "operator",
                "scopes": [String](),
                "auth": token.isEmpty ? [String: String]() : ["token": token],
            ],
        ]
        guard let data = try? JSONSerialization.data(withJSONObject: frame),
              let text = String(data: data, encoding: .utf8) else { return }
        task?.send(.string(text)) { [weak self] error in
            if let error { self?.end(.unreachable(Self.describe(error))) }
        }
    }

    private static func verdict(_ frame: [String: Any]) -> ProbeResult {
        if frame["ok"] as? Bool == true {
            let payload = frame["payload"] as? [String: Any]
            let server = payload?["server"] as? [String: Any]
            return .ok(version: server?["version"] as? String ?? "")
        }
        let error = frame["error"] as? [String: Any]
        let detail = (error?["details"] as? [String: Any])?["code"] as? String ?? ""
        let message = error?["message"] as? String ?? ""
        // 这次测试故意不带设备身份。网关回这两句，说明地址已经到了网关。
        if detail.hasPrefix("AUTH_") || message.localizedCaseInsensitiveContains("device identity") {
            return .badToken
        }
        return .unreachable(message.isEmpty ? L10n.string("网关拒绝了连接") : message)
    }

    private static func describe(_ error: Error) -> String {
        let code = (error as NSError).code
        switch code {
        case NSURLErrorCannotFindHost, NSURLErrorDNSLookupFailed: return L10n.string("找不到这个地址")
        case NSURLErrorCannotConnectToHost: return L10n.string("连不上，网关可能没开")
        case NSURLErrorTimedOut: return L10n.string("超时了")
        case NSURLErrorSecureConnectionFailed, NSURLErrorServerCertificateUntrusted,
             NSURLErrorServerCertificateHasBadDate, NSURLErrorServerCertificateNotYetValid:
            // 开了代理时，不存在的域名也会解析成功，然后在 TLS 这一步失败。
            return L10n.string("安全连接没建立起来，检查地址或证书")
        case NSURLErrorBadServerResponse: return L10n.string("这个地址不是 OpenClaw 网关")
        default: return error.localizedDescription
        }
    }

    private func end(_ result: ProbeResult) {
        let done = finish
        cancel()
        done?(result)
    }
}
