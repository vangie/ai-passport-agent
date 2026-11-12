import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def method(source, name):
    start = re.search(rf"    private func {name}\(", source).start()
    opening = source.index("{", start)
    depth = 1
    ending = opening + 1
    while depth:
        if source[ending] == "{":
            depth += 1
        elif source[ending] == "}":
            depth -= 1
        ending += 1
    return source[start:ending].replace("private func", "func", 1)


HARNESS = r'''
import Foundation

final class LinkHarness {
    var ready = true
    var busy = true
    var status = ""
    var error: String?
    var expected = "OK saved"
    var responseComplete = false
    var rejection: String?
    var writer: BLECommandWriter? = BLECommandWriter("SAVE 1 Test test-pass")
    var completion: (() -> Void)?
    var timer: Timer?
    var candidate = PassportSnapshot()
    var nearbyRows: [NearbyNetwork] = []
    var nearby: [NearbyNetwork] = []
    var mutations: [PhoneMutation] = [.wifi(PhoneWiFi(ssid: "Test", password: "test-pass"))]
    var networkScanInProgress = false
    var disconnects = 0

    func disconnect() {
        disconnects += 1
        ready = false
        busy = false
        writer = nil
        completion = nil
        mutations = []
    }

    METHODS
}

@main struct LinkTests {
    static func main() throws {
        for response in ["ERR join notfound", "ERR join pass", "ERR join timeout", "ERR join security", "ERR stale", "ERR busy"] {
            for responseBeforeAck in [false, true] {
                let link = LinkHarness()
                var successful = false
                link.completion = { successful = true }
                precondition(link.writer?.next(maximum: 512) != nil)
                if !responseBeforeAck { link.writer?.acknowledge() }
                try link.receive(response)
                if responseBeforeAck {
                    precondition(link.busy && link.writer != nil)
                    link.writer?.acknowledge()
                    link.finishIfReady()
                }
                precondition(link.disconnects == 0 && link.ready && !link.busy)
                precondition(link.writer == nil && link.completion == nil && link.mutations.isEmpty)
                precondition(link.error == ProvisioningRejection.message(for: response) && !successful)
                link.finishIfReady()
                precondition(link.ready && link.disconnects == 0)

                link.error = nil
                link.responseComplete = false
                link.writer = BLECommandWriter("SAVE 1 Test corrected-pass")
                link.busy = true
                link.completion = { successful = true }
                precondition(link.writer?.next(maximum: 512) != nil)
                link.writer?.acknowledge()
                try link.receive("OK saved")
                precondition(successful && link.ready && !link.busy && link.disconnects == 0)
            }
        }
        let setup = LinkHarness()
        for pending in [false, true] {
            let taken = LinkHarness()
            if !pending { taken.writer = nil }
            try taken.receive("SESSION TAKEN")
            precondition(taken.disconnects == 1 && !taken.ready)
            precondition(taken.error == L10n.string("设备已被其他配置端接管，输入内容仍保留。需要时请手动重新连接。"))
        }
        setup.ready = false
        precondition(setup.writer?.next(maximum: 512) != nil)
        setup.writer?.acknowledge()
        try setup.receive("ERR protocol")
        precondition(setup.disconnects == 1 && !setup.ready)
        for responseBeforeAck in [false, true] {
            let link = LinkHarness()
            link.expected = "SCAN END"
            link.networkScanInProgress = true
            link.writer = BLECommandWriter("SCAN LIVE")
            link.completion = { link.networkScanInProgress = false }
            precondition(link.writer?.next(maximum: 512) != nil)
            if !responseBeforeAck { link.writer?.acknowledge() }
            try link.receive("SCAN LIVE")
            precondition(link.nearby.isEmpty)
            try link.receive("NET -65 Test iPhone")
            precondition(link.nearby.count == 1 && link.busy && link.networkScanInProgress)
            try link.receive("NET_KIND hotspot Test%20iPhone")
            precondition(link.nearby[0].personalHotspot)
            try link.receive("NET -40 Test iPhone")
            try link.receive("NET -70 Office Wi-Fi")
            precondition(link.nearby.count == 2 && link.nearby[0].rssi == -40)
            try link.receive("SCAN END")
            if responseBeforeAck {
                precondition(link.busy && link.networkScanInProgress)
                link.writer?.acknowledge()
                link.finishIfReady()
            }
            precondition(!link.busy && !link.networkScanInProgress && link.nearby.count == 2)
        }
        let partial = LinkHarness()
        partial.expected = "SCAN END"
        partial.networkScanInProgress = true
        precondition(partial.writer?.next(maximum: 512) != nil)
        partial.writer?.acknowledge()
        try partial.receive("NET -50 Available network")
        try partial.receive("ERR scan")
        precondition(partial.nearby.count == 1 && partial.ready && !partial.busy && !partial.networkScanInProgress)
        print("Phone link rejection, acknowledgement ordering, and retry tests: PASS")
    }
}
'''


class PhoneLinkRuntimeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("swiftc"), "Swift compiler is unavailable")
    def test_rejected_configuration_keeps_connection_for_retry(self):
        source = (ROOT / "ios/Passport/PassportLink.swift").read_text()
        methods = "\n".join(method(source, name) for name in ["receive", "finishIfReady", "fail"])
        with tempfile.TemporaryDirectory() as directory:
            harness = Path(directory) / "LinkHarness.swift"
            executable = Path(directory) / "test"
            harness.write_text(HARNESS.replace("METHODS", methods))
            subprocess.run(["swiftc", str(harness), str(ROOT / "ios/Passport/ProvisioningProtocol.swift"),
                            str(ROOT / "shared/AppLocalization.swift"),
                            "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
