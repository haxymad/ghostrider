#include "jky_builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { uint32_t state[4]; uint64_t count; uint8_t buffer[64]; } md5_ctx;

static const uint32_t md5_k[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391,
};

static const uint8_t md5_s[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5, 9,14,20,5, 9,14,20,5, 9,14,20,5, 9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21,
};

#define ROTL32(x,n) (((x) << (n)) | ((x) >> (32 - (n))))

static void md5_transform(uint32_t st[4], const uint8_t blk[64]) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
        m[i] = (uint32_t)blk[i*4] | ((uint32_t)blk[i*4+1] << 8) |
               ((uint32_t)blk[i*4+2] << 16) | ((uint32_t)blk[i*4+3] << 24);

    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f, g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5*i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3*i + 5) % 16; }
        else             { f = c ^ (b | ~d);       g = (7*i) % 16; }
        f = f + a + md5_k[i] + m[g];
        a = d; d = c; c = b;
        b = b + ROTL32(f, md5_s[i]);
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
}

static void md5_init(md5_ctx *c) {
    c->state[0] = 0x67452301; c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe; c->state[3] = 0x10325476;
    c->count = 0;
}

static void md5_update(md5_ctx *c, const uint8_t *data, size_t len) {
    size_t idx = (size_t)(c->count & 63);
    c->count += len;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(c->buffer + idx, data, part);
        md5_transform(c->state, c->buffer);
        for (i = part; i + 63 < len; i += 64)
            md5_transform(c->state, data + i);
        idx = 0;
    }
    memcpy(c->buffer + idx, data + i, len - i);
}

static void md5_final(md5_ctx *c, uint8_t out[16]) {
    uint64_t bits = c->count * 8;
    size_t idx = (size_t)(c->count & 63);
    static const uint8_t pad[64] = { 0x80 };
    size_t padlen = (idx < 56) ? (56 - idx) : (120 - idx);
    md5_update(c, pad, padlen);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (bits >> (8*i)) & 0xff;
    md5_update(c, lenbuf, 8);
    for (int i = 0; i < 4; i++) {
        out[i*4]   = (c->state[i]      ) & 0xff;
        out[i*4+1] = (c->state[i] >>  8) & 0xff;
        out[i*4+2] = (c->state[i] >> 16) & 0xff;
        out[i*4+3] = (c->state[i] >> 24) & 0xff;
    }
}

typedef struct { uint32_t state[8]; uint64_t count; uint8_t buffer[64]; } sha_ctx;

