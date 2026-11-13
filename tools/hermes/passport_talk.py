import asyncio
import base64
from collections import deque
import contextlib
import json
import math
from pathlib import Path
import secrets
import struct
import time
from urllib.parse import urlencode, urlsplit

from aiohttp import ClientSession, ClientTimeout, WSMsgType

from .passport_auth import ProtocolError
from .passport_ima import decode_audio, encode_ima
from .passport_protocol import bounded_text, wire_json


DEFAULT_ENDPOINT = "wss://api.openai.com/v1/realtime"
CONSULT = "hermes_agent_consult"
END_CALL = "passport_end_call"


def talk_config(extra, get_secret):
    config = extra.get("realtime", {})
    if not isinstance(config, dict) or config.get("enabled") is not True:
        return None
    endpoint = config.get("url", DEFAULT_ENDPOINT)
    if not isinstance(endpoint, str):
        raise ValueError("Invalid realtime URL")
    parsed = urlsplit(endpoint)
    if (parsed.scheme != "wss" or not parsed.hostname or parsed.username or parsed.password or
            parsed.query or parsed.fragment or any(character.isspace() for character in endpoint)):
        raise ValueError("Realtime requires a credential-free wss:// URL")
    variable = config.get("api_key_env", "OPENAI_API_KEY")
    if not isinstance(variable, str) or not variable.isidentifier():
        raise ValueError("Invalid realtime credential variable")
    key = get_secret(variable)
    if not key and config.get("api_key_file"):
        path = Path(config["api_key_file"]).expanduser()
        if not path.is_file():
            return None
        if path.is_symlink() or path.stat().st_mode & 0o077 or path.stat().st_size > 4096:
            raise ValueError("Realtime key file must be private (0600)")
        key = path.read_text().strip()
    if not isinstance(key, str) or not key.strip() or any(character in key for character in "\r\n"):
        return None
    model, voice = config.get("model", "gpt-realtime-2.1"), config.get("voice", "coral")
    if any(not isinstance(value, str) or not value or len(value) > 128 or
           any(character.isspace() for character in value) for value in (model, voice)):
        raise ValueError("Invalid realtime model or voice")
    gain = config.get("output_gain_db", 0)
    if isinstance(gain, bool) or not isinstance(gain, (int, float)) or not 0 <= gain <= 12:
        raise ValueError("Realtime output gain must be between 0 and 12 dB")
    return {"url": endpoint, "key": key.strip(), "model": model, "voice": voice, "output_gain_db": gain}


def amplify_pcm(pcm, gain_db):
    if len(pcm) % 2:
        raise ProtocolError("Invalid upstream PCM")
    if not gain_db:
        return pcm
    factor = 10 ** (gain_db / 20)
    output = bytearray(len(pcm))
    knee = 26000
    headroom = 32767 - knee
    for offset, (sample,) in enumerate(struct.iter_unpack("<h", pcm)):
        magnitude = abs(sample * factor)
        if magnitude > knee:
            magnitude = knee + headroom * math.tanh((magnitude - knee) / headroom)
        value = round(math.copysign(magnitude, sample))
        struct.pack_into("<h", output, offset * 2, value)
    return bytes(output)


