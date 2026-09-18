#ifndef VW_QUALITY_HOOK_H_
#define VW_QUALITY_HOOK_H_

#include <stdint.h>

#define VW_QUALITY_MARKER_ENV "VW_QUALITY_MARKER_PREFIX"
#define VW_QUALITY_EOF_MARKER_SUFFIX ".source-eof"
#define VW_QUALITY_DROPS_MARKER_SUFFIX ".dropped-audio-us"

#if defined(__cplusplus)
extern "C" {
#endif

void vw_quality_hook_on_queue_drop(uint64_t dropped_audio_us);

#if defined(__cplusplus)
}
#endif

#endif  // VW_QUALITY_HOOK_H_
