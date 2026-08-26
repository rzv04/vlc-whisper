#ifndef _WIN32

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vw_log.h"
#include "vw_source_decoder.h"

#ifdef VW_WITH_FFMPEG

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

struct vw_source_decoder {
  AVFormatContext* fmt_ctx;
  AVCodecContext* codec_ctx;
  SwrContext* swr_ctx;
  AVPacket* pkt;
  AVFrame* frame;
  int audio_stream_idx;
  int64_t duration_us;
  int64_t current_pts_us;
  int64_t stream_start_time;  // Container start_time in stream TB; media-relative normalization base.
  bool eof_reached;
  bool pkt_pending;
  int16_t leftover_buffer[4096];
  size_t leftover_count;
};

// Strips file:// URI prefix if present
static const char* vw_source_decoder_normalize_posix_path(const char* url, char* buf, size_t buf_len) {
  if (!url) return "";
  const char* src = url;
  if (strncmp(src, "file://", 7) == 0) {
    src += 7;
  }
  // URL decode %20 etc.
  size_t idx = 0;
  while (*src && idx + 1 < buf_len) {
    if (*src == '%' && src[1] && src[2]) {
      char hex[3] = {src[1], src[2], '\0'};
      char* end = NULL;
      long val = strtol(hex, &end, 16);
      if (end == hex + 2) {
        buf[idx++] = (char)val;
        src += 3;
        continue;
      }
    }
    buf[idx++] = *src++;
  }
  buf[idx] = '\0';
  return buf;
}

vw_source_decoder_t* vw_source_decoder_open(const char* url, vw_source_decoder_info_t* info) {
  if (!url || url[0] == '\0') return NULL;

  char clean_path[4096];
  vw_source_decoder_normalize_posix_path(url, clean_path, sizeof(clean_path));

  AVFormatContext* fmt_ctx = NULL;
  if (avformat_open_input(&fmt_ctx, clean_path, NULL, NULL) < 0) {
    return NULL;
  }

  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    avformat_close_input(&fmt_ctx);
    return NULL;
  }

  int audio_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (audio_idx < 0) {
    avformat_close_input(&fmt_ctx);
    return NULL;
  }

  AVStream* stream = fmt_ctx->streams[audio_idx];
  const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    avformat_close_input(&fmt_ctx);
    return NULL;
  }

  AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
  if (!codec_ctx) {
    avformat_close_input(&fmt_ctx);
    return NULL;
  }

  if (avcodec_parameters_to_context(codec_ctx, stream->codecpar) < 0 || avcodec_open2(codec_ctx, codec, NULL) < 0) {
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return NULL;
  }

  SwrContext* swr_ctx = NULL;
  AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
  if (swr_alloc_set_opts2(&swr_ctx, &out_ch_layout, AV_SAMPLE_FMT_S16, 16000, &codec_ctx->ch_layout,
                          codec_ctx->sample_fmt, codec_ctx->sample_rate, 0, NULL) < 0 ||
      !swr_ctx || swr_init(swr_ctx) < 0) {
    if (swr_ctx) swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return NULL;
  }

  vw_source_decoder_t* dec = (vw_source_decoder_t*)calloc(1, sizeof(vw_source_decoder_t));
  if (!dec) {
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    return NULL;
  }

  dec->fmt_ctx = fmt_ctx;
  dec->codec_ctx = codec_ctx;
  dec->swr_ctx = swr_ctx;
  dec->audio_stream_idx = audio_idx;
  // Normalize to a media-relative timeline: containers may begin at a nonzero
  // start_time (e.g. MPEG-TS), while VLC positions are media-relative from 0.
  dec->stream_start_time = (stream->start_time == AV_NOPTS_VALUE) ? 0 : stream->start_time;
  dec->pkt = av_packet_alloc();
  dec->frame = av_frame_alloc();
  dec->duration_us = fmt_ctx->duration > 0 ? (int64_t)av_rescale(fmt_ctx->duration, 1000000, AV_TIME_BASE) : -1;

  if (info) {
    info->duration_us = dec->duration_us;
    info->channels = 1;
    info->sample_rate = 16000;
    snprintf(info->container_format, sizeof(info->container_format), "%s",
             fmt_ctx->iformat ? fmt_ctx->iformat->name : "ffmpeg");
  }

  return dec;
}
bool vw_source_decoder_seek(vw_source_decoder_t* decoder, int64_t target_pts_us) {
  if (!decoder || !decoder->fmt_ctx || decoder->audio_stream_idx < 0) return false;

  AVStream* stream = decoder->fmt_ctx->streams[decoder->audio_stream_idx];
  int64_t target_ts = av_rescale_q(target_pts_us, (AVRational){1, 1000000}, stream->time_base);
  // Media-relative position -> raw stream timestamp: add the container offset
  // so seeks land on the requested media time even when start_time != 0.
  target_ts += decoder->stream_start_time;

  if (av_seek_frame(decoder->fmt_ctx, decoder->audio_stream_idx, target_ts, AVSEEK_FLAG_BACKWARD) >= 0) {
    avcodec_flush_buffers(decoder->codec_ctx);
    decoder->current_pts_us = target_pts_us;
    decoder->eof_reached = false;
    decoder->leftover_count = 0;
    if (decoder->pkt_pending) {
      av_packet_unref(decoder->pkt);
      decoder->pkt_pending = false;
    }
    return true;
  }
  return false;
}

