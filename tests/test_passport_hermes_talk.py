import asyncio
import base64
import contextlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

from aiohttp import web

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("passport_hermes", ROOT / "tools/hermes/__init__.py",
                                            submodule_search_locations=[str(ROOT / "tools/hermes")])
PACKAGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PACKAGE
SPEC.loader.exec_module(PACKAGE)
from passport_hermes.passport_auth import ProtocolError
from passport_hermes.passport_ima import decode_audio, decode_ima, encode_ima
from passport_hermes.passport_talk import CONSULT, END_CALL, TalkSession, amplify_pcm, session_settings, talk_config


class CodecTests(unittest.TestCase):
    def test_matches_existing_javascript_wire_codec(self):
        for count in (1, 2, 479, 480, 481, 1919, 1920):
            pcm = b"".join(struct.pack("<h", (sample * 997) % 65536 - 32768) for sample in range(count))
            for index in (0, 47, 88):
                encoded, final_index = encode_ima(pcm, index)
                script = """import {encodeIma,decodeIma} from './tools/openclaw/passport-ima.mjs';
const state={index:Number(process.argv[2])};
const encoded=encodeIma(Buffer.from(process.argv[1],'base64'),state);
console.log(JSON.stringify([encoded.toString('base64'),state.index,decodeIma(encoded).toString('base64')]));"""
                expected = json.loads(subprocess.check_output(["node", "--input-type=module", "-e", script,
                    base64.b64encode(pcm).decode(), str(index)], cwd=ROOT, text=True))
                self.assertEqual([base64.b64encode(encoded).decode(), final_index,
                                  base64.b64encode(decode_ima(encoded)).decode()], expected)
                self.assertEqual(decode_audio("passport-ima:" + expected[0]), decode_ima(encoded))

    def test_invalid_audio_bounds(self):
        for value in (None, "", "passport-ima:!!!!", "passport-ima:AA==", "passport-silence:0",
                      "passport-silence:3842", "passport-silence:3", "passport-silence:１２"):
            with self.assertRaises(ProtocolError):
                decode_audio(value)
        self.assertEqual(decode_audio("passport-silence:3840"), bytes(3840))

    def test_private_scoped_configuration(self):
        self.assertIsNone(talk_config({}, lambda name: "secret"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "key"
            path.write_text("private-key")
            config = {"realtime": {"enabled": True, "api_key_file": str(path)}}
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                talk_config(config, lambda name: None)
            path.chmod(0o600)
            settings = talk_config(config, lambda name: None)
            self.assertEqual(settings["key"], "private-key")
            self.assertEqual(session_settings(settings, "zh", "Test persona")["session"]["audio"]["input"]["transcription"]["language"], "zh")
        for endpoint in ("ws://gateway.example", "wss://user:secret@gateway.example", "wss://gateway.example?key=secret", 42):
            with self.assertRaises(ValueError):
                talk_config({"realtime": {"enabled": True, "url": endpoint}}, lambda name: "secret")
        for gain in (-1, 13, True, "6", float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                talk_config({"realtime": {"enabled": True, "output_gain_db": gain}}, lambda name: "secret")
        settings = talk_config({"realtime": {"enabled": True, "output_gain_db": 6}}, lambda name: "secret")
        self.assertEqual(settings["output_gain_db"], 6)

    def test_output_gain_preserves_silence_timing_and_limits_peaks(self):
        samples = (0, 1000, -1000, 12000, -12000, 20000, -20000, 32767, -32768)
        pcm = struct.pack("<9h", *samples)
        self.assertIs(amplify_pcm(pcm, 0), pcm)
        amplified = amplify_pcm(pcm, 6.020599913279624)
        result = struct.unpack("<9h", amplified)
        self.assertEqual(len(amplified), len(pcm))
        self.assertEqual(result[:5], (0, 2000, -2000, 24000, -24000))
        self.assertTrue(26000 < result[5] < result[7] <= 32767)
        self.assertEqual(result[5], -result[6])
        self.assertGreaterEqual(result[8], -32767)
        self.assertEqual(amplify_pcm(bytes(3840), 12), bytes(3840))
        with self.assertRaises(ProtocolError):
            amplify_pcm(b"\x00", 6)


class TalkTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.events = []
        self.frames = []
        self.allowed = True
        self.consulted = []

        async def send(frame):
            self.events.append(frame["payload"])

        async def consult(question, context):
            self.consulted.append((question, context))
            return {"answer": "Tool result"}

        self.call = TalkSession("device", {"url": "wss://api.openai.com/v1/realtime", "model": "test", "key": "test", "voice": "coral"},
                                "zh", "Test persona", send, consult, lambda: self.allowed)

        async def upstream(frame):
            self.frames.append(frame)

        self.call.upstream = upstream

    async def asyncTearDown(self):
        await self.call.close()

    async def test_playback_marks_interrupt_and_late_audio(self):
        self.call.player = asyncio.create_task(self.call.playback())
        await self.call.handle({"type": "response.created", "response": {"id": "reply-1"}})
        await self.call.handle({"type": "response.output_audio.delta", "response_id": "reply-1", "item_id": "item-1",
                               "delta": base64.b64encode(bytes(1920)).decode()})
        await self.call.handle({"type": "response.output_audio.done", "response_id": "reply-1", "item_id": "item-1"})
        await asyncio.sleep(0.02)
        mark = next(event["markName"] for event in self.events if event["type"] == "mark")
        self.assertEqual(mark, "audio-1")
        await self.call.acknowledge(mark)
        await self.call.interrupt()
        self.assertIn({"type": "conversation.item.truncate", "item_id": "item-1", "content_index": 0, "audio_end_ms": 40}, self.frames)
        await self.call.acknowledge(mark)
        await self.call.handle({"type": "response.output_audio.delta", "response_id": "reply-1", "item_id": "item-1", "delta": "AAAA"})
        self.assertFalse(self.call.queue)
        sequence = self.call.sequence
        await self.call.handle({"type": "response.created", "response": {"id": "reply-2"}})
        await asyncio.sleep(0.02)
        self.assertGreater(self.call.sequence, sequence)

    async def test_server_vad_does_not_cancel_device_owned_playback(self):
        settings = session_settings(self.call.config, "zh", "Test persona")["session"]
        detection = settings["audio"]["input"]["turn_detection"]
        self.assertTrue(detection["create_response"])
        self.assertFalse(detection["interrupt_response"])
        await self.call.handle({"type": "response.created", "response": {"id": "reply-1"}})
        await self.call.handle({"type": "response.output_audio.delta", "response_id": "reply-1",
                               "item_id": "item-1", "delta": base64.b64encode(bytes(1920)).decode()})
        queued = list(self.call.queue)
        await self.call.handle({"type": "input_audio_buffer.speech_started"})
        self.assertTrue(self.call.speaking)
        self.assertTrue(self.call.response_active)
        self.assertEqual(list(self.call.queue), queued)
        self.assertFalse(self.frames)
        self.assertFalse(any(event["type"] == "clear" for event in self.events))
        await self.call.handle({"type": "input_audio_buffer.speech_stopped"})
        self.assertFalse(self.call.speaking)
        await self.call.interrupt()
        self.assertFalse(self.call.queue)
        self.assertIn({"type": "response.cancel"}, self.frames)
        self.assertEqual(self.events[-1]["type"], "clear")

    async def test_output_gain_is_applied_before_device_encoding(self):
        self.call.config["output_gain_db"] = 6
        self.call.player = asyncio.create_task(self.call.playback())
        pcm = struct.pack("<960h", *([1000, -1000] * 480))
        await self.call.handle({"type": "response.output_audio.delta", "item_id": "item-gain",
                               "delta": base64.b64encode(pcm).decode()})
        await self.call.handle({"type": "response.output_audio.done", "item_id": "item-gain"})
        async with asyncio.timeout(2):
            while not any(event["type"] == "audioDone" for event in self.events):
                await asyncio.sleep(0.01)
        audio = next(event for event in self.events if event["type"] == "audio")
        decoded = decode_audio(audio["audioBase64"])
        self.assertEqual(len(decoded), len(pcm))
        self.assertGreater(sum(abs(sample) for sample, in struct.iter_unpack("<h", decoded)) / 960, 1800)
        self.assertEqual(self.call.items["item-gain"]["sent"], len(pcm))

    async def test_native_consult_waits_for_current_response(self):
        await self.call.handle({"type": "response.created", "response": {"id": "reply-1"}})
        tool = {"type": "response.function_call_arguments.done", "call_id": "tool-1", "name": CONSULT,
                "arguments": json.dumps({"question": "Check weather", "context": "Shanghai"})}
        await self.call.handle(tool)
        await self.call.work
        self.assertEqual(len(self.consulted), 1)
        self.assertEqual([frame["type"] for frame in self.frames], ["conversation.item.create"])
        await self.call.handle(tool)
        self.assertEqual(len(self.consulted), 1)
        await self.call.handle({"type": "response.done", "response": {"id": "reply-1", "status": "completed"}})
        self.assertEqual(self.frames[-1]["type"], "response.create")

    async def test_synthetic_session_has_no_agent_tools(self):
        self.call.config["synthetic"] = True
        settings = session_settings(self.call.config, "zh", "Test persona")["session"]
        self.assertEqual(settings["tools"], [])
        self.assertEqual(settings["tool_choice"], "none")
        await self.call.handle({"type": "response.function_call_arguments.done", "call_id": "synthetic-tool",
                               "name": CONSULT, "arguments": '{"question":"Run a command"}'})
        await self.call.work
        self.assertFalse(self.consulted)

    async def test_revocation_and_queue_bounds(self):
        self.allowed = False
        with self.assertRaises(ProtocolError):
            await self.call.emit("ready")
        with self.assertRaises(ProtocolError):
            self.call.enqueue({"type": "audio"}, 2 * 1024 * 1024 + 1)

    async def test_close_cancels_owned_work_once(self):
        self.call.work = asyncio.create_task(asyncio.Event().wait())
        self.call.player = asyncio.create_task(asyncio.Event().wait())
        await asyncio.gather(self.call.close(), self.call.close())
        self.assertTrue(self.call.work.cancelled())
        self.assertTrue(self.call.player.cancelled())
        self.assertEqual(sum(event["type"] == "close" for event in self.events), 1)

    async def test_end_call_and_unknown_mark(self):
        with self.assertRaises(ProtocolError):
            await self.call.acknowledge("foreign")
        await self.call.handle({"type": "response.function_call_arguments.done", "call_id": "end", "name": END_CALL})
        self.assertTrue(self.call.closed)
        self.assertEqual(self.events[-1]["reason"], "completed")

    async def test_real_websocket_upstream_lifecycle(self):
        async def handler(request):
            socket = web.WebSocketResponse()
            await socket.prepare(request)
            settings = await socket.receive_json()
            self.assertEqual(settings["type"], "session.update")
            await socket.send_json({"type": "session.updated"})
            frame = await socket.receive_json()
            self.assertEqual(frame["type"], "input_audio_buffer.append")
            self.assertEqual(base64.b64decode(frame["audio"]), bytes(960))
            await socket.close()
            return socket

        application = web.Application()
        application.router.add_get("/realtime", handler)
        runner = web.AppRunner(application)
        await runner.setup()
        site = web.TCPSite(runner, "127.0.0.1", 0)
        await site.start()
        self.call.config["url"] = f"http://127.0.0.1:{site._server.sockets[0].getsockname()[1]}/realtime"
        del self.call.upstream
        self.call.reader = asyncio.create_task(self.call.start())
        try:
            async with asyncio.timeout(3):
                while not self.call.ready:
                    await asyncio.sleep(0.01)
            await self.call.append({"audioBase64": "passport-silence:960"})
            await asyncio.wait_for(self.call.reader, 3)
            self.assertTrue(self.call.closed)
            self.assertTrue(self.call.client.closed)
        finally:
            await runner.cleanup()


if __name__ == "__main__":
    unittest.main()