static const uint32_t sha_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define ROTR32(x,n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_transform(uint32_t st[8], const uint8_t blk[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)blk[i*4] << 24) | ((uint32_t)blk[i*4+1] << 16) |
               ((uint32_t)blk[i*4+2] << 8) | (uint32_t)blk[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR32(w[i-15], 7) ^ ROTR32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROTR32(w[i-2], 17) ^ ROTR32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = st[0], b = st[1], c = st[2], d = st[3];
    uint32_t e = st[4], f = st[5], g = st[6], h = st[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + sha_k[i] + w[i];
        uint32_t S0 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22);
        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

static void sha_init(sha_ctx *c) {
    c->state[0] = 0x6a09e667; c->state[1] = 0xbb67ae85;
    c->state[2] = 0x3c6ef372; c->state[3] = 0xa54ff53a;
    c->state[4] = 0x510e527f; c->state[5] = 0x9b05688c;
    c->state[6] = 0x1f83d9ab; c->state[7] = 0x5be0cd19;
    c->count = 0;
}

static void sha_update(sha_ctx *c, const uint8_t *data, size_t len) {
    size_t idx = (size_t)(c->count & 63);
    c->count += len;
    size_t part = 64 - idx;
    size_t i = 0;
    if (len >= part) {
        memcpy(c->buffer + idx, data, part);
        sha_transform(c->state, c->buffer);
        for (i = part; i + 63 < len; i += 64)
            sha_transform(c->state, data + i);
        idx = 0;
    }
    memcpy(c->buffer + idx, data + i, len - i);
}

static void sha_final(sha_ctx *c, uint8_t out[32]) {
    uint64_t bits = c->count * 8;
    size_t idx = (size_t)(c->count & 63);
    static const uint8_t pad[64] = { 0x80 };
    size_t padlen = (idx < 56) ? (56 - idx) : (120 - idx);
    sha_update(c, pad, padlen);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (bits >> (8*(7 - i))) & 0xff;
    sha_update(c, lenbuf, 8);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (c->state[i] >> 24) & 0xff;
        out[i*4+1] = (c->state[i] >> 16) & 0xff;
        out[i*4+2] = (c->state[i] >>  8) & 0xff;
        out[i*4+3] = (c->state[i]      ) & 0xff;
    }
}

static const char b64e[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *b64_encode(const uint8_t *in, size_t len) {
    size_t olen = 4 * ((len + 2) / 3);
    char *out = malloc(olen + 1);
    size_t i, o = 0;
    for (i = 0; i + 2 < len; i += 3) {
        uint32_t v = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
        out[o++] = b64e[(v >> 18) & 63];
        out[o++] = b64e[(v >> 12) & 63];
        out[o++] = b64e[(v >>  6) & 63];
        out[o++] = b64e[(v      ) & 63];
    }
    if (i < len) {
        uint32_t v = in[i] << 16;
        int rem = (int)(len - i);
        if (rem == 2) v |= in[i+1] << 8;
        out[o++] = b64e[(v >> 18) & 63];
        out[o++] = b64e[(v >> 12) & 63];
        out[o++] = (rem == 2) ? b64e[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return out;
}

static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + c - 'a';
    if (c >= '0' && c <= '9') return 52 + c - '0';
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint8_t *b64_decode(const char *in, size_t *out_len) {
    size_t n = strlen(in);
    uint8_t *out = malloc(n);
    size_t o = 0;
    int quad[4], qn = 0;
    for (size_t i = 0; i < n; i++) {
        if (in[i] == '=') break;
        int v = b64val(in[i]);
        if (v < 0) continue;
        quad[qn++] = v;
        if (qn == 4) {
            out[o++] = (quad[0] << 2) | (quad[1] >> 4);
            out[o++] = ((quad[1] & 15) << 4) | (quad[2] >> 2);
            out[o++] = ((quad[2] & 3) << 6) | quad[3];
            qn = 0;
        }
    }
    if (qn == 2) out[o++] = (quad[0] << 2) | (quad[1] >> 4);
    else if (qn == 3) {
        out[o++] = (quad[0] << 2) | (quad[1] >> 4);
        out[o++] = ((quad[1] & 15) << 4) | (quad[2] >> 2);
    }
    *out_len = o;
    return out;
}

static int to_bytes(Value v, const uint8_t **out, size_t *len) {
    if (v.type == JKY_STR) { *out = (const uint8_t *)v.v.s; *len = strlen(v.v.s); return 0; }
    if (v.type == JKY_BYTES) { *out = v.v.bytes.data; *len = v.v.bytes.len; return 0; }
    return -1;
}

static Value bytes_value(uint8_t *data, size_t len) {
    Value v; v.type = JKY_BYTES; v.rc = 0;
    v.v.bytes.data = data;
    v.v.bytes.len = len;
    return v;
}

static BuiltinResult bi_md5(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mkstr(""); return BUILTIN_OK; }
    md5_ctx ctx; md5_init(&ctx); md5_update(&ctx, data, len);
    uint8_t dig[16]; md5_final(&ctx, dig);
    char hex[33];
    for (int i = 0; i < 16; i++) snprintf(hex + i*2, 3, "%02x", dig[i]);
    *out = mkstr(hex);
    return BUILTIN_OK;
}

static BuiltinResult bi_sha256(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mkstr(""); return BUILTIN_OK; }
    sha_ctx ctx; sha_init(&ctx); sha_update(&ctx, data, len);
    uint8_t dig[32]; sha_final(&ctx, dig);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i*2, 3, "%02x", dig[i]);
    *out = mkstr(hex);
    return BUILTIN_OK;
}

static BuiltinResult bi_xor(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mknone(); return BUILTIN_OK; }

    const uint8_t *key; size_t klen;
    uint8_t kb[1];
    if (a[1].type == JKY_INT) {
        kb[0] = (uint8_t)(a[1].v.i & 0xff);
        key = kb; klen = 1;
    } else if (to_bytes(a[1], &key, &klen) < 0 || klen == 0) {
        *out = mknone(); return BUILTIN_OK;
    }

    uint8_t *r = malloc(len ? len : 1);
    for (size_t i = 0; i < len; i++) r[i] = data[i] ^ key[i % klen];
    *out = bytes_value(r, len);
    return BUILTIN_OK;
}

static BuiltinResult bi_b64e(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    const uint8_t *data; size_t len;
    if (to_bytes(a[0], &data, &len) < 0) { *out = mkstr(""); return BUILTIN_OK; }
    char *s = b64_encode(data, len);
    *out = mkstr(s);
    free(s);
    return BUILTIN_OK;
}

static BuiltinResult bi_b64d(VM *vm, Value *a, int c, Value *out) {
    (void)vm; (void)c;
    if (a[0].type != JKY_STR) { *out = bytes_value(malloc(1), 0); return BUILTIN_OK; }
    size_t olen = 0;
    uint8_t *d = b64_decode(a[0].v.s, &olen);
    *out = bytes_value(d, olen);
    return BUILTIN_OK;
}

const Builtin BUILTINS_CRYPTO[] = {
    { "crypto_md5",        1, 1, bi_md5    },
    { "crypto_sha256",     1, 1, bi_sha256 },
    { "crypto_xor",        2, 2, bi_xor    },
    { "crypto_b64_encode", 1, 1, bi_b64e   },
    { "crypto_b64_decode", 1, 1, bi_b64d   },
    /* web UI aliases */
    { "md5",        1, 1, bi_md5    },
    { "sha256",     1, 1, bi_sha256 },
    { "xor",        2, 2, bi_xor    },
    { "b64_encode", 1, 1, bi_b64e   },
    { "b64_decode", 1, 1, bi_b64d   },
    { NULL, 0, 0, NULL },
};