static void vw_source_decoder_process_frame(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples,
                                            size_t* inout_total_samples, int64_t* out_pts_us) {
  AVStream* stream = decoder->fmt_ctx->streams[decoder->audio_stream_idx];
  int64_t frame_pts = decoder->frame->pts;
  if (frame_pts == AV_NOPTS_VALUE) {
    frame_pts = decoder->frame->best_effort_timestamp;
  }
  if (frame_pts == AV_NOPTS_VALUE) {
    frame_pts = decoder->frame->pkt_dts;
  }
  if (frame_pts != AV_NOPTS_VALUE && stream) {
    // Raw stream timestamp -> media-relative: strip the container offset so
    // caption scheduling aligns with VLC's media timeline (mirror of seek).
    frame_pts -= decoder->stream_start_time;
    if (frame_pts < 0) frame_pts = 0;  // head padding can precede start_time
    decoder->current_pts_us = (int64_t)av_rescale_q(frame_pts, stream->time_base, (AVRational){1, 1000000});
  }

  if (out_pts_us && *inout_total_samples == 0) {
    *out_pts_us = decoder->current_pts_us;
  }

  int16_t resample_buf[4096];
  uint8_t* out_ptrs[1] = {(uint8_t*)resample_buf};
  int converted = swr_convert(decoder->swr_ctx, out_ptrs, (int)(sizeof(resample_buf) / sizeof(int16_t)),
                              (const uint8_t**)decoder->frame->extended_data, decoder->frame->nb_samples);

  if (converted > 0) {
    size_t samples_converted = (size_t)converted;
    size_t needed = max_samples - *inout_total_samples;
    if (samples_converted <= needed) {
      memcpy(out_pcm + *inout_total_samples, resample_buf, samples_converted * sizeof(int16_t));
      *inout_total_samples += samples_converted;
      decoder->current_pts_us += (int64_t)((samples_converted * 1000000ULL) / 16000ULL);
    } else {
      memcpy(out_pcm + *inout_total_samples, resample_buf, needed * sizeof(int16_t));
      *inout_total_samples += needed;
      decoder->current_pts_us += (int64_t)((needed * 1000000ULL) / 16000ULL);

      size_t remainder = samples_converted - needed;
      if (remainder > sizeof(decoder->leftover_buffer) / sizeof(int16_t)) {
        remainder = sizeof(decoder->leftover_buffer) / sizeof(int16_t);
      }
      memcpy(decoder->leftover_buffer, resample_buf + needed, remainder * sizeof(int16_t));
      decoder->leftover_count = remainder;
    }
  }
}

