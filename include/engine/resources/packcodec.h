#pragma once

/**
 * The packs' block codec (docs/PACKS.md): byte-oriented LZ, no bit stream. Shared by the game (decoding) and
 * tools/assetc (encoding: built as a DLL from the same source with PACK_CODEC_ENCODER).
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Decodes a block into exactly `unpacked` bytes; 0 when the block is not whole or not well formed. */
int pack_decode(const uint8_t *in, size_t packed, uint8_t *out, size_t unpacked);

#ifdef PACK_CODEC_ENCODER
/** The most a block of `size` bytes can take encoded. */
size_t pack_bound(size_t size);
/** Encodes a block (at most 16 MB); the encoded size, or 0 when it would not be smaller. `level` 1 (fast) to 9. */
size_t pack_encode(const uint8_t *in, size_t size, uint8_t *out, size_t capacity, int level);
#endif

#ifdef __cplusplus
}
#endif
