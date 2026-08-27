#include "vw_sha256.h"

#include <string.h>

#define VW_SHA256_ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const uint32_t kVW_SHA256_K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

static void vw_sha256_transform(vw_sha256_context_t* ctx, const uint8_t data[64]) {
  uint32_t a, b, c, d, e, f, g, h;
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)data[i * 4] << 24) | ((uint32_t)data[i * 4 + 1] << 16) | ((uint32_t)data[i * 4 + 2] << 8) |
           (uint32_t)data[i * 4 + 3];
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = VW_SHA256_ROTR(w[i - 15], 7) ^ VW_SHA256_ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = VW_SHA256_ROTR(w[i - 2], 17) ^ VW_SHA256_ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = VW_SHA256_ROTR(e, 6) ^ VW_SHA256_ROTR(e, 11) ^ VW_SHA256_ROTR(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + S1 + ch + kVW_SHA256_K[i] + w[i];
    uint32_t S0 = VW_SHA256_ROTR(a, 2) ^ VW_SHA256_ROTR(a, 13) ^ VW_SHA256_ROTR(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
}

void vw_sha256_init(vw_sha256_context_t* ctx) {
  if (!ctx) return;
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667U;
  ctx->state[1] = 0xbb67ae85U;
  ctx->state[2] = 0x3c6ef372U;
  ctx->state[3] = 0xa54ff53aU;
  ctx->state[4] = 0x510e527fU;
  ctx->state[5] = 0x9b05688cU;
  ctx->state[6] = 0x1f83d9abU;
  ctx->state[7] = 0x5be0cd19U;
}

void vw_sha256_update(vw_sha256_context_t* ctx, const uint8_t* data, size_t len) {
  if (!ctx || (!data && len > 0)) return;
  for (size_t i = 0; i < len; i++) {
    ctx->data[ctx->datalen] = data[i];
    ctx->datalen++;
    if (ctx->datalen == 64) {
      vw_sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

void vw_sha256_final(vw_sha256_context_t* ctx, uint8_t hash[32]) {
  if (!ctx || !hash) return;
  uint32_t i = ctx->datalen;
  // Pad whatever data is left in the buffer.
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) ctx->data[i++] = 0x00;
    vw_sha256_transform(ctx, ctx->data);
    memset(ctx->data, 0, 56);
  }
  // Append total bit length as 64-bit big-endian.
  ctx->bitlen += (uint64_t)ctx->datalen * 8;
  ctx->data[63] = (uint8_t)(ctx->bitlen);
  ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
  ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
  ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
  ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
  ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
  ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
  ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
  vw_sha256_transform(ctx, ctx->data);
  for (i = 0; i < 4; i++) {
    for (uint32_t j = 0; j < 8; j++) {
      hash[i + j * 4] = (uint8_t)((ctx->state[j] >> (24 - i * 8)) & 0xFFU);
    }
  }
}

void vw_sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  vw_sha256_context_t ctx;
  vw_sha256_init(&ctx);
  vw_sha256_update(&ctx, data, len);
  vw_sha256_final(&ctx, out);
}

void vw_sha256_to_hex(const uint8_t hash[32], char out_hex[65]) {
  static const char kHex[] = "0123456789abcdef";
  if (!hash || !out_hex) return;
  for (int i = 0; i < 32; i++) {
    out_hex[i * 2] = kHex[(hash[i] >> 4) & 0xF];
    out_hex[i * 2 + 1] = kHex[hash[i] & 0xF];
  }
  out_hex[64] = '\0';
}