size_t vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples,
                                    int64_t* out_pts_us) {
  if (!decoder || !decoder->fmt_ctx || !out_pcm || max_samples == 0) return 0;

  size_t total_samples = 0;

  // Drain leftovers first
  if (decoder->leftover_count > 0) {
    size_t to_copy = decoder->leftover_count < max_samples ? decoder->leftover_count : max_samples;
    memcpy(out_pcm, decoder->leftover_buffer, to_copy * sizeof(int16_t));
    if (out_pts_us && total_samples == 0) {
      *out_pts_us = decoder->current_pts_us;
    }
    total_samples += to_copy;
    decoder->current_pts_us += (int64_t)((to_copy * 1000000ULL) / 16000ULL);

    if (to_copy < decoder->leftover_count) {
      memmove(decoder->leftover_buffer, decoder->leftover_buffer + to_copy,
              (decoder->leftover_count - to_copy) * sizeof(int16_t));
      decoder->leftover_count -= to_copy;
      return total_samples;
    } else {
      decoder->leftover_count = 0;
    }
  }

  int defer_no_progress = 0;
  while (total_samples < max_samples && !decoder->eof_reached) {
    if (!decoder->pkt_pending) {
      int ret = av_read_frame(decoder->fmt_ctx, decoder->pkt);
      if (ret < 0) {
        decoder->eof_reached = true;
        break;
      }
    }

    if (decoder->pkt->stream_index == decoder->audio_stream_idx) {
      size_t progress_total = total_samples;
      int progress_leftover = (int)decoder->leftover_count;
      int send_ret = avcodec_send_packet(decoder->codec_ctx, decoder->pkt);
      if (send_ret == AVERROR(EAGAIN)) {
        // Decoder output buffer is full: drain what we can, then retry the same
        // packet. We never drop the packet here -- it is deferred to the next
        // iteration so no compressed audio is lost.
        while (total_samples < max_samples && decoder->leftover_count == 0 &&
               avcodec_receive_frame(decoder->codec_ctx, decoder->frame) >= 0) {
          vw_source_decoder_process_frame(decoder, out_pcm, max_samples, &total_samples, out_pts_us);
        }
        if (total_samples < max_samples && decoder->leftover_count == 0) {
          send_ret = avcodec_send_packet(decoder->codec_ctx, decoder->pkt);
        }
      }

      if (send_ret >= 0) {
        while (total_samples < max_samples && decoder->leftover_count == 0 &&
               avcodec_receive_frame(decoder->codec_ctx, decoder->frame) >= 0) {
          vw_source_decoder_process_frame(decoder, out_pcm, max_samples, &total_samples, out_pts_us);
        }
        av_packet_unref(decoder->pkt);
        decoder->pkt_pending = false;
        defer_no_progress = 0;
      } else if (send_ret == AVERROR(EAGAIN)) {
        // Decoder still cannot accept the packet after draining. Defer it and loop
        // without reading a new packet until the decoder makes room. Guard against a
        // genuine deadlock: if draining produced no new samples and the leftover
        // buffer did not shrink twice in a row, stop to avoid an infinite loop.
        if (total_samples == progress_total && (int)decoder->leftover_count == progress_leftover) {
          defer_no_progress++;
        } else {
          defer_no_progress = 0;
        }
        if (defer_no_progress >= 2) {
          av_packet_unref(decoder->pkt);
          decoder->pkt_pending = false;
          break;
        }
        decoder->pkt_pending = true;
      } else {
        if (send_ret != AVERROR_EOF) {
          vw_log_event(VW_LOG_LEVEL_WARN, "DECODER_FFMPEG_SEND", "avcodec_send_packet failed (%d)", send_ret);
        }
        av_packet_unref(decoder->pkt);
        decoder->pkt_pending = false;
      }
    } else {
      av_packet_unref(decoder->pkt);
      decoder->pkt_pending = false;
    }
  }

  return total_samples;
}

int64_t vw_source_decoder_get_duration_us(const vw_source_decoder_t* decoder) {
  return decoder ? decoder->duration_us : -1;
}

void vw_source_decoder_close(vw_source_decoder_t* decoder) {
  if (decoder) {
    if (decoder->pkt) av_packet_free(&decoder->pkt);
    if (decoder->frame) av_frame_free(&decoder->frame);
    if (decoder->swr_ctx) swr_free(&decoder->swr_ctx);
    if (decoder->codec_ctx) avcodec_free_context(&decoder->codec_ctx);
    if (decoder->fmt_ctx) avformat_close_input(&decoder->fmt_ctx);
    free(decoder);
  }
}

#else  // !VW_WITH_FFMPEG

struct vw_source_decoder {
  int dummy;
};

vw_source_decoder_t* vw_source_decoder_open(const char* url, vw_source_decoder_info_t* info) {
  (void)url;
  (void)info;
  return NULL;
}

bool vw_source_decoder_seek(vw_source_decoder_t* decoder, int64_t target_pts_us) {
  (void)decoder;
  (void)target_pts_us;
  return false;
}

size_t vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples,
                                    int64_t* out_pts_us) {
  (void)decoder;
  (void)out_pcm;
  (void)max_samples;
  (void)out_pts_us;
  return 0;
}

int64_t vw_source_decoder_get_duration_us(const vw_source_decoder_t* decoder) {
  (void)decoder;
  return -1;
}

void vw_source_decoder_close(vw_source_decoder_t* decoder) {
  if (decoder) free(decoder);
}

#endif  // VW_WITH_FFMPEG

#endif  // !_WIN32
