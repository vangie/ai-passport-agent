import base64
import hashlib
import json
import os
from pathlib import Path
import secrets
import sqlite3
import time
from urllib.parse import urlsplit


class ProtocolError(ValueError):
    def __init__(self, message, code="INVALID_REQUEST"):
        super().__init__(message)
        self.code = code


def encode(value):
    return base64.urlsafe_b64encode(value).decode().rstrip("=")


def decode(value, size):
    if not isinstance(value, str) or len(value) > size * 2:
        raise ProtocolError("Invalid device proof", "AUTH_FAILED")
    try:
        result = base64.b64decode(value + "=" * (-len(value) % 4), altchars=b"-_", validate=True)
    except (ValueError, TypeError) as error:
        raise ProtocolError("Invalid device proof", "AUTH_FAILED") from error
    if len(result) != size:
        raise ProtocolError("Invalid device proof", "AUTH_FAILED")
    return result


class DeviceStore:
    def __init__(self, filename, now=time.time):
        self.now = now
        destination = Path(filename).expanduser()
        destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        descriptor = os.open(destination, os.O_CREAT | os.O_RDWR, 0o600)
        os.close(descriptor)
        os.chmod(destination, 0o600)
        self.db = sqlite3.connect(destination)
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS pairing (digest TEXT PRIMARY KEY, expires REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS devices (
                id TEXT PRIMARY KEY, public_key TEXT NOT NULL, token_hash TEXT NOT NULL);
        """)

    def close(self):
        self.db.close()

    @staticmethod
    def digest(token):
        return hashlib.sha256(token.encode()).hexdigest()

    def issue(self, url):
        parsed = urlsplit(url)
        if (parsed.scheme not in {"ws", "wss"} or not parsed.hostname or parsed.username or
                parsed.password or parsed.query or parsed.fragment or len(url.encode()) >= 160 or
                any(character.isspace() for character in url)):
            raise ProtocolError("Use a WebSocket endpoint without embedded credentials")
        if parsed.scheme == "ws" and parsed.hostname not in {"localhost", "127.0.0.1", "::1"}:
            raise ProtocolError("Public and LAN endpoints must use wss://")
        token = secrets.token_urlsafe(32)
        with self.db:
            self.db.execute("DELETE FROM pairing WHERE expires <= ?", (self.now(),))
            if self.db.execute("SELECT count(*) FROM pairing").fetchone()[0] >= 16:
                raise ProtocolError("Too many pending pairing codes")
            self.db.execute("INSERT INTO pairing VALUES (?, ?)", (self.digest(token), self.now() + 600))
        code = encode(json.dumps({"url": url, "bootstrapToken": token}, separators=(",", ":")).encode())
        return {"gatewayUrl": url, "setupCode": code}

    def authenticate(self, params, nonce, timestamp):
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

        client = params.get("client", {})
        device = params.get("device", {})
        scopes = params.get("scopes", [])
        if (client.get("id") != "gateway-client" or client.get("mode") != "ui" or
                client.get("platform") != "esp32c3" or client.get("deviceFamily") != "ai-passport" or
                params.get("role") != "operator" or scopes not in [
                    ["operator.read", "operator.write"],
                    ["operator.read", "operator.write", "operator.talk"]] or
                params.get("minProtocol", 99) > 3 or params.get("maxProtocol", 0) < 3 or
                device.get("nonce") != nonce or device.get("signedAt") != timestamp):
            raise ProtocolError("Invalid signed device handshake", "AUTH_FAILED")
        public_key = decode(device.get("publicKey"), 32)
        identity = hashlib.sha256(public_key).hexdigest()
        if device.get("id") != identity:
            raise ProtocolError("Device identity mismatch", "AUTH_FAILED")
        auth = params.get("auth", {})
        bootstrap = auth.get("bootstrapToken")
        token = bootstrap or auth.get("deviceToken")
        if not isinstance(token, str) or not 32 <= len(token) <= 127 or "|" in token:
            raise ProtocolError("Missing device credential", "AUTH_FAILED")
        proof = "|".join(["v3", identity, client["id"], client["mode"], "operator", ",".join(scopes),
                          str(timestamp), token, nonce, client["platform"], client["deviceFamily"]])
        try:
            Ed25519PublicKey.from_public_bytes(public_key).verify(decode(device.get("signature"), 64), proof.encode())
        except InvalidSignature as error:
            raise ProtocolError("Device signature rejected", "AUTH_FAILED") from error
        with self.db:
            if bootstrap:
                known = self.db.execute("SELECT 1 FROM devices WHERE id = ?", (identity,)).fetchone()
                if not known and self.db.execute("SELECT count(*) FROM devices").fetchone()[0] >= 32:
                    raise ProtocolError("Device limit reached", "AUTH_FAILED")
                consumed = self.db.execute("DELETE FROM pairing WHERE digest = ? AND expires > ?",
                                           (self.digest(token), self.now()))
                if consumed.rowcount != 1:
                    raise ProtocolError("Pairing code expired or already used", "AUTH_FAILED")
                token = secrets.token_urlsafe(32)
                self.db.execute("INSERT OR REPLACE INTO devices VALUES (?, ?, ?)",
                                (identity, encode(public_key), self.digest(token)))
            else:
                stored = self.db.execute("SELECT public_key, token_hash FROM devices WHERE id = ?", (identity,)).fetchone()
                if not stored or stored[0] != encode(public_key) or not secrets.compare_digest(stored[1], self.digest(token)):
                    raise ProtocolError("Device credential revoked or invalid", "AUTH_FAILED")
        return identity, token

    def revoke(self, identity):
        with self.db:
            self.db.execute("DELETE FROM devices WHERE id = ?", (identity,))

    def authorized(self, identity, token):
        stored = self.db.execute("SELECT token_hash FROM devices WHERE id = ?", (identity,)).fetchone()
        return bool(stored and secrets.compare_digest(stored[0], self.digest(token)))


def main():
    import argparse

    parser = argparse.ArgumentParser(description="AI Passport Hermes pairing; output contains a one-use credential")
    parser.add_argument("--home", type=Path, required=True, help="Active Hermes profile directory")
    subparsers = parser.add_subparsers(dest="command", required=True)
    pair = subparsers.add_parser("pair")
    pair.add_argument("--url", required=True)
    revoke = subparsers.add_parser("revoke")
    revoke.add_argument("device_id")
    arguments = parser.parse_args()
    store = DeviceStore(arguments.home / "passport" / "devices.sqlite3")
    try:
        if arguments.command == "pair":
            print(json.dumps(store.issue(arguments.url)))
        else:
            store.revoke(arguments.device_id)
    finally:
        store.close()


if __name__ == "__main__":
    main()
