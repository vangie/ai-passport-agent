import base64
import binascii
import hashlib
import json
import re
import secrets
import struct
import time

from .passport_auth import ProtocolError


MAX_AUDIO_BYTES = 480058
MAX_FRAME_BYTES = 660000
MAX_REPLY_BYTES = 4096
MAX_HISTORY_BYTES = 5500


def wire_json(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def bounded_text(value, maximum=MAX_REPLY_BYTES):
    return str(value).encode()[:maximum].decode("utf-8", errors="ignore")


def attachment(params):
    message = params.get("message", "")
    files = params.get("attachments", [])
    if not isinstance(message, str) or len(message.encode()) > 4096:
        raise ProtocolError("Question is too long")
    if not files:
        if not message.strip():
            raise ProtocolError("Question is empty")
        return message, None
    if message or not isinstance(files, list) or len(files) != 1 or not isinstance(files[0], dict):
        raise ProtocolError("Send one voice recording or one text question")
    audio = files[0]
    encoded = audio.get("content")
    if (audio.get("type") != "audio" or audio.get("mimeType") != "audio/wav" or
            not isinstance(encoded, str) or len(encoded) > (MAX_AUDIO_BYTES + 2) // 3 * 4):
        raise ProtocolError("Unsupported voice attachment")
    try:
        raw = bytearray(base64.b64decode(encoded, validate=True))
    except (ValueError, binascii.Error) as error:
        raise ProtocolError("Invalid voice encoding") from error
    if len(raw) < 58 or len(raw) > MAX_AUDIO_BYTES or raw[:4] != b"RIFF" or raw[8:16] != b"WAVEfmt ":
        raise ProtocolError("Invalid Passport WAV")
    if (struct.unpack_from("<IHHIIHH", raw, 16) != (18, 7, 1, 16000, 16000, 1, 8) or
            raw[38:42] != b"fact" or struct.unpack_from("<I", raw, 42)[0] != 4 or raw[50:54] != b"data"):
        raise ProtocolError("Expected Passport 16 kHz mono mu-law audio")
    samples = len(raw) - 58
    if not samples or struct.unpack_from("<I", raw, 54)[0] < samples:
        raise ProtocolError("Invalid voice length")
    struct.pack_into("<I", raw, 4, len(raw) - 8)
    struct.pack_into("<I", raw, 46, samples)
    struct.pack_into("<I", raw, 54, samples)
    return "", bytes(raw)


class PassportProtocol:
    def __init__(self, store, now=time.time):
        self.store = store
        self.now = now
        self.subscribers = {}
        self.store.db.execute("""CREATE TABLE IF NOT EXISTS runs (
            device TEXT NOT NULL, request TEXT NOT NULL, digest TEXT NOT NULL,
            run TEXT NOT NULL, session TEXT NOT NULL, state TEXT NOT NULL,
            text TEXT NOT NULL, transcript TEXT NOT NULL, created REAL NOT NULL,
            PRIMARY KEY(device, request))""")
        with self.store.db:
            self.store.db.execute("UPDATE runs SET state = 'error' WHERE state = 'running'")

    def challenge(self):
        return {"nonce": secrets.token_urlsafe(24), "ts": int(self.now() * 1000)}

    @staticmethod
    def session(identity, key):
        if key != f"agent:passport:passport-{identity}":
            raise ProtocolError("Device does not own this conversation", "FORBIDDEN")
        return key

    def get_run(self, identity, request):
        cursor = self.store.db.execute("SELECT * FROM runs WHERE device = ? AND request = ?", (identity, request))
        row = cursor.fetchone()
        return dict(zip([column[0] for column in cursor.description], row)) if row else None

    def prepare(self, identity, params):
        session = self.session(identity, params.get("sessionKey"))
        request = params.get("idempotencyKey")
        if not isinstance(request, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,79}", request):
            raise ProtocolError("Invalid request identity")
        question, audio = attachment(params)
        digest = hashlib.sha256(question.encode() if audio is None else audio).hexdigest()
        existing = self.get_run(identity, request)
        if existing:
            if existing["digest"] != digest:
                raise ProtocolError("Request identity was already used for different audio", "CONFLICT")
            return existing, None, False
        with self.store.db:
            self.store.db.execute("DELETE FROM runs WHERE state != 'running' AND created < ?", (self.now() - 86400,))
            if self.store.db.execute("SELECT 1 FROM runs WHERE device = ? AND state = 'running'", (identity,)).fetchone():
                raise ProtocolError("Wait for the current question to finish", "BUSY")
            if self.store.db.execute("SELECT count(*) FROM runs WHERE state = 'running'").fetchone()[0] >= 4:
                raise ProtocolError("All question slots are busy", "BUSY")
            if self.store.db.execute("SELECT count(*) FROM runs").fetchone()[0] >= 128:
                raise ProtocolError("Question history is full; try again later", "BUSY")
            self.store.db.execute("INSERT INTO runs VALUES (?, ?, ?, ?, ?, 'running', '', '', ?)",
                                  (identity, request, digest, secrets.token_hex(16), session, self.now()))
        return self.get_run(identity, request), (question, audio), True

    def history(self, identity, params):
        self.session(identity, params.get("sessionKey"))
        rows = self.store.db.execute("SELECT request FROM runs WHERE device = ? ORDER BY created DESC LIMIT 8", (identity,)).fetchall()
        runs = [self.get_run(identity, row[0]) for row in rows]
        result = {"messages": [], "sessionInfo": {"activeRunIds": [run["run"] for run in runs if run["state"] == "running"]}}
        for run in runs:
            if run["state"] == "running":
                continue
            message = self.message(run)
            candidate = {**result, "messages": [message, *result["messages"]]}
            if not result["messages"]:
                while len(wire_json(candidate).encode()) > MAX_HISTORY_BYTES:
                    text = message["content"][0]["text"]
                    message["content"][0]["text"] = bounded_text(text, len(text.encode()) // 2)
            if len(wire_json(candidate).encode()) > MAX_HISTORY_BYTES:
                break
            result = candidate
        return result

    @staticmethod
    def message(run):
        return {"role": "assistant", "content": [{"type": "text", "text": run["text"]}],
                "stopReason": "stop" if run["state"] == "final" else "error",
                "__openclaw": {"runId": run["run"]}}

    def transcript(self, identity, params):
        self.session(identity, params.get("sessionKey"))
        request = params.get("requestId")
        if not isinstance(request, str):
            raise ProtocolError("Invalid request identity")
        run = self.get_run(identity, request)
        if not run or run["run"] != params.get("runId"):
            raise ProtocolError("Unknown question", "NOT_FOUND")
        return {"runId": run["run"], "requestId": run["request"], "text": run["transcript"]}

    async def update(self, identity, request, text=None, transcript=None, state=None):
        run = self.get_run(identity, request)
        if not run or run["state"] != "running":
            return
        with self.store.db:
            self.store.db.execute("UPDATE runs SET text = ?, transcript = ?, state = ? WHERE device = ? AND request = ?",
                (bounded_text(run["text"] if text is None else text),
                 bounded_text(run["transcript"] if transcript is None else transcript, 479),
                 state or run["state"], identity, request))
        run = self.get_run(identity, request)
        payload = {"runId": run["run"], "sessionKey": run["session"],
                   "state": run["state"] if state else "delta", "message": self.message(run)}
        for send in tuple(self.subscribers.get(identity, ())):
            await send({"type": "event", "event": "chat", "payload": payload})

    def dispatch(self, identity, method, params):
        if method == "sessions.messages.subscribe":
            self.session(identity, params.get("key"))
            return {}
        if method == "chat.history":
            return self.history(identity, params)
        if method == "passport.transcript":
            return self.transcript(identity, params)
        if method == "passport.status":
            return {"protocol": 1, "backend": "hermes", "ask": True, "upstreamAvailable": False,
                    "upstreamConfigured": False, "providerRegistered": False}
        if method.startswith("talk."):
            raise ProtocolError("Call methods require the authenticated audio adapter", "NOT_SUPPORTED")
        raise ProtocolError("Unknown method", "METHOD_NOT_FOUND")
