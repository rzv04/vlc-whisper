#ifdef _WIN32

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "vw_source_decoder.h"

struct vw_source_decoder {
  IMFSourceReader* p_reader;
  int64_t duration_us;
  int64_t current_pts_us;
  bool eof_reached;
  int16_t leftover_buffer[4096];
  size_t leftover_count;
};

// URL decode helper to convert file URI (e.g. file:///C:/video%20file.mp4) to native Windows path.
static void vw_source_decoder_normalize_win32_path(const char* url, char* out_path, size_t max_out) {
  if (!url || !out_path || max_out == 0) return;
  const char* src = url;

  // Strip file:/// or file://
  if (strncmp(src, "file:///", 8) == 0) {
    src += 8;
  } else if (strncmp(src, "file://", 7) == 0) {
    src += 7;
  }

  size_t dst_idx = 0;
  while (*src && dst_idx + 1 < max_out) {
    if (*src == '%' && src[1] && src[2]) {
      char hex[3] = {src[1], src[2], '\0'};
      char* end = NULL;
      long val = strtol(hex, &end, 16);
      if (end == hex + 2) {
        out_path[dst_idx++] = (char)val;
        src += 3;
        continue;
      }
    }
    // Convert forward slash to backslash if drive letter follows
    if (*src == '/') {
      out_path[dst_idx++] = '\\';
    } else {
      out_path[dst_idx++] = *src;
    }
    src++;
  }
  out_path[dst_idx] = '\0';
}

