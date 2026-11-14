#include "passport_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void project(const char *wire, const char *expected) {
    for (size_t chunk=1;chunk<=strlen(wire)+1;chunk++) {
        passport_json_filter_t f; char out[PASSPORT_JSON_OUTPUT_MAX];
        passport_json_filter_init(&f);
        size_t n=strlen(wire);
        for(size_t p=0;p<n;p+=chunk) assert(passport_json_filter_feed(&f,wire+p,n-p<chunk?n-p:chunk));
        assert(passport_json_filter_finish(&f,out,sizeof(out))>0);
        assert(!strcmp(out,expected));
    }
}
int main(void) {
    project("{\"type\":\"res\",\"ok\":true,\"payload\":{\"backend\":\"hermes\",\"passportProtocol\":1,\"passportAsk\":true}}",
            "{\"type\":\"res\",\"ok\":true,\"payload\":{\"backend\":\"hermes\",\"passportProtocol\":1,\"passportAsk\":true}}");
    project("{\"type\":\"res\",\"id\":\"passport-check-status\",\"ok\":true,\"payload\":{\"protocol\":1,\"upstreamAvailable\":true,\"upstreamConfigured\":false,\"providerRegistered\":true}}",
            "{\"type\":\"res\",\"id\":\"passport-check-status\",\"ok\":true,\"payload\":{\"protocol\":1,\"upstreamAvailable\":true,\"upstreamConfigured\":false,\"providerRegistered\":true}}");
    project("{\"type\":\"res\",\"id\":\"passport-transcript\",\"ok\":true,\"payload\":{\"runId\":\"run-1\",\"requestId\":\"clip-1\",\"text\":\"水浒传的林冲\"}}",
            "{\"type\":\"res\",\"id\":\"passport-transcript\",\"ok\":true,\"payload\":{\"runId\":\"run-1\",\"requestId\":\"clip-1\",\"text\":\"水浒传的林冲\"}}");
    project("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"401 Unauthorized\",\"code\":\"realtime_unavailable\",\"provider\":\"fixture\",\"model\":\"fixture-realtime\",\"phase\":\"connect\"}}",
            "{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"error\",\"message\":\"401 Unauthorized\",\"code\":\"realtime_unavailable\",\"provider\":\"fixture\",\"model\":\"fixture-realtime\",\"phase\":\"connect\"}}");
    project("{\"event\":\"talk.event\",\"payload\":{\"type\":\"toolCall\",\"callId\":\"call-1\",\"name\":\"openclaw_agent_consult\",\"args\":{\"question\":\"几点\",\"context\":\"助手\",\"responseStyle\":\"简短\"}}}",
            "{\"event\":\"talk.event\",\"payload\":{\"type\":\"toolCall\",\"callId\":\"call-1\",\"name\":\"openclaw_agent_consult\",\"args\":{\"question\":\"几点\",\"context\":\"助手\",\"responseStyle\":\"简短\"}}}");
    project("{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\",\"talkEvent\":{\"seq\":7,\"turnId\":\"turn-1\"}}}",
            "{\"event\":\"talk.event\",\"payload\":{\"relaySessionId\":\"live-1\",\"type\":\"audio\",\"audioBase64\":\"AQIDBA==\",\"talkEvent\":{\"seq\":7,\"turnId\":\"turn-1\"}}}");
    project("{\"payload\":{\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"他说：\\\"你好\\\"，‘朋友’和“助手”—稍等…\"}]}}}",
            "{\"payload\":{\"message\":{\"content\":[{\"type\":\"text\",\"text\":\"他说：\\\"你好\\\"，‘朋友’和“助手”—稍等…\"}]}}}");
    project("{\"payload\":{\"deltaText\":\"\\u201c你好\\u201d \\u2018朋友\\u2019\"}}",
            "{\"payload\":{\"deltaText\":\"\\u201c你好\\u201d \\u2018朋友\\u2019\"}}");
    project("{\"type\":\"event\",\"event\":\"chat\",\"payload\":{\"sessionKey\":\"fixture\",\"runId\":\"run-1\",\"seq\":2,\"state\":\"delta\",\"deltaText\":\"助手\",\"replace\":true}}",
            "{\"type\":\"event\",\"event\":\"chat\",\"payload\":{\"sessionKey\":\"fixture\",\"runId\":\"run-1\",\"seq\":2,\"state\":\"delta\",\"deltaText\":\"助手\",\"replace\":true}}");
    assert(sizeof(passport_json_filter_t)<7000);
    project("{\"event\":\"connect.challenge\",\"payload\":{\"nonce\":\"abc\",\"ts\":12345}}",
            "{\"event\":\"connect.challenge\",\"payload\":{\"nonce\":\"abc\",\"ts\":12345}}");
    project("{\"type\":\"res\",\"ok\":true,\"payload\":{\"snapshot\":{\"text\":\"discard\"},\"auth\":{\"role\":\"operator\",\"deviceToken\":\"fixture-token\",\"deviceTokens\":[{\"role\":\"operator\",\"scopes\":[\"operator.read\",\"operator.write\"]}]},\"policy\":{\"maxPayload\":26214400}}}",
            "{\"type\":\"res\",\"ok\":true,\"payload\":{\"auth\":{\"role\":\"operator\",\"deviceToken\":\"fixture-token\",\"deviceTokens\":[{\"role\":\"operator\",\"scopes\":[\"operator.read\",\"operator.write\"]}]},\"policy\":{\"maxPayload\":26214400}}}");
    project("{\"ok\":false,\"error\":{\"details\":{\"code\":\"PAIRING_REQUIRED\"},\"message\":\"quote \\\" brace }\"}}",
            "{\"ok\":false,\"error\":{\"details\":{\"code\":\"PAIRING_REQUIRED\"},\"message\":\"quote \\\" brace }\"}}");
    project("{\"event\":\"chat\",\"payload\":{\"state\":\"final\",\"sessionKey\":\"agent:main:passport-test\",\"runId\":\"current\",\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"ignored\"},{\"type\":\"text\",\"text\":\"中文 🚀 \\u4e2d \\ud83d\\ude80\"}]}}}",
            "{\"event\":\"chat\",\"payload\":{\"state\":\"final\",\"sessionKey\":\"agent:main:passport-test\",\"runId\":\"current\",\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"thinking\"},{\"type\":\"text\",\"text\":\"中文 🚀 \\u4e2d \\ud83d\\ude80\"}]}}}");
    project("{\"payload\":{\"messages\":[{\"role\":\"assistant\",\"content\":\"reply\",\"__openclaw\":{\"runId\":\"current\",\"seq\":1}}],\"inFlightRun\":null}}",
            "{\"payload\":{\"messages\":[{\"role\":\"assistant\",\"content\":\"reply\",\"__openclaw\":{\"runId\":\"current\",\"seq\":1}}],\"inFlightRun\":null}}");
    project("{\"payload\":{\"sessionInfo\":{\"hasActiveRun\":true,\"activeRunIds\":[\"current\"],\"lastRunId\":\"current\",\"snapshotAt\":55},\"messages\":[{\"role\":\"assistant\",\"stopReason\":\"toolUse\",\"__openclaw\":{\"runId\":\"current\"}}]}}",
            "{\"payload\":{\"sessionInfo\":{\"hasActiveRun\":true,\"activeRunIds\":[\"current\"],\"lastRunId\":\"current\"},\"messages\":[{\"role\":\"assistant\",\"stopReason\":\"toolUse\",\"__openclaw\":{\"runId\":\"current\"}}]}}");
    passport_json_filter_t f; char out[PASSPORT_JSON_OUTPUT_MAX];
    passport_json_filter_init(&f);
    const char *s="{\"payload\":{\"snapshot\":{\"text\":\"";
    assert(passport_json_filter_feed(&f,s,strlen(s)));
    for(int i=0;i<100000;i++) assert(passport_json_filter_feed(&f,"noise",5));
    s="\"},\"message\":{\"content\":[{\"type\":\"thinking\",\"thinking\":\"";
    assert(passport_json_filter_feed(&f,s,strlen(s)));
    for(int i=0;i<100000;i++) assert(passport_json_filter_feed(&f,"noise",5));
    s="\"},{\"type\":\"text\",\"text\":\"kept\"}]}}}";
    assert(passport_json_filter_feed(&f,s,strlen(s)));
    assert(passport_json_filter_finish(&f,out,sizeof(out))>0);
    assert(!strcmp(out,"{\"payload\":{\"message\":{\"content\":[{\"type\":\"thinking\"},{\"type\":\"text\",\"text\":\"kept\"}]}}}"));
    const char *bad[]={"{\"ok\":","{\"ok\":tru}","{\"x\":01}","{\"x\":1.}","{\"x\":1e}","{\"x\":NaN}","{\"x\":[] ,}","{\"x\":[1,]}","{\"x\":{]}","{\"x\":\"\\q\"}","{\"x\":\"\\uD800\"}","{\"x\":\"\\uDC00\"}","{\"x\":\"\xc0\x80\"}","{\"x\":true false}","{}{}"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        passport_json_filter_init(&f);
        passport_json_filter_feed(&f,bad[i],strlen(bad[i]));
        assert(passport_json_filter_finish(&f,out,sizeof(out))<0);
    }
    /* A retained secret cannot be silently shortened. */
    passport_json_filter_init(&f); s="{\"payload\":{\"auth\":{\"deviceToken\":\"";
    assert(passport_json_filter_feed(&f,s,strlen(s)));
    for(int i=0;i<7000 && !f.failed;i++) passport_json_filter_feed(&f,"x",1);
    assert(f.failed);
    puts("Gateway projection: bounded 1 MB input, all split positions, auth, history, UTF-8, surrogate pairs and malformed input PASS");
}
