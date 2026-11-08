import Foundation

@main struct PairPromptTests {
    static func main() throws {
        func code(_ url: String, token: String = "test-bootstrap") throws -> String {
            let data = try JSONSerialization.data(withJSONObject: ["url": url, "bootstrapToken": token])
            return data.base64EncodedString()
        }
        let first = try code("wss://first.example")
        let second = try code("wss://second.example")
        precondition(PairPrompt.decode(payload: first)?.url == "wss://first.example")
        precondition(PairPrompt.resolve(url: "", code: first)?.code == first)
        precondition(PairPrompt.resolve(url: "wss://first.example/", code: first)?.url == "wss://first.example")
        // The address tested by the app must equal the address the badge decodes.
        precondition(PairPrompt.resolve(url: "wss://first.example", code: second) == nil)
        precondition(PairPrompt.resolve(url: "wss://first.example", code: "not-a-code") == nil)
        let wrapped = String(first.prefix(15)) + "\n \t" + String(first.dropFirst(15))
        precondition(PairPrompt.decode(payload: wrapped)?.code == first)
        precondition(PairPrompt.decode(payload: "oc-pair://" + first)?.code == first)
        let qr = try JSONSerialization.data(withJSONObject: ["url": "wss://first.example", "code": first])
        precondition(PairPrompt.decode(payload: String(decoding: qr, as: UTF8.self))?.code == first)
        let misleadingQR = try JSONSerialization.data(withJSONObject: ["url": "wss://wrong.example", "code": first])
        precondition(PairPrompt.decode(payload: String(decoding: misleadingQR, as: UTF8.self)) == nil)
        let missingToken = Data("{\"url\":\"wss://first.example\"}".utf8).base64EncodedString()
        precondition(PairPrompt.decode(payload: missingToken) == nil)
        let emptyToken = try code("wss://first.example", token: "")
        let wrongScheme = try code("https://first.example")
        let noHost = try code("wss://")
        precondition(PairPrompt.decode(payload: emptyToken) == nil)
        precondition(PairPrompt.decode(payload: wrongScheme) == nil)
        precondition(PairPrompt.decode(payload: noHost) == nil)
        precondition(PairPrompt.decode(payload: "") == nil)
        func link(_ items: [(String, String)]) -> URL {
            var url = URLComponents()
            url.scheme = ConfigurationLink.scheme
            url.host = "pair"
            url.queryItems = items.map { URLQueryItem(name: $0.0, value: $0.1) }
            return url.url!
        }
        let imported = try ConfigurationLink.parse(link([("code", first), ("ssid", "家里 Wi-Fi"),
                                                       ("password", "pass+word & ="), ("name", "家里")]))
        precondition(imported.setupCode == first && imported.gatewayURL == "wss://first.example")
        precondition(imported.ssid == "家里 Wi-Fi" && imported.password == "pass+word & =")
        let codeOnly = try ConfigurationLink.parse(link([("code", second)]))
        precondition(codeOnly.ssid == nil && codeOnly.password == nil && codeOnly.name == nil)
        for bad in [link([("code", first), ("code", second)]),
                    link([("code", first), ("url", "wss://wrong.example")]),
                    link([("password", "orphan-password")]),
                    link([("code", "invalid")]),
                    link([("code", first), ("save", "true")]),
                    URL(string: "https://pair?code=unused")!,
                    URL(string: "ai-passport-openclaw://pair")!] {
            do { _ = try ConfigurationLink.parse(bad); preconditionFailure("invalid link accepted") }
            catch {}
        }
        print("Pairing: embedded address, mismatch rejection, folded codes and QR validation PASS")
        print("Deep links: validated imports, field preservation, special characters, duplicate and unsafe-action rejection PASS")
    }
}
