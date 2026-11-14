#include "passport_endpoint.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    passport_endpoint_t endpoint;
    assert(passport_endpoint_parse("wss://agent.example", true, &endpoint));
    assert(endpoint.tls && endpoint.port == 443);
    assert(!strcmp(endpoint.host, "agent.example"));
    assert(!strcmp(endpoint.path, "/passport/v1"));
    assert(passport_endpoint_parse("ws://agent.example:18789/", false, &endpoint));
    assert(!endpoint.tls && endpoint.port == 18789 && !strcmp(endpoint.path, "/"));
    assert(passport_endpoint_parse("wss://agent.example/prefix/", true, &endpoint));
    assert(!strcmp(endpoint.path, "/prefix/passport/v1"));
    assert(passport_endpoint_parse("wss://agent.example/passport/v1///", true, &endpoint));
    assert(!strcmp(endpoint.path, "/passport/v1"));
    assert(passport_endpoint_parse("wss://agent.example/prefix/passport/v1", true, &endpoint));
    assert(!strcmp(endpoint.path, "/prefix/passport/v1"));
    const char *invalid[] = {"", "https://agent.example", "wss://", "wss://user@agent.example",
        "ws://agent.example:0", "ws://agent.example:65536", "ws://agent.example:80oops",
        "ws://agent.example:+443", "ws://agent.example:",
        "wss://agent.example/path?token=secret", "wss://agent.example/path#fragment",
        "wss://agent.example/with space", "wss://agent.example/\r\n"};
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++)
        assert(!passport_endpoint_parse(invalid[index], true, &endpoint));
    char url[300];
    memset(url, 'x', sizeof(url));
    memcpy(url, "wss://agent.example/", 20);
    url[sizeof(url) - 1] = 0;
    assert(!passport_endpoint_parse(url, false, &endpoint));
    const char prefix[] = "wss://agent.example/";
    size_t path_start = sizeof(prefix) - 2;
    memcpy(url, prefix, sizeof(prefix) - 1);
    url[path_start + sizeof(endpoint.path) - 1] = 0;
    assert(passport_endpoint_parse(url, false, &endpoint));
    assert(!passport_endpoint_parse(url, true, &endpoint));
    url[path_start + sizeof(endpoint.path) - sizeof("/passport/v1")] = 0;
    assert(passport_endpoint_parse(url, true, &endpoint));
    assert(strlen(endpoint.path) == sizeof(endpoint.path) - 1);
    assert(!passport_endpoint_parse(NULL, true, &endpoint));
    assert(!passport_endpoint_parse("wss://agent.example", true, NULL));
    puts("Passport endpoint: native paths, ports and rejected credentials/truncation PASS");
    return 0;
}
