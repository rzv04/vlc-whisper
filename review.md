# Unstaged Changes Review

**README.md** — 🔵 nit: test docs duplicate CMakePresets.json content already in README. Shorten to link to presets instead of full command list.

**plugin/include/vw_queue.h:L8-13** — 🟡 risk: inlined `vw_audio_chunk_t` drops `#include "vw_audio_buffer.h"`. Old code used `vw_audio_buffer_t` in struct fields? If queue had `vw_audio_buffer_t*` member, removal breaks. Verify no other file includes `vw_queue.h` expecting `vw_audio_buffer.h` transitively.

**protocol/src/vw_protocol_codec.c — CAPTION_SEGMENT** — 🔴 bug: encodes `bool is_final` (1 byte) via `ENC_FIELD`, never encodes `uint8_t flags`. Api-contracts says wire format is `u8 flags` (bit0=final, bit1=replace). `flags` field dead on decode. Fix: encode `flags` only, drop `is_final` from wire.

**protocol/src/vw_protocol_codec.c — START_SESSION** — 🟡 risk: `model_id` and `language` serialized as fixed 64/16 bytes. Api-contracts says wire strings are `u16 length + bytes`. Wastes ~76 bytes per START and mismatches spec. Fix: encode as length-prefixed.

**protocol/src/vw_protocol_validate.c:L15-31** — 🟡 risk: `is_valid_utf8()` accepts overlong sequences (e.g. `0xC0 0x80` decodes as NUL). Overlong UTF-8 bypasses control-char filter below. Add overlong rejection per RFC 3629.

**protocol/src/vw_protocol_validate.c:L62-63** — 🔵 nit: `pcm_bytes = duration_us * 32 / 1000` uses integer truncation. For `duration_us=1` → `expected_bytes=0`, valid audio under 31.25 µs silently passes. Negligible at 16kHz (0.5 sample), but comment would help.

**tests/unit/test_protocol_validate.c — AUDIO test** — 🔵 nit: `pcm_data = (const uint8_t*)"12"` with `pcm_bytes=32000`. Pointer non-null so validation passes, but data out of bounds if reader dereferences. Use a real buffer of `expected_bytes`.

**tests/include/vw_test.h** — 🔵 nit: `EXPECT_EQ_STR` uses `strcmp` — crashes if either pointer is NULL. Validate tests never pass NULL, but a NULL guard costs nothing and prevents future surprise.

**protocol/src/vw_protocol_codec.c — VW_MSG_SHUTDOWN/VW_MSG_STARTED** — 🔵 nit: encode returns `written=0`, decode returns true with no fields. Consistent with zero-payload messages, but if STARTED ever gains fields, back-compat breaks. Add a comment noting these are zero-payload sentinel messages.
