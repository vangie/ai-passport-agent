import base64
import struct

from .passport_auth import ProtocolError


STEPS = (7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37,
         41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
         209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
         876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
         2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
         8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
         22385, 24623, 27086, 29794, 32767)
INDICES = (-1, -1, -1, -1, 2, 4, 6, 8)


def update(code, predictor, index):
    step = STEPS[index]
    change = (step >> 3) + (step >> 2 if code & 1 else 0) + (step >> 1 if code & 2 else 0) + (step if code & 4 else 0)
    predictor = max(-32768, min(32767, predictor + (-change if code & 8 else change)))
    return predictor, max(0, min(88, index + INDICES[code & 7]))


def encode_ima(pcm, index=0):
    if not pcm or len(pcm) > 3840 or len(pcm) % 2 or not 0 <= index <= 88:
        raise ProtocolError("Invalid Passport PCM")
    result = bytearray()
    for offset in range(0, len(pcm), 960):
        count = min(480, (len(pcm) - offset) // 2)
        predictor = struct.unpack_from("<h", pcm, offset)[0]
        block = bytearray(6 + count // 2)
        struct.pack_into("<hBBH", block, 0, predictor, index, 0, count)
        for sample in range(1, count):
            delta = struct.unpack_from("<h", pcm, offset + sample * 2)[0] - predictor
            code = 8 if delta < 0 else 0
            delta = abs(delta)
            step = STEPS[index]
            for flag, threshold in ((4, step), (2, step >> 1), (1, step >> 2)):
                if delta >= threshold:
                    code |= flag
                    delta -= threshold
            predictor, index = update(code, predictor, index)
            block[6 + (sample - 1) // 2] |= code if sample & 1 else code << 4
        result.extend(block)
    return bytes(result), index


def decode_ima(encoded):
    if not encoded or len(encoded) > 992:
        raise ProtocolError("Invalid Passport IMA frame")
    result = bytearray()
    offset = 0
    while offset < len(encoded):
        if len(encoded) - offset < 6:
            raise ProtocolError("Truncated Passport IMA header")
        predictor, index, reserved, count = struct.unpack_from("<hBBH", encoded, offset)
        size = 6 + count // 2
        if (not 1 <= count <= 480 or index > 88 or reserved or size > len(encoded) - offset or
                len(result) + count * 2 > 3840 or not count & 1 and encoded[offset + size - 1] & 0xf0):
            raise ProtocolError("Invalid Passport IMA block")
        result.extend(struct.pack("<h", predictor))
        for sample in range(1, count):
            packed = encoded[offset + 6 + (sample - 1) // 2]
            predictor, index = update(packed & 15 if sample & 1 else packed >> 4, predictor, index)
            result.extend(struct.pack("<h", predictor))
        offset += size
    return bytes(result)


def decode_audio(value):
    if not isinstance(value, str):
        raise ProtocolError("Invalid Passport audio")
    if value.startswith("passport-silence:"):
        count = value.removeprefix("passport-silence:")
        if not count.isascii() or not count.isdigit() or len(count) > 4 or not 2 <= int(count) <= 3840 or int(count) % 2:
            raise ProtocolError("Invalid Passport silence")
        return bytes(int(count))
    if not value.startswith("passport-ima:") or not 4 <= len(value[13:]) <= 1312:
        raise ProtocolError("Passport IMA audio required")
    try:
        raw = base64.b64decode(value[13:], validate=True)
    except ValueError as error:
        raise ProtocolError("Invalid Passport audio encoding") from error
    return decode_ima(raw)
