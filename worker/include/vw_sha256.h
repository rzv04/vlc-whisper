#ifndef VW_SHA256_H_
#define VW_SHA256_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Streaming SHA-256 context buffering 64-byte blocks and tracking total bit
// length as 64-bit big-endian counter to support multi-gigabyte model files
// without overflow while keeping no global state.
typedef struct vw_sha256_context {
  uint32_t state[8];
  uint64_t bitlen;
  uint8_t data[64];
  uint32_t datalen;
} vw_sha256_context_t;

// Initializes SHA-256 context to FIPS 180-4 initial hash values and clears
// block buffer and bit counter for a fresh incremental hashing session.
void vw_sha256_init(vw_sha256_context_t* ctx);

// Feeds arbitrary-length input into the streaming context, buffering partial
// blocks internally and compressing full 64-byte chunks while updating bit
// length for correct final padding.
void vw_sha256_update(vw_sha256_context_t* ctx, const uint8_t* data, size_t len);

// Finalizes hash computation by appending padding and 64-bit big-endian bit
// length, compressing the last block, and writing the 32-byte digest output.
void vw_sha256_final(vw_sha256_context_t* ctx, uint8_t hash[32]);

// One-shot helper computing SHA-256 of a contiguous buffer in a single
// call without managing context lifetime manually for short inputs or tests.
void vw_sha256(const uint8_t* data, size_t len, uint8_t out[32]);

// Converts a 32-byte binary SHA-256 digest into a 64-character lowercase hex
// string with NUL terminator for catalog comparison and diagnostic logging.
void vw_sha256_to_hex(const uint8_t hash[32], char out_hex[65]);

#ifdef __cplusplus
}
#endif

#endif  // VW_SHA256_H_
