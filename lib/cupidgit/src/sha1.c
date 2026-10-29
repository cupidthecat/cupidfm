#include "sha1.h"
#include <string.h>

// SHA-1 constants
#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_transform(uint32_t state[5], const uint8_t block[SHA1_BLOCK_SIZE]) {
    uint32_t w[80];
    uint32_t a, b, c, d, e, f, k, temp;

    // Prepare message schedule
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               ((uint32_t)block[i * 4 + 3]);
    }

    for (int i = 16; i < 80; i++) {
        w[i] = ROL32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    // Initialize working variables
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    // Main loop
    for (int i = 0; i < 80; i++) {
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        temp = ROL32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL32(b, 30);
        b = a;
        a = temp;
    }

    // Add this chunk's hash to result
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void SHA1_Init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

void SHA1_Update(SHA1_CTX *ctx, const void *data, size_t len) {
    const uint8_t *input = (const uint8_t *)data;
    size_t buffer_offset = (size_t)(ctx->count % SHA1_BLOCK_SIZE);

    ctx->count += len;

    // Fill buffer if we have leftover data
    if (buffer_offset > 0) {
        size_t copy_len = SHA1_BLOCK_SIZE - buffer_offset;
        if (copy_len > len) {
            copy_len = len;
        }
        memcpy(ctx->buffer + buffer_offset, input, copy_len);
        input += copy_len;
        len -= copy_len;
        buffer_offset += copy_len;

        if (buffer_offset == SHA1_BLOCK_SIZE) {
            sha1_transform(ctx->state, ctx->buffer);
            buffer_offset = 0;
        }
    }

    // Process full blocks
    while (len >= SHA1_BLOCK_SIZE) {
        sha1_transform(ctx->state, input);
        input += SHA1_BLOCK_SIZE;
        len -= SHA1_BLOCK_SIZE;
    }

    // Save remaining bytes
    if (len > 0) {
        memcpy(ctx->buffer, input, len);
    }
}

void SHA1_Final(uint8_t digest[SHA1_DIGEST_SIZE], SHA1_CTX *ctx) {
    uint8_t padding[SHA1_BLOCK_SIZE];
    uint64_t bit_count = ctx->count * 8;
    size_t pad_len;
    size_t buffer_offset = (size_t)(ctx->count % SHA1_BLOCK_SIZE);

    // Pad with 0x80 followed by zeros
    padding[0] = 0x80;
    memset(padding + 1, 0, sizeof(padding) - 1);

    // Calculate padding length
    if (buffer_offset < 56) {
        pad_len = 56 - buffer_offset;
    } else {
        pad_len = SHA1_BLOCK_SIZE + 56 - buffer_offset;
    }

    SHA1_Update(ctx, padding, pad_len);

    // Append length in bits (big-endian)
    padding[0] = (uint8_t)(bit_count >> 56);
    padding[1] = (uint8_t)(bit_count >> 48);
    padding[2] = (uint8_t)(bit_count >> 40);
    padding[3] = (uint8_t)(bit_count >> 32);
    padding[4] = (uint8_t)(bit_count >> 24);
    padding[5] = (uint8_t)(bit_count >> 16);
    padding[6] = (uint8_t)(bit_count >> 8);
    padding[7] = (uint8_t)(bit_count);

    SHA1_Update(ctx, padding, 8);

    // Output hash (big-endian)
    for (int i = 0; i < 5; i++) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }

    // Clear context
    memset(ctx, 0, sizeof(SHA1_CTX));
}