def session_settings(config, language, persona):
    instructions = (
        "You are the voice interface to the user's personal Hermes agent. "
        "Use the agent identity below without inventing memories or tool results. "
        "Answer greetings, ordinary conversation and general knowledge directly. "
        "For current information (including weather/news), private memory, files, tools or actions, "
        "call hermes_agent_consult with the concrete request and relevant conversation context. "
        "The personal agent retains all tool permissions and approval requirements. "
        "Acknowledge delegated work at most once; wait for its actual result and explain failures honestly. "
        "Ask a short clarification when speech is ambiguous. Never claim an action happened without a tool result. "
        "When the user clearly asks to end the call, use passport_end_call; do not merely say goodbye. "
        "Keep replies brief. " +
        ("Speak natural Mandarin with Simplified Chinese captions, including when tools return English. "
         if language == "zh" else "Speak natural English unless the user requests another language. ") +
        "\nAgent identity:\n" + persona
    )
    settings = {"type": "session.update", "session": {"type": "realtime", "model": config["model"],
        "output_modalities": ["audio"], "instructions": instructions,
        "audio": {"input": {"format": {"type": "audio/pcm", "rate": 24000},
            "noise_reduction": {"type": "near_field"},
            "transcription": {"model": "gpt-4o-mini-transcribe", "language": language},
            "turn_detection": {"type": "server_vad", "threshold": 0.6, "prefix_padding_ms": 300,
                               "silence_duration_ms": 650, "create_response": True, "interrupt_response": False}},
            "output": {"format": {"type": "audio/pcm", "rate": 24000}, "voice": config["voice"]}},
        "tools": [{"type": "function", "name": CONSULT,
                   "description": "Ask the personal Hermes agent to use its tools, memory and permissions.",
                   "parameters": {"type": "object", "properties": {"question": {"type": "string"},
                       "context": {"type": "string"}}, "required": ["question"], "additionalProperties": False}},
                  {"type": "function", "name": END_CALL, "description": "End this voice call when explicitly requested.",
                   "parameters": {"type": "object", "properties": {}, "additionalProperties": False}}],
        "tool_choice": "auto", "max_output_tokens": 1000}}
    if config.get("synthetic"):
        settings["session"]["tools"] = []
        settings["session"]["tool_choice"] = "none"
    return settings


