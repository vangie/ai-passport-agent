#include "passport.h"

#include <stdio.h>

int passport_openclaw_chat_send(char *out, size_t out_len, const passport_turn_t *turn) {
    const char *session = "agent:main:passport";
    const char *format = "audio/ogg";
    const char *message = "";
    if (turn != NULL) {
        if (turn->session_key[0] != '\0') {
            session = turn->session_key;
        }
        if (turn->audio_format[0] != '\0') {
            format = turn->audio_format;
        }
        if (!turn->has_audio) {
            message = turn->transcript;
        }
    }
    if (turn != NULL && turn->has_audio) {
        return snprintf(out, out_len,
                        "{\"type\":\"req\",\"method\":\"chat.send\","
                        "\"params\":{\"sessionKey\":\"%s\","
                        "\"message\":\"\","
                        "\"attachments\":[{\"type\":\"%s\","
                        "\"source\":\"device\"}]}}",
                        session, format);
    }
    return snprintf(out, out_len,
                    "{\"type\":\"req\",\"method\":\"chat.send\","
                    "\"params\":{\"sessionKey\":\"%s\",\"message\":\"%s\"}}",
                    session, message);
}
