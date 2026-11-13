import asyncio
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("passport_hermes", ROOT / "tools/hermes/__init__.py",
                                            submodule_search_locations=[str(ROOT / "tools/hermes")])
PACKAGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PACKAGE
SPEC.loader.exec_module(PACKAGE)
from passport_hermes.passport_auth import DeviceStore, ProtocolError, encode
from passport_hermes.passport_protocol import MAX_HISTORY_BYTES, PassportProtocol, attachment, bounded_text, wire_json
from passport_hermes.passport_speech import speech_language, transcription_hints


def signed(private, challenge, credential, bootstrap=False, talk=False):
    public = private.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    identity = hashlib.sha256(public).hexdigest()
    scopes = ["operator.read", "operator.write"] + (["operator.talk"] if talk else [])
    proof = "|".join(["v3", identity, "gateway-client", "ui", "operator", ",".join(scopes),
                      str(challenge["ts"]), credential, challenge["nonce"], "esp32c3", "ai-passport"])
    return {"minProtocol": 3, "maxProtocol": 4, "role": "operator", "scopes": scopes,
            "client": {"id": "gateway-client", "mode": "ui", "platform": "esp32c3", "deviceFamily": "ai-passport"},
            "auth": {"bootstrapToken" if bootstrap else "deviceToken": credential},
            "device": {"id": identity, "publicKey": encode(public), "signature": encode(private.sign(proof.encode())),
                       "signedAt": challenge["ts"], "nonce": challenge["nonce"]}}


def wav_payload(samples=1600, declared=480000):
    header = bytearray(58)
    header[:4] = b"RIFF"
    struct.pack_into("<I", header, 4, declared + 50)
    header[8:16] = b"WAVEfmt "
    struct.pack_into("<IHHIIHH", header, 16, 18, 7, 1, 16000, 16000, 1, 8)
    header[38:42] = b"fact"
    struct.pack_into("<II", header, 42, 4, declared)
    header[50:54] = b"data"
    struct.pack_into("<I", header, 54, declared)
    return bytes(header) + b"\xff" * samples


class HermesProtocolTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.clock = 1000
        self.store = DeviceStore(Path(self.directory.name) / "devices.sqlite3", now=lambda: self.clock)
        self.protocol = PassportProtocol(self.store, now=lambda: self.clock)
        self.private = Ed25519PrivateKey.generate()
        self.pair = self.store.issue("wss://gateway.example")
        self.bootstrap = json.loads(base64.urlsafe_b64decode(self.pair["setupCode"] + "=" * (-len(self.pair["setupCode"]) % 4)))["bootstrapToken"]
        self.challenge = self.protocol.challenge()
        self.identity, self.token = self.store.authenticate(signed(self.private, self.challenge, self.bootstrap, True), **{
            "nonce": self.challenge["nonce"], "timestamp": self.challenge["ts"]})
        self.session = f"agent:passport:passport-{self.identity}"

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def request(self, identifier="clip-1", message="Hello"):
        return {"sessionKey": self.session, "idempotencyKey": identifier, "message": message}

    def test_one_use_pairing_then_persistent_auth(self):
        with self.assertRaises(ProtocolError):
            self.store.authenticate(signed(self.private, self.challenge, self.bootstrap, True), self.challenge["nonce"], self.challenge["ts"])
        authenticated = self.store.authenticate(signed(self.private, self.challenge, self.token), self.challenge["nonce"], self.challenge["ts"])
        self.assertEqual(authenticated, (self.identity, self.token))
        self.assertEqual(Path(self.directory.name, "devices.sqlite3").stat().st_mode & 0o777, 0o600)
        self.assertNotIn(self.token, self.store.db.execute("SELECT token_hash FROM devices").fetchone()[0])
        self.assertTrue(self.store.authorized(self.identity, self.token))
        self.store.revoke(self.identity)
        self.assertFalse(self.store.authorized(self.identity, self.token))
        with self.assertRaises(ProtocolError):
            self.store.authenticate(signed(self.private, self.challenge, self.token), self.challenge["nonce"], self.challenge["ts"])

    def test_expired_pair_and_invalid_signature_do_not_consume_grant(self):
        pair = self.store.issue("wss://gateway.example")
        credential = json.loads(base64.urlsafe_b64decode(pair["setupCode"] + "=" * (-len(pair["setupCode"]) % 4)))["bootstrapToken"]
        params = signed(self.private, self.challenge, credential, True)
        params["device"]["signature"] = encode(bytes(64))
        with self.assertRaises(ProtocolError):
            self.store.authenticate(params, self.challenge["nonce"], self.challenge["ts"])
        self.assertEqual(self.store.db.execute("SELECT count(*) FROM pairing").fetchone()[0], 1)
        self.clock += 601
        with self.assertRaises(ProtocolError):
            self.store.authenticate(signed(self.private, self.challenge, credential, True), self.challenge["nonce"], self.challenge["ts"])

    def test_replay_and_scope_escalation_rejected(self):
        params = signed(self.private, self.challenge, self.token)
        with self.assertRaises(ProtocolError):
            self.store.authenticate(params, "another-challenge", self.challenge["ts"])
        params["scopes"].append("operator.admin")
        with self.assertRaises(ProtocolError):
            self.store.authenticate(params, self.challenge["nonce"], self.challenge["ts"])

    async def test_stream_history_transcript_and_deduplication(self):
        delivered = []

        async def send(frame):
            delivered.append(frame)

        self.protocol.subscribers[self.identity] = {send}
        run, content, fresh = self.protocol.prepare(self.identity, self.request())
        self.assertTrue(fresh)
        self.assertEqual(content, ("Hello", None))
        self.assertEqual(self.protocol.prepare(self.identity, self.request())[0]["run"], run["run"])
        with self.assertRaises(ProtocolError):
            self.protocol.prepare(self.identity, self.request(message="Changed"))
        with self.assertRaises(ProtocolError):
            self.protocol.prepare(self.identity, self.request("clip-2"))
        await self.protocol.update(self.identity, "clip-1", text="Answer", transcript="Question")
        await self.protocol.update(self.identity, "clip-1", state="final")
        self.assertEqual([frame["payload"]["state"] for frame in delivered], ["delta", "final"])
        history = self.protocol.history(self.identity, {"sessionKey": self.session})
        self.assertEqual(history["messages"][-1]["__openclaw"]["runId"], run["run"])
        self.assertEqual(history["sessionInfo"]["activeRunIds"], [])
        transcript = self.protocol.transcript(self.identity, {"sessionKey": self.session, "requestId": "clip-1", "runId": run["run"]})
        self.assertEqual(transcript["text"], "Question")

    def test_foreign_session_and_unsupported_call_rejected(self):
        with self.assertRaises(ProtocolError):
            self.protocol.history(self.identity, {"sessionKey": "agent:passport:passport-other"})
        with self.assertRaises(ProtocolError):
            self.protocol.dispatch(self.identity, "talk.session.create", {})
        self.assertFalse(self.protocol.dispatch(self.identity, "passport.status", {})["upstreamConfigured"])

    async def test_history_fits_device_projection_and_preserves_latest_run(self):
        for index in range(8):
            self.clock += 1
            request = f"long-{index}"
            run, _, _ = self.protocol.prepare(self.identity, self.request(request))
            await self.protocol.update(self.identity, request, text="\u6797\u51b2\"\\\n" * 1000, state="final")
        history = self.protocol.history(self.identity, {"sessionKey": self.session})
        self.assertLessEqual(len(wire_json(history).encode()), MAX_HISTORY_BYTES)
        self.assertEqual(history["messages"][-1]["__openclaw"]["runId"], run["run"])
        self.assertTrue(history["messages"][-1]["content"][0]["text"])
        frame = {"type": "res", "id": "passport-history", "ok": True, "payload": history}
        self.assertLess(len(wire_json(frame).encode()), 6144)

    async def test_history_remains_bounded_with_escaped_control_characters(self):
        run, _, _ = self.protocol.prepare(self.identity, self.request())
        await self.protocol.update(self.identity, "clip-1", text="\x01" * 4096, state="final")
        history = self.protocol.history(self.identity, {"sessionKey": self.session})
        self.assertLessEqual(len(wire_json(history).encode()), MAX_HISTORY_BYTES)
        self.assertEqual(history["messages"][-1]["__openclaw"]["runId"], run["run"])
        self.assertTrue(history["messages"][-1]["content"][0]["text"])

    async def test_aborted_receipt_never_reruns_and_has_bounded_utf8_text(self):
        run, _, _ = self.protocol.prepare(self.identity, self.request())
        await self.protocol.update(self.identity, "clip-1", text="\u56de\u590d" * 2000, state="aborted")
        repeated, content, fresh = self.protocol.prepare(self.identity, self.request())
        self.assertEqual(repeated["run"], run["run"])
        self.assertLessEqual(len(repeated["text"].encode()), 4096)
        self.assertFalse(fresh)
        self.assertIsNone(content)
        await self.protocol.update(self.identity, "clip-1", text="Late result", state="final")
        self.assertEqual(self.protocol.get_run(self.identity, "clip-1")["state"], "aborted")

    def test_restart_marks_unknown_runs_failed_without_resubmitting(self):
        run, _, _ = self.protocol.prepare(self.identity, self.request())
        restarted = PassportProtocol(self.store)
        retried, content, fresh = restarted.prepare(self.identity, self.request())
        self.assertEqual(retried["run"], run["run"])
        self.assertEqual(retried["state"], "error")
        self.assertFalse(fresh)
        self.assertIsNone(content)

    def test_streamed_mulaw_header_normalized_and_audio_bounded(self):
        params = self.request(message="")
        params["attachments"] = [{"type": "audio", "mimeType": "audio/wav", "content": base64.b64encode(wav_payload()).decode()}]
        question, audio = attachment(params)
        self.assertEqual(question, "")
        self.assertEqual(struct.unpack_from("<I", audio, 54)[0], 1600)
        self.assertEqual(struct.unpack_from("<I", audio, 4)[0], len(audio) - 8)
        params["attachments"][0]["content"] = base64.b64encode(wav_payload(samples=480001, declared=480001)).decode()
        with self.assertRaises(ProtocolError):
            attachment(params)
        params["attachments"][0]["content"] = "not base64!"
        with self.assertRaises(ProtocolError):
            attachment(params)

    def test_utf8_boundary_and_private_transport(self):
        self.assertEqual(bounded_text("\u6797\u51b2", 5), "\u6797")
        for url in ["ws://gateway.example", "wss://user:pass@gateway.example", "wss://gateway.example/?secret=1"]:
            with self.assertRaises(ProtocolError):
                self.store.issue(url)

    def test_language_hint_is_limited_to_passport_voice(self):
        token = speech_language.set("zh")
        try:
            self.assertEqual(transcription_hints(source="ai_passport"), {"language": "zh"})
            self.assertIsNone(transcription_hints(source="gateway"))
        finally:
            speech_language.reset(token)
        self.assertIsNone(transcription_hints(source="ai_passport"))