vw_source_decoder_t* vw_source_decoder_open(const char* url, vw_source_decoder_info_t* info) {
  if (!url || url[0] == '\0') return NULL;

  char clean_path[4096];
  vw_source_decoder_normalize_win32_path(url, clean_path, sizeof(clean_path));

  int wlen = MultiByteToWideChar(CP_UTF8, 0, clean_path, -1, NULL, 0);
  if (wlen <= 0) return NULL;

  wchar_t* wpath = (wchar_t*)malloc((size_t)wlen * sizeof(wchar_t));
  if (!wpath) return NULL;
  MultiByteToWideChar(CP_UTF8, 0, clean_path, -1, wpath, wlen);

  IMFSourceReader* pReader = NULL;
  HRESULT hr = MFCreateSourceReaderFromURL(wpath, NULL, &pReader);
  free(wpath);

  if (FAILED(hr) || !pReader) {
    return NULL;
  }

  // Deselect all streams then select the first audio stream
  pReader->lpVtbl->SetStreamSelection(pReader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
  hr = pReader->lpVtbl->SetStreamSelection(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
  if (FAILED(hr)) {
    pReader->lpVtbl->Release(pReader);
    return NULL;
  }

  // Configure target output media type to PCM 16kHz 16-bit Mono
  IMFMediaType* pPartialType = NULL;
  hr = MFCreateMediaType(&pPartialType);
  if (FAILED(hr) || !pPartialType) {
    pReader->lpVtbl->Release(pReader);
    return NULL;
  }

  pPartialType->lpVtbl->SetGUID(pPartialType, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
  pPartialType->lpVtbl->SetGUID(pPartialType, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
  pPartialType->lpVtbl->SetUINT32(pPartialType, &MF_MT_AUDIO_NUM_CHANNELS, 1);
  pPartialType->lpVtbl->SetUINT32(pPartialType, &MF_MT_AUDIO_SAMPLES_PER_SECOND, 16000);
  pPartialType->lpVtbl->SetUINT32(pPartialType, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

  hr = pReader->lpVtbl->SetCurrentMediaType(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pPartialType);
  pPartialType->lpVtbl->Release(pPartialType);

  if (FAILED(hr)) {
    pReader->lpVtbl->Release(pReader);
    return NULL;
  }

  vw_source_decoder_t* decoder = (vw_source_decoder_t*)calloc(1, sizeof(vw_source_decoder_t));
  if (!decoder) {
    pReader->lpVtbl->Release(pReader);
    return NULL;
  }

  decoder->p_reader = pReader;
  decoder->duration_us = -1;

  // Retrieve media duration if available
  PROPVARIANT var;
  PropVariantInit(&var);
  if (SUCCEEDED(pReader->lpVtbl->GetPresentationAttribute(pReader, (DWORD)MF_SOURCE_READER_MEDIASOURCE, &MF_PD_DURATION,
                                                          &var)) &&
      var.vt == VT_UI8) {
    decoder->duration_us = (int64_t)(var.uhVal.QuadPart / 10);  // 100ns to µs
  }
  PropVariantClear(&var);

  if (info) {
    info->duration_us = decoder->duration_us;
    info->channels = 1;
    info->sample_rate = 16000;
    snprintf(info->container_format, sizeof(info->container_format), "MediaFoundation");
  }

  return decoder;
}

bool vw_source_decoder_seek(vw_source_decoder_t* decoder, int64_t target_pts_us) {
  if (!decoder || !decoder->p_reader) return false;

  PROPVARIANT var;
  PropVariantInit(&var);
  var.vt = VT_I8;
  var.hVal.QuadPart = (LONGLONG)(target_pts_us * 10);  // µs to 100ns units

  HRESULT hr = decoder->p_reader->lpVtbl->SetCurrentPosition(decoder->p_reader, &GUID_NULL, &var);
  PropVariantClear(&var);

  if (SUCCEEDED(hr)) {
    decoder->current_pts_us = target_pts_us;
    decoder->eof_reached = false;
    decoder->leftover_count = 0;
    return true;
  }
  return false;
}

size_t vw_source_decoder_read_s16le(vw_source_decoder_t* decoder, int16_t* out_pcm, size_t max_samples,
                                    int64_t* out_pts_us) {
  if (!decoder || !decoder->p_reader || !out_pcm || max_samples == 0) return 0;

  size_t total_samples = 0;

  // Drain any leftovers first
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

  while (total_samples < max_samples && !decoder->eof_reached) {
    DWORD dwStreamIndex = 0;
    DWORD dwStreamFlags = 0;
    LONGLONG llTimestamp = 0;
    IMFSample* pSample = NULL;

    HRESULT hr = decoder->p_reader->lpVtbl->ReadSample(decoder->p_reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0,
                                                       &dwStreamIndex, &dwStreamFlags, &llTimestamp, &pSample);

    if (FAILED(hr) || (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM)) {
      decoder->eof_reached = true;
      if (pSample) pSample->lpVtbl->Release(pSample);
      break;
    }

    if (!pSample) {
      continue;
    }

    IMFMediaBuffer* pBuffer = NULL;
    hr = pSample->lpVtbl->ConvertToContiguousBuffer(pSample, &pBuffer);
    if (SUCCEEDED(hr) && pBuffer) {
      BYTE* pAudioData = NULL;
      DWORD cbCurrentLength = 0;
      hr = pBuffer->lpVtbl->Lock(pBuffer, &pAudioData, NULL, &cbCurrentLength);
      if (SUCCEEDED(hr) && pAudioData && cbCurrentLength > 0) {
        size_t samples_in_sample = cbCurrentLength / sizeof(int16_t);
        const int16_t* in_samples = (const int16_t*)pAudioData;

        if (out_pts_us && total_samples == 0) {
          *out_pts_us = (int64_t)(llTimestamp / 10);
          decoder->current_pts_us = *out_pts_us;
        }

        size_t needed = max_samples - total_samples;
        if (samples_in_sample <= needed) {
          memcpy(out_pcm + total_samples, in_samples, samples_in_sample * sizeof(int16_t));
          total_samples += samples_in_sample;
          decoder->current_pts_us += (int64_t)((samples_in_sample * 1000000ULL) / 16000ULL);
        } else {
          memcpy(out_pcm + total_samples, in_samples, needed * sizeof(int16_t));
          total_samples += needed;
          decoder->current_pts_us += (int64_t)((needed * 1000000ULL) / 16000ULL);

          size_t remainder = samples_in_sample - needed;
          if (remainder > sizeof(decoder->leftover_buffer) / sizeof(int16_t)) {
            remainder = sizeof(decoder->leftover_buffer) / sizeof(int16_t);
          }
          memcpy(decoder->leftover_buffer, in_samples + needed, remainder * sizeof(int16_t));
          decoder->leftover_count = remainder;
        }
        pBuffer->lpVtbl->Unlock(pBuffer);
      }
      pBuffer->lpVtbl->Release(pBuffer);
    }
    pSample->lpVtbl->Release(pSample);
  }

  return total_samples;
}

int64_t vw_source_decoder_get_duration_us(const vw_source_decoder_t* decoder) {
  return decoder ? decoder->duration_us : -1;
}

void vw_source_decoder_close(vw_source_decoder_t* decoder) {
  if (decoder) {
    if (decoder->p_reader) {
      decoder->p_reader->lpVtbl->Release(decoder->p_reader);
      decoder->p_reader = NULL;
    }
    free(decoder);
  }
}

#endif  // _WIN32