class TalkSession:
    def __init__(self, identity, config, language, persona, send, consult, authorized=lambda: True):
        self.identity = identity
        self.id = secrets.token_hex(16)
        self.config = config
        self.language = language
        self.persona = persona
        self.send = send
        self.consult = consult
        self.authorized = authorized
        self.cleanup = None
        self.speaking = False
        self.resume_pending = False
        self.items = {}
        self.client = None
        self.socket = None
        self.reader = None
        self.player = None
        self.work = None
        self.ready = False
        self.closed = False
        self.queue = deque()
        self.queue_bytes = 0
        self.wake = asyncio.Event()
        self.sequence = 0
        self.index = 0
        self.due = 0
        self.output_item = None
        self.output_response = None
        self.suppressed_response = None
        self.response_active = False
        self.mark_number = 0
        self.marks = {}
        self.seen_calls = set()
        self.started = time.monotonic()

    def description(self):
        return {"sessionId": self.id, "provider": "hermes-realtime", "passportIma": True,
                "passportSilence": True, "passportProtocol": 1,
                "audio": {"inputEncoding": "pcm16", "inputSampleRateHz": 24000,
                          "outputEncoding": "pcm16", "outputSampleRateHz": 24000}}

    async def emit(self, event, **fields):
        if not self.closed:
            if not self.authorized():
                raise ProtocolError("Device credential revoked", "AUTH_FAILED")
            await self.send({"type": "event", "event": "talk.event", "payload": {
                "relaySessionId": self.id, "type": event, **fields}})

    async def upstream(self, frame):
        if not self.socket or self.socket.closed:
            raise ProtocolError("Realtime connection unavailable", "UNAVAILABLE")
        await asyncio.wait_for(self.socket.send_json(frame, dumps=wire_json), 5)

    async def start(self):
        try:
            self.client = ClientSession(timeout=ClientTimeout(total=None, sock_connect=20))
            self.socket = await self.client.ws_connect(
                self.config["url"] + "?" + urlencode({"model": self.config["model"]}),
                headers={"Authorization": "Bearer " + self.config["key"]},
                max_msg_size=512 * 1024, heartbeat=20, compress=0)
            await self.upstream(session_settings(self.config, self.language, self.persona))
            self.player = asyncio.create_task(self.playback())
            self.reader = asyncio.current_task()
            async with asyncio.timeout(3600):
                while not self.closed:
                    if not self.authorized():
                        raise ProtocolError("Device credential revoked", "AUTH_FAILED")
                    try:
                        message = await self.socket.receive(timeout=30 if self.ready else 20)
                    except TimeoutError:
                        if not self.ready:
                            raise ProtocolError("Realtime ready timeout")
                        await self.emit("ready")
                        continue
                    if message.type == WSMsgType.TEXT:
                        await self.handle(json.loads(message.data))
                    elif message.type in {WSMsgType.ERROR, WSMsgType.CLOSED, WSMsgType.CLOSE}:
                        break
            if not self.closed:
                await self.close("disconnected")
        except asyncio.CancelledError:
            raise
        except Exception:
            if not self.closed:
                with contextlib.suppress(Exception):
                    await self.emit("error", code="UPSTREAM_UNAVAILABLE", message="Realtime service unavailable; check host credentials, model and network")
                await self.close("error")

    def clear_output(self):
        self.queue.clear()
        self.queue_bytes = 0
        self.index = 0
        self.due = 0
        self.wake.set()

    async def interrupt(self, cancel=True):
        self.suppressed_response = self.output_response
        self.clear_output()
        if cancel and self.response_active:
            await self.upstream({"type": "response.cancel"})
        self.response_active = False
        if self.output_item:
            await self.upstream({"type": "conversation.item.truncate", "item_id": self.output_item,
                                 "content_index": 0, "audio_end_ms": self.items.get(self.output_item, {}).get("played", 0) // 48})
            self.output_item = None
        self.marks.clear()
        self.items.clear()
        await self.emit("clear")

    def enqueue(self, event, size=0):
        if self.queue_bytes + size > 2 * 1024 * 1024 or len(self.queue) >= 4096:
            raise ProtocolError("Realtime playback queue exceeded its bound")
        self.queue.append((event, size))
        self.queue_bytes += size
        self.wake.set()

    async def playback(self):
        try:
            while not self.closed:
                if not self.queue:
                    self.wake.clear()
                    await self.wake.wait()
                    continue
                delay = self.due - time.monotonic() - 0.32
                if delay > 0:
                    self.wake.clear()
                    with contextlib.suppress(TimeoutError):
                        await asyncio.wait_for(self.wake.wait(), delay)
                    continue
                event, size = self.queue.popleft()
                self.queue_bytes -= size
                self.sequence += 1
                item = event.pop("_item", None)
                if size:
                    self.due = max(self.due, time.monotonic()) + size / 48000
                    self.output_item = item
                    state = self.items.setdefault(item, {"sent": 0, "played": 0, "marked": 0})
                    state["sent"] += size
                final = event["type"] == "audioDone"
                await self.emit(event.pop("type"), playbackSequence=self.sequence, **event)
                state = self.items.get(item)
                if state and state["sent"] > state["marked"] and (final or state["sent"] - state["marked"] >= 15360):
                    self.mark_number += 1
                    name = f"audio-{self.mark_number}"
                    if len(self.marks) >= 64:
                        raise ProtocolError("Playback acknowledgements stalled")
                    self.marks[name] = (item, state["sent"])
                    state["marked"] = state["sent"]
                    self.sequence += 1
                    await self.emit("mark", markName=name, playbackSequence=self.sequence)
        except asyncio.CancelledError:
            raise
        except Exception:
            await self.close("error")

    async def append(self, params):
        if not self.ready:
            raise ProtocolError("Realtime session is not ready", "BUSY")
        pcm = decode_audio(params.get("audioBase64"))
        await self.upstream({"type": "input_audio_buffer.append", "audio": base64.b64encode(pcm).decode()})

    async def handle(self, frame):
        event = frame.get("type")
        response = frame.get("response_id") or frame.get("response", {}).get("id")
        if response and response == self.suppressed_response:
            return
        if event == "session.updated":
            if not self.ready:
                self.ready = True
                await self.emit("ready")
        elif event == "input_audio_buffer.speech_started":
            self.speaking = True
        elif event == "input_audio_buffer.speech_stopped":
            self.speaking = False
        elif event == "conversation.item.input_audio_transcription.completed":
            await self.emit("transcript", role="user", text=bounded_text(frame.get("transcript", "")), final=True)
        elif event == "response.created":
            self.output_response = frame.get("response", {}).get("id")
            self.response_active = True
            self.resume_pending = False
            self.enqueue({"type": "responseStarted"})
        elif event == "response.output_audio.delta":
            raw = base64.b64decode(frame["delta"], validate=True)
            if len(raw) % 2:
                raise ProtocolError("Invalid upstream PCM")
            item = frame.get("item_id")
            if not isinstance(item, str) or len(item) > 128:
                raise ProtocolError("Invalid upstream audio item")
            for offset in range(0, len(raw), 3840):
                pcm = amplify_pcm(raw[offset:offset + 3840], self.config.get("output_gain_db", 0))
                encoded, self.index = encode_ima(pcm, self.index)
                self.enqueue({"type": "audio", "_item": item, "audioBase64": "passport-ima:" + base64.b64encode(encoded).decode()}, len(pcm))
        elif event in {"response.output_audio_transcript.delta", "response.output_audio_transcript.done"}:
            final = event.endswith(".done")
            self.enqueue({"type": "transcript" if final else "partial", "role": "assistant", "final": final,
                          "text": bounded_text(frame.get("transcript" if final else "delta", ""))})
        elif event == "response.output_audio.done":
            self.enqueue({"type": "audioDone", "_item": frame.get("item_id")})
        elif event == "response.function_call_arguments.done":
            call = frame.get("call_id")
            if not isinstance(call, str) or not 1 <= len(call) <= 128 or call in self.seen_calls:
                return
            if len(self.seen_calls) >= 256:
                raise ProtocolError("Realtime tool limit reached")
            self.seen_calls.add(call)
            if frame.get("name") == END_CALL:
                await self.close("completed")
            elif self.work and not self.work.done():
                await self.tool_result(call, {"error": "A Hermes task is already active"})
            else:
                self.work = asyncio.create_task(self.run_tool(call, frame))
        elif event == "response.done":
            self.response_active = False
            status = frame.get("response", {}).get("status")
            if status == "failed":
                raise ProtocolError("Realtime response failed")
            await self.resume()
        elif event == "error":
            raise ProtocolError("Realtime service rejected the request")

    async def run_tool(self, call, frame):
        try:
            if self.config.get("synthetic"):
                raise ProtocolError("Synthetic tests cannot execute tools")
            args = json.loads(frame.get("arguments", "{}"))
            question = args.get("question") if isinstance(args, dict) else None
            context = args.get("context", "") if isinstance(args, dict) else ""
            if (frame.get("name") != CONSULT or not isinstance(question, str) or not question.strip() or
                    not isinstance(context, str) or len(question.encode()) + len(context.encode()) > 3900):
                raise ProtocolError("Unsupported Hermes tool request")
            async with asyncio.timeout(120):
                result = await self.consult(question, context)
        except asyncio.CancelledError:
            raise
        except Exception:
            result = {"error": "Hermes could not complete the task; it may require approval on the host"}
        if not self.closed:
            try:
                await self.tool_result(call, result)
            except Exception:
                await self.close("error")

    async def tool_result(self, call, result):
        await self.upstream({"type": "conversation.item.create", "item": {
            "type": "function_call_output", "call_id": call, "output": wire_json(result)}})
        self.resume_pending = True
        await self.resume()

    async def resume(self):
        if self.resume_pending and not self.response_active and not self.speaking and not self.closed:
            self.resume_pending = False
            self.response_active = True
            await self.upstream({"type": "response.create"})

    async def acknowledge(self, name):
        if isinstance(name, str) and name not in self.marks and name.startswith("audio-") and name[6:].isdigit() and int(name[6:]) <= self.mark_number:
            return
        if not isinstance(name, str) or name not in self.marks:
            raise ProtocolError("Unknown playback mark")
        item, played = self.marks.pop(name)
        state = self.items.get(item)
        if state:
            state["played"] = max(state["played"], played)
        for stale in list(self.items):
            if stale != self.output_item and not any(mark[0] == stale for mark in self.marks.values()):
                self.items.pop(stale)

    async def close(self, reason="completed"):
        if self.closed:
            if self.cleanup and asyncio.current_task() not in (self.work, self.player, self.reader):
                await asyncio.shield(self.cleanup)
            return
        self.closed = True
        self.clear_output()
        caller = asyncio.current_task()
        self.cleanup = asyncio.create_task(self.teardown(reason, caller))
        await asyncio.shield(self.cleanup)

    async def teardown(self, reason, caller):
        with contextlib.suppress(Exception):
            await self.send({"type": "event", "event": "talk.event", "payload": {
                "relaySessionId": self.id, "type": "close", "reason": reason}})
        tasks = [task for task in (self.work, self.player, self.reader)
                 if task and task is not caller and not task.done()]
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        if self.socket:
            await self.socket.close()
        if self.client:
            await self.client.close()
