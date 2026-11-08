#include "monocypher-ed25519.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void unhex(uint8_t *out, const char *in, size_t count) {
    for (size_t i=0; i<count; i++) { unsigned v; assert(sscanf(in+2*i, "%2x", &v)==1); out[i]=v; }
}
int main(void) {
    /* RFC 8032 section 7.1, test vector 1. */
    uint8_t seed[32], key[64], public[32], expected_public[32], signature[64], expected_signature[64];
    unhex(seed, "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", 32);
    unhex(expected_public, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", 32);
    unhex(expected_signature, "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                              "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", 64);
    crypto_ed25519_key_pair(key, public, seed);
    assert(!memcmp(public, expected_public, 32));
    crypto_ed25519_sign(signature, key, (const uint8_t *)"", 0);
    assert(!memcmp(signature, expected_signature, 64));
    assert(crypto_ed25519_check(signature, public, (const uint8_t *)"", 0)==0);
    signature[0]^=1;
    assert(crypto_ed25519_check(signature, public, (const uint8_t *)"", 0)!=0);
    puts("Device identity: RFC 8032 Ed25519 signature and tampering vector PASS");
}
