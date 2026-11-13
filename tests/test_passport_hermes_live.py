import asyncio
import importlib.util
import json
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("passport_hermes_tests", ROOT / "tests/test_passport_hermes.py")
HELPERS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HELPERS)


@unittest.skipUnless(os.environ.get("PASSPORT_HERMES_LIVE") == "1", "Opt-in real provider integration test")
class HermesLiveTests(unittest.IsolatedAsyncioTestCase):
    async def test_native_gateway_answers_and_remembers_followup(self):
        from aiohttp import ClientSession
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        from gateway.config import GatewayConfig, Platform, PlatformConfig
        from gateway.platform_registry import platform_registry
        from gateway.run import GatewayRunner
        from hermes_cli.config import get_hermes_home
        from hermes_cli.plugins import discover_plugins

        home = get_hermes_home()
        if not home.name.startswith("passport-hermes-live-"):
            self.fail("Real integration test requires a disposable passport-hermes-live-* home")
        discover_plugins()
        self.assertIsNotNone(platform_registry.get("ai_passport"))
        platform = Platform("ai_passport")
        config = GatewayConfig(platforms={platform: PlatformConfig(enabled=True, extra={"port": 18792, "speech_language": "zh"})},
                               sessions_dir=home / "sessions", multiplex_profiles=False)
        runner = GatewayRunner(config)
        try:
            self.assertTrue(await runner.start())
            adapter = runner.adapters[platform]
            pairing = adapter.store.issue("ws://127.0.0.1:18792")
            import base64

            credential = json.loads(base64.urlsafe_b64decode(pairing["setupCode"] + "=" * (-len(pairing["setupCode"]) % 4)))["bootstrapToken"]
            private = Ed25519PrivateKey.generate()
            async with ClientSession() as client:
                async with client.ws_connect("http://127.0.0.1:18792") as websocket:
                    challenge = (await websocket.receive_json())["payload"]
                    params = HELPERS.signed(private, challenge, credential, True)
                    identity = params["device"]["id"]
                    await websocket.send_json({"type": "req", "id": "connect", "method": "connect", "params": params})
                    self.assertTrue((await websocket.receive_json())["ok"])
                    for index, question in enumerate([
                        "Remember this test code word: amber. Reply briefly with the word only.",
                        "What test code word did I just give you? Reply with the word only."]):
                        await websocket.send_json({"type": "req", "id": "send", "method": "chat.send", "params": {
                            "sessionKey": f"agent:passport:passport-{identity}", "message": question, "idempotencyKey": f"live-{index}"}})
                        self.assertTrue((await websocket.receive_json())["ok"])
                        while True:
                            frame = await asyncio.wait_for(websocket.receive_json(), 180)
                            if frame.get("payload", {}).get("state") in {"final", "error"}:
                                payload = frame["payload"]
                                self.assertEqual(payload["state"], "final")
                                answer = payload["message"]["content"][0]["text"]
                                self.assertIn("amber", answer.lower())
                                print(f"Hermes real-provider turn {index + 1}: PASS", flush=True)
                                break
                    voice_path = os.environ.get("PASSPORT_HERMES_TEST_WAV")
                    if voice_path:
                        audio = Path(voice_path).read_bytes()
                        await websocket.send_json({"type": "req", "id": "voice", "method": "chat.send", "params": {
                            "sessionKey": f"agent:passport:passport-{identity}", "message": "", "idempotencyKey": "live-voice",
                            "attachments": [{"type": "audio", "mimeType": "audio/wav", "content": base64.b64encode(audio).decode()}]}})
                        acknowledgment = await websocket.receive_json()
                        self.assertTrue(acknowledgment["ok"])
                        while True:
                            frame = await asyncio.wait_for(websocket.receive_json(), 180)
                            if frame.get("payload", {}).get("state") in {"final", "error"}:
                                self.assertEqual(frame["payload"]["state"], "final")
                                answer = frame["payload"]["message"]["content"][0]["text"]
                                self.assertTrue(answer.strip())
                                break
                        await websocket.send_json({"type": "req", "id": "transcript", "method": "passport.transcript", "params": {
                            "sessionKey": f"agent:passport:passport-{identity}", "requestId": "live-voice",
                            "runId": acknowledgment["payload"]["runId"]}})
                        while True:
                            frame = await websocket.receive_json()
                            if frame.get("id") == "transcript":
                                self.assertTrue(frame["ok"])
                                self.assertTrue(frame["payload"]["text"].strip())
                                self.assertTrue(any("\u4e00" <= character <= "\u9fff" for character in frame["payload"]["text"]))
                                print("Hermes real-provider voice transcription and answer: PASS", flush=True)
                                break
        finally:
            await runner.stop()


if __name__ == "__main__":
    unittest.main()
