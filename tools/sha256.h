#ifndef TOOLS_SHA256_H
#define TOOLS_SHA256_H

/*
 * SHA-256, for the bake tools.
 *
 * The device verifies a downloaded bundle with mbedtls, which ESP-IDF ships
 * and a host build does not. This is the other half of that check, and it is
 * here rather than as a dependency because it is the only cryptographic thing
 * either tool does.
 *
 * Public-domain construction, straight from FIPS 180-4.
 */

#include <stdint.h>
#include <string.h>

struct Sha256
{
    uint32_t state[8];
    uint64_t bits;
    uint8_t buf[64];
    size_t buf_len;
};

static const uint32_t k_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

#define SHA256_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void
sha256_block(struct Sha256* s, const uint8_t* p)
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for( i = 0; i < 16; i++ )
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];

    for( ; i < 64; i++ )
    {
        uint32_t s0 = SHA256_ROR(w[i - 15], 7) ^ SHA256_ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = SHA256_ROR(w[i - 2], 17) ^ SHA256_ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = s->state[0]; b = s->state[1]; c = s->state[2]; d = s->state[3];
    e = s->state[4]; f = s->state[5]; g = s->state[6]; h = s->state[7];

    for( i = 0; i < 64; i++ )
    {
        uint32_t S1 = SHA256_ROR(e, 6) ^ SHA256_ROR(e, 11) ^ SHA256_ROR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + k_sha256_k[i] + w[i];
        uint32_t S0 = SHA256_ROR(a, 2) ^ SHA256_ROR(a, 13) ^ SHA256_ROR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    s->state[0] += a; s->state[1] += b; s->state[2] += c; s->state[3] += d;
    s->state[4] += e; s->state[5] += f; s->state[6] += g; s->state[7] += h;
}

static void
sha256_init(struct Sha256* s)
{
    s->state[0] = 0x6a09e667u; s->state[1] = 0xbb67ae85u;
    s->state[2] = 0x3c6ef372u; s->state[3] = 0xa54ff53au;
    s->state[4] = 0x510e527fu; s->state[5] = 0x9b05688cu;
    s->state[6] = 0x1f83d9abu; s->state[7] = 0x5be0cd19u;
    s->bits = 0;
    s->buf_len = 0;
}

static void
sha256_update(struct Sha256* s, const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;

    s->bits += (uint64_t)len * 8;

    while( len > 0 )
    {
        size_t take = 64 - s->buf_len;

        if( take > len )
            take = len;

        memcpy(s->buf + s->buf_len, p, take);
        s->buf_len += take;
        p += take;
        len -= take;

        if( s->buf_len == 64 )
        {
            sha256_block(s, s->buf);
            s->buf_len = 0;
        }
    }
}

static void
sha256_final(struct Sha256* s, uint8_t out[32])
{
    uint64_t bits = s->bits;
    uint8_t pad = 0x80;
    uint8_t len_be[8];
    int i;

    sha256_update(s, &pad, 1);
    s->bits = bits; /* the padding is not message length */

    pad = 0x00;
    while( s->buf_len != 56 )
    {
        sha256_update(s, &pad, 1);
        s->bits = bits;
    }

    for( i = 0; i < 8; i++ )
        len_be[i] = (uint8_t)(bits >> (56 - 8 * i));

    sha256_update(s, len_be, 8);

    for( i = 0; i < 8; i++ )
    {
        out[i * 4] = (uint8_t)(s->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(s->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(s->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)s->state[i];
    }
}

#endif /* TOOLS_SHA256_H */