@unittest.skipUnless(importlib.util.find_spec("gateway"), "Requires the real Hermes runtime; protocol tests still run")
class HermesAdapterTests(unittest.IsolatedAsyncioTestCase):
    async def test_call_scope_ownership_and_exclusive_question(self):
        from unittest.mock import patch
        from aiohttp import ClientSession
        from gateway.config import PlatformConfig
        from gateway.platform_registry import PlatformEntry, platform_registry
        from passport_hermes.adapter import PassportAdapter, check_requirements

        with tempfile.TemporaryDirectory() as directory:
            platform_registry.register(PlatformEntry(name="ai_passport", label="AI Passport", adapter_factory=PassportAdapter,
                                                     check_fn=check_requirements))
            adapter = PassportAdapter(PlatformConfig(enabled=True, extra={"port": 18791}))
            adapter.home = Path(directory)
            await adapter.connect()
            adapter.realtime_config = lambda identity: {"url": "wss://gateway.example", "key": "private", "model": "test", "voice": "coral"}
            private = Ed25519PrivateKey.generate()
            pair = adapter.store.issue("ws://127.0.0.1:18791")
            code = json.loads(base64.urlsafe_b64decode(pair["setupCode"] + "=" * (-len(pair["setupCode"]) % 4)))["bootstrapToken"]

            async def start(call):
                call.ready = True
                await call.emit("ready")
                await asyncio.Event().wait()

            replacement = patch("passport_hermes.adapter.TalkSession.start", start)
            replacement.start()
            try:
                async with ClientSession() as client:
                    async with client.ws_connect("http://127.0.0.1:18791") as socket:
                        challenge = (await socket.receive_json())["payload"]
                        proof = signed(private, challenge, code, True)
                        identity = proof["device"]["id"]
                        proof["caps"] = ["passport-ima-v1"]
                        await socket.send_json({"type": "req", "id": "auth", "method": "connect", "params": proof})
                        hello = (await socket.receive_json())["payload"]
                        self.assertEqual(hello["backend"], "hermes")
                        token = hello["auth"]["deviceToken"]
                        params = {"mode": "realtime", "transport": "gateway-relay", "brain": "agent-consult", "language": "zh",
                                  "sessionKey": f"agent:passport:passport-{identity}"}
                        await socket.send_json({"type": "req", "id": "create", "method": "talk.session.create", "params": params})
                        self.assertEqual((await socket.receive_json())["error"]["code"], "FORBIDDEN")
                    async with client.ws_connect("http://127.0.0.1:18791") as socket:
                        challenge = (await socket.receive_json())["payload"]
                        proof = signed(private, challenge, token, talk=True)
                        proof["caps"] = ["passport-ima-v1"]
                        await socket.send_json({"type": "req", "id": "auth", "method": "connect", "params": proof})
                        hello = await socket.receive_json()
                        self.assertTrue(hello["ok"])
                        self.assertEqual(hello["payload"]["backend"], "hermes")
                        await socket.send_json({"type": "req", "id": "create", "method": "talk.session.create", "params": params})
                        description = (await socket.receive_json())["payload"]
                        self.assertTrue(description["passportIma"])
                        self.assertEqual((await socket.receive_json())["payload"]["type"], "ready")
                        await socket.send_json({"type": "req", "id": "ask", "method": "chat.send", "params": {}})
                        self.assertEqual((await socket.receive_json())["error"]["code"], "BUSY")
                        await socket.send_json({"type": "req", "id": "close", "method": "talk.session.close", "params": {"sessionId": "foreign"}})
                        self.assertEqual((await socket.receive_json())["error"]["code"], "FORBIDDEN")
                    async with asyncio.timeout(2):
                        while adapter.calls:
                            await asyncio.sleep(0.01)
            finally:
                replacement.stop()
                await adapter.disconnect()

    async def test_native_active_session_cancellation_stops_only_current_device(self):
        from gateway.config import PlatformConfig
        from gateway.platform_registry import PlatformEntry, platform_registry
        from passport_hermes.adapter import PassportAdapter, check_requirements

        with tempfile.TemporaryDirectory() as directory:
            platform_registry.register(PlatformEntry(name="ai_passport", label="AI Passport", adapter_factory=PassportAdapter,
                                                     check_fn=check_requirements))
            adapter = PassportAdapter(PlatformConfig(enabled=True, extra={"port": 18791}))
            adapter.home = Path(directory)
            started = asyncio.Event()
            cancelled = asyncio.Event()

            async def handler(event):
                if event.get_command() == "stop":
                    return "Stopped"
                started.set()
                try:
                    await asyncio.Event().wait()
                finally:
                    cancelled.set()

            adapter.set_message_handler(handler)
            await adapter.connect()
            try:
                identity = "cancel-device"
                adapter.store.db.execute("INSERT INTO devices VALUES (?, ?, ?)", (identity, "test", "initial-hash"))
                adapter.store.db.commit()
                params = {"sessionKey": f"agent:passport:passport-{identity}", "idempotencyKey": "active-cancel", "message": "Wait"}
                run, content, _ = adapter.protocol.prepare(identity, params)
                await adapter.submit(identity, run, content)
                await asyncio.wait_for(started.wait(), 2)
                source = adapter.turns[identity]["event"].source
                self.assertTrue(await adapter.reauthorize_native_roles(source))
                adapter.store.db.execute("UPDATE devices SET token_hash = ? WHERE id = ?", ("rotated-hash", identity))
                self.assertFalse(await adapter.reauthorize_native_roles(source))
                adapter.store.db.execute("UPDATE devices SET token_hash = ? WHERE id = ?", ("initial-hash", identity))
                with self.assertRaises(ProtocolError):
                    await adapter.abort("other-device", {"sessionKey": params["sessionKey"], "runId": run["run"]})
                self.assertFalse(cancelled.is_set())
                await adapter.abort(identity, {"sessionKey": params["sessionKey"], "runId": run["run"]})
                await asyncio.wait_for(cancelled.wait(), 2)
                self.assertNotEqual(adapter.protocol.get_run(identity, "active-cancel")["state"], "running")
            finally:
                await adapter.disconnect()

    async def test_cancelled_preparation_does_not_dispatch_and_control_hook_does_not_lose_owner(self):
        from gateway.config import PlatformConfig
        from gateway.platform_registry import PlatformEntry, platform_registry
        from gateway.platforms.event import MessageEvent, ProcessingOutcome
        from passport_hermes.adapter import PassportAdapter, check_requirements

        with tempfile.TemporaryDirectory() as directory:
            platform_registry.register(PlatformEntry(name="ai_passport", label="AI Passport", adapter_factory=PassportAdapter,
                                                     check_fn=check_requirements))
            adapter = PassportAdapter(PlatformConfig(enabled=True, extra={"port": 18791}))
            adapter.home = Path(directory)
            await adapter.connect()
            calls = []

            async def handler(event):
                calls.append(event.text)
                return "Unexpected dispatch"

            adapter.set_message_handler(handler)
            identity = "test-device"
            params = {"sessionKey": f"agent:passport:passport-{identity}", "idempotencyKey": "cancel-before-dispatch", "message": "Hello"}
            try:
                run, content, _ = adapter.protocol.prepare(identity, params)
                await adapter.abort(identity, {"sessionKey": params["sessionKey"], "runId": run["run"]})
                await adapter.submit(identity, run, content)
                self.assertEqual(calls, [])
                self.assertEqual(adapter.protocol.get_run(identity, params["idempotencyKey"])["state"], "aborted")
                source = adapter.build_source(chat_id=identity, user_id=identity)
                adapter.turns[identity] = {"request": "question-owner", "path": None}
                await adapter.on_processing_complete(MessageEvent(text="/stop", source=source, message_id="control-request"), ProcessingOutcome.SUCCESS)
                self.assertIn(identity, adapter.turns)
            finally:
                await adapter.disconnect()

    async def test_real_adapter_websocket_question_reconnect_and_no_duplicate_turn(self):
        from aiohttp import ClientSession
        from gateway.config import PlatformConfig
        from gateway.platform_registry import PlatformEntry, platform_registry
        from passport_hermes.adapter import PassportAdapter, check_requirements

        with tempfile.TemporaryDirectory() as directory:
            platform_registry.register(PlatformEntry(name="ai_passport", label="AI Passport", adapter_factory=PassportAdapter,
                                                     check_fn=check_requirements))
            adapter = PassportAdapter(PlatformConfig(enabled=True, extra={"port": 18791}))
            adapter.home = Path(directory)
            calls = []
            received = []

            async def handler(event):
                calls.append(event.text)
                received.append(event)
                return "Hermes received your question"

            adapter.set_message_handler(handler)
            await adapter.connect()
            private = Ed25519PrivateKey.generate()
            pair = adapter.store.issue("ws://127.0.0.1:18791")
            bootstrap = json.loads(base64.urlsafe_b64decode(pair["setupCode"] + "=" * (-len(pair["setupCode"]) % 4)))["bootstrapToken"]
            token = bootstrap
            identity = None
            try:
                async with ClientSession() as client:
                    for attempt in range(2):
                        async with client.ws_connect("http://127.0.0.1:18791") as websocket:
                            challenge = (await websocket.receive_json())["payload"]
                            params = signed(private, challenge, token, attempt == 0)
                            identity = params["device"]["id"]
                            await websocket.send_json({"type": "req", "id": "connect", "method": "connect", "params": params})
                            hello = await websocket.receive_json()
                            self.assertTrue(hello["ok"])
                            token = hello["payload"]["auth"]["deviceToken"]
                            await websocket.send_json({"type": "req", "id": "foreign", "method": "chat.history", "params": {
                                "sessionKey": "agent:passport:passport-another-device"}})
                            self.assertEqual((await websocket.receive_json())["error"]["code"], "FORBIDDEN")
                            await websocket.send_json({"type": "req", "id": "send", "method": "chat.send", "params": {
                                "sessionKey": f"agent:passport:passport-{identity}", "message": "Hello Hermes", "idempotencyKey": "same-recording"}})
                            self.assertTrue((await websocket.receive_json())["ok"])
                            if attempt == 0:
                                while True:
                                    event = await asyncio.wait_for(websocket.receive_json(), 10)
                                    if event.get("payload", {}).get("state") == "final":
                                        self.assertEqual(event["payload"]["message"]["content"][0]["text"], "Hermes received your question")
                                        break
                    self.assertEqual(calls, ["Hello Hermes"])
                    self.assertFalse(received[0].allow_gateway_control)
                    self.assertTrue(received[0].source.role_authorized)
                    async with client.ws_connect("http://127.0.0.1:18791") as websocket:
                        await websocket.receive_json()
                        await websocket.send_json({"type": "req", "id": "bad", "method": "chat.history", "params": {}})
                        self.assertEqual((await websocket.receive_json())["error"]["code"], "AUTH_FAILED")
                    with self.assertRaises(Exception) as denied:
                        await client.ws_connect("http://127.0.0.1:18791", headers={"Origin": "https://untrusted.example"})
                    self.assertEqual(denied.exception.status, 403)
            finally:
                await adapter.disconnect()


if __name__ == "__main__":
    unittest.main()
