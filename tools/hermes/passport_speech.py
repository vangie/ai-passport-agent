from contextvars import ContextVar


speech_language = ContextVar("passport_speech_language", default="")


def transcription_hints(*, source=None, **kwargs):
    language = speech_language.get()
    return {"language": language} if source == "ai_passport" and language else None
