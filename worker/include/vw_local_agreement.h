#ifndef VW_LOCAL_AGREEMENT_H_
#define VW_LOCAL_AGREEMENT_H_

#include <stddef.h>
#include <stdint.h>

#define VW_LOCAL_AGREEMENT_MAX_WORDS 256U
#define VW_LOCAL_AGREEMENT_WORD_BYTES 128U
#define VW_LOCAL_AGREEMENT_COMMITTED_TAIL_WORDS 5U
#define VW_LOCAL_AGREEMENT_OVERLAP_US 1000000LL
#define VW_LOCAL_AGREEMENT_RETAIN_US 100000LL

typedef struct vw_local_agreement_word {
  int64_t start_pts_us;
  int64_t end_pts_us;
  char text_utf8[VW_LOCAL_AGREEMENT_WORD_BYTES];
} vw_local_agreement_word_t;

typedef struct vw_local_agreement {
  vw_local_agreement_word_t previous[VW_LOCAL_AGREEMENT_MAX_WORDS];
  size_t previous_count;
  vw_local_agreement_word_t committed_tail[VW_LOCAL_AGREEMENT_COMMITTED_TAIL_WORDS];
  size_t committed_tail_count;
  int64_t last_committed_end_us;
  int has_committed;
} vw_local_agreement_t;

// Initializes a bounded LocalAgreement-2 state with no prior hypothesis or committed timestamp; safe for stack-owned
// state and equivalent to resetting a previously used instance.
void vw_local_agreement_init(vw_local_agreement_t* state);

// Drops the unconfirmed hypothesis and committed-tail overlap memory at pause, seek, restart, or discontinuity so text
// from one acoustic epoch can never confirm text in another.
void vw_local_agreement_reset(vw_local_agreement_t* state);

// Applies exact LocalAgreement-2 to one timestamped word hypothesis, returning newly confirmed words in caller storage;
// the first hypothesis confirms nothing and output is bounded by output_capacity.
size_t vw_local_agreement_update(vw_local_agreement_t* state, const vw_local_agreement_word_t* hypothesis,
                                 size_t hypothesis_count, vw_local_agreement_word_t* output, size_t output_capacity);

// Joins one confirmed word run into a bounded caption string and returns its authentic first/last word PTS; fails
// instead of truncating when the destination cannot contain the complete UTF-8 text.
int vw_local_agreement_format_commit(const vw_local_agreement_word_t* words, size_t word_count, char* text_out,
                                     size_t text_capacity, int64_t* start_pts_us, int64_t* end_pts_us);

#endif  // VW_LOCAL_AGREEMENT_H_
