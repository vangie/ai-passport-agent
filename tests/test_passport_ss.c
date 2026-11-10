#include "passport_ss.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define expect(cond) do { if (!(cond)) { fprintf(stderr, "fail %s:%d %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static void roundtrip(void) {
    uint8_t salt[32], hello[256], back[64];
    const uint8_t address[] = {3,10,'g','w','.','e','x','a','m','p','l','e',0x01,0xbb};
    for (int i = 0; i < 32; i++) salt[i] = (uint8_t)i;
    size_t hello_len = 0, used = 0, got = 0;
    expect(passport_ss_hello(salt, "aes-256-cfb", "secret", "gw.example", 443, hello, sizeof(hello), &hello_len) == 0);
    expect(hello_len == 16 + sizeof(address) && !memcmp(hello, salt, 16));
    /* Echo the client's own stream back: its first 16 bytes act as the IV. */
    expect(passport_ss_consume(hello, hello_len, &used, back, sizeof(back), &got) == 0 && used == 16 && got == 0);
    int rc = passport_ss_consume(hello + 16, hello_len - 16, &used, back, sizeof(back), &got);
    expect(rc == 1 && used == hello_len - 16 && got == sizeof(address) && !memcmp(back, address, got));
    passport_ss_close();
}

static void short_reads_stay_aligned(void) {
    uint8_t salt[32] = {0};
    uint8_t hello[64], first[1], rest[8];
    size_t hello_len = 0, used = 0, got = 0;
    expect(passport_ss_hello(salt, "aes-256-cfb", "secret", "a", 1, hello, sizeof(hello), &hello_len) == 0);
    /* The IV may arrive split across reads. */
    expect(passport_ss_consume(hello, 5, &used, first, 1, &got) == 0 && used == 5 && got == 0);
    expect(passport_ss_consume(hello + 5, 11, &used, first, 1, &got) == 0 && used == 11);
    expect(passport_ss_consume(hello + 16, hello_len - 16, &used, first, 1, &got) == 1);
    expect(used == 1 && got == 1 && first[0] == 3);
    expect(passport_ss_consume(hello + 17, hello_len - 17, &used, rest, sizeof(rest), &got) == 1);
    expect(got == hello_len - 17);
    passport_ss_close();
}

/* A real server answers with its own IV, different from the client's. */
static void server_iv_is_used(void) {
    uint8_t client_salt[32] = {1}, server_iv[32] = {2}, hello[64], reply[64], all[16];
    size_t hello_len = 0, reply_len = 0, sealed = 0, have = 0;
    const uint8_t plain[] = {'o', 'k'};
    expect(passport_ss_hello(server_iv, "aes-256-cfb", "pw", "s", 1, reply, sizeof(reply), &reply_len) == 0);
    expect(passport_ss_seal(plain, 2, reply + reply_len, sizeof(reply) - reply_len, &sealed) == 0);
    reply_len += sealed;
    expect(passport_ss_hello(client_salt, "aes-256-cfb", "pw", "s", 1, hello, sizeof(hello), &hello_len) == 0);
    for (size_t at = 0; at < reply_len;) {
        size_t used = 0, got = 0;
        int rc = passport_ss_consume(reply + at, reply_len - at, &used, all + have, sizeof(all) - have, &got);
        expect(rc >= 0 && used > 0);
        if (rc < 0 || !used) break;
        at += used; have += got;
    }
    expect(have == 7 && all[0] == 3 && all[2] == 's' && !memcmp(all + 5, plain, 2));
    passport_ss_close();
}

int main(void) {
    expect(passport_ss_method("aes-256-cfb"));
    expect(!passport_ss_method("aes-256-gcm") && !passport_ss_method("chacha20-ietf-poly1305"));
    expect(!passport_ss_method("rc4") && !passport_ss_method(NULL));
    roundtrip();
    short_reads_stay_aligned();
    server_iv_is_used();
    if (fails) { fprintf(stderr, "%d failures\n", fails); return 1; }
    puts("Shadowsocks codec: aes-256-cfb, split IV, short reads and server IV PASS");
    return 0;
}
