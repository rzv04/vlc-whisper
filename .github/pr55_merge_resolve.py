from pathlib import Path


def resolve_blocks(path, resolutions):
    p = Path(path)
    lines = p.read_text().splitlines(keepends=True)
    out = []
    block_index = 0
    i = 0
    while i < len(lines):
        if not lines[i].startswith("<<<<<<< "):
            out.append(lines[i])
            i += 1
            continue
        i += 1
        ours = []
        while i < len(lines) and not lines[i].startswith("======="):
            ours.append(lines[i])
            i += 1
        if i >= len(lines):
            raise RuntimeError(f"{path}: malformed conflict (missing =======)")
        i += 1
        theirs = []
        while i < len(lines) and not lines[i].startswith(">>>>>>> "):
            theirs.append(lines[i])
            i += 1
        if i >= len(lines):
            raise RuntimeError(f"{path}: malformed conflict (missing >>>>>>>)")
        i += 1
        if block_index >= len(resolutions):
            raise RuntimeError(f"{path}: unexpected conflict block {block_index}")
        out.extend(resolutions[block_index](ours, theirs))
        block_index += 1
    if block_index != len(resolutions):
        raise RuntimeError(f"{path}: expected {len(resolutions)} conflicts, found {block_index}")
    p.write_text("".join(out))


def keep(_ours, _theirs, text):
    return [line + "\n" for line in text.splitlines()]


# Lua: preserve main's paused-preview setting while giving it a dedicated row after
# the abstraction branch's new ASR engine/backend controls.
def lua_resolution(_ours, _theirs):
    return keep(_ours, _theirs, '''  w_show_paused =
    dlg:add_check_box("Show subtitles while paused (local files only)", cur_show_paused, 1, 7, 4, 1)

  w_trans_enabled = dlg:add_check_box("Auto translation (real-time subtitles)", cur_trans_enabled, 1, 8, 4, 1)''')


resolve_blocks("lua/extensions/vlc_whisper_settings.lua", [lua_resolution])
p = Path("lua/extensions/vlc_whisper_settings.lua")
text = p.read_text()
replacements = [
    ('dlg:add_label("Source (from):", 1, 8, 1, 1)', 'dlg:add_label("Source (from):", 1, 9, 1, 1)'),
    ('dlg:add_dropdown(2, 8, 3, 1)', 'dlg:add_dropdown(2, 9, 3, 1)'),
    ('dlg:add_label("Translation (to):", 1, 9, 1, 1)', 'dlg:add_label("Translation (to):", 1, 10, 1, 1)'),
    ('dlg:add_dropdown(2, 9, 3, 1)', 'dlg:add_dropdown(2, 10, 3, 1)'),
    ('dlg:add_label("Screen placement:", 1, 10, 1, 1)', 'dlg:add_label("Screen placement:", 1, 11, 1, 1)'),
    ('dlg:add_dropdown(2, 10, 3, 1)', 'dlg:add_dropdown(2, 11, 3, 1)'),
    ('dlg:add_label("Translation test:", 1, 11, 1, 1)', 'dlg:add_label("Translation test:", 1, 12, 1, 1)'),
    ('dlg:add_button("How to test", on_test_translate, 2, 11, 3, 1)', 'dlg:add_button("How to test", on_test_translate, 2, 12, 3, 1)'),
    ('dlg:add_label("Worker runtime performs translation; this dialog never makes HTTP requests.", 1, 12, 4, 1)', 'dlg:add_label("Worker runtime performs translation; this dialog never makes HTTP requests.", 1, 13, 4, 1)'),
    ('dlg:add_button("Apply", on_apply, 1, 13, 2, 1)', 'dlg:add_button("Apply", on_apply, 1, 14, 2, 1)'),
    ('dlg:add_button("Download Selected Model", on_download, 3, 13, 2, 1)', 'dlg:add_button("Download Selected Model", on_download, 3, 14, 2, 1)'),
    ('dlg:add_label("Detected backend: " .. tostring(cur_active), 1, 14, 4, 1)', 'dlg:add_label("Detected backend: " .. tostring(cur_active), 1, 15, 4, 1)'),
    ('dlg:add_label("Model availability: checking...", 1, 15, 4, 1)', 'dlg:add_label("Model availability: checking...", 1, 16, 4, 1)'),
    ('dlg:add_label(".en models force English; enabling translation sends finalized subtitle text to Google.", 1, 16, 4, 1)', 'dlg:add_label(".en models force English; enabling translation sends finalized subtitle text to Google.", 1, 17, 4, 1)'),
]
for old, new in replacements:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"lua row replacement expected once, found {count}: {old}")
    text = text.replace(old, new, 1)
p.write_text(text)


# Protocol: main already allocated 11..15 to translation diagnostics. Keep them and
# give the experimental engine-unavailable error the next free value.
def protocol_resolution(_ours, _theirs):
    return keep(_ours, _theirs, '''  E_TRANSLATION_PROVIDER = 11,
  E_TRANSLATION_TRANSPORT = 12,
  E_TRANSLATION_PARSE = 13,
  E_TRANSLATION_DEADLINE = 14,
  E_TRANSLATION_LOCAL = 15,
  E_ENGINE_UNAVAILABLE = 16''')


resolve_blocks("protocol/include/vw_protocol_types.h", [protocol_resolution])
p = Path("protocol/include/vw_protocol_types.h")
text = p.read_text()
if text.count("#define VW_ERROR_MAX 15U") != 1:
    raise RuntimeError("expected one VW_ERROR_MAX 15U")
text = text.replace("#define VW_ERROR_MAX 15U", "#define VW_ERROR_MAX 16U", 1)
p.write_text(text)


# Worker STATUS: retain the generic ASR facade from PR 55 and main's new authoritative
# session-state / queued-audio metrics.
def worker_signature(_ours, _theirs):
    return keep(_ours, _theirs, '''                                  const vw_worker_config_t* config, const vw_asr_engine_t* engine,
                                  const vw_worker_queue_t* queue, const vw_audio_buffer_t* audio_buf,
                                  bool session_active, bool paused, uint64_t* sequence) {''')


def worker_body(_ours, _theirs):
    return keep(_ours, _theirs, '''  if (!session_active) {
    status.state = VW_SESSION_STATE_IDLE;
  } else if (paused) {
    status.state = VW_SESSION_STATE_PAUSED;
  } else {
    status.state = VW_SESSION_STATE_PLAYING;
  }
  uint64_t queued_audio_us = queue ? vw_worker_queue_get_queued_audio_us(queue) : 0;
  if (audio_buf) {
    size_t samples = vw_audio_buffer_get_available(audio_buf);
    queued_audio_us += (uint64_t)samples * 1000000ULL / VW_AUDIO_SAMPLE_RATE;
  }
  status.queued_audio_us = (int64_t)queued_audio_us;
  status.inference_us = (int64_t)(engine ? vw_asr_engine_get_total_inference_us(engine) : 0);''')


resolve_blocks("worker/src/vw_worker.c", [worker_signature, worker_body])

for path in [
    "lua/extensions/vlc_whisper_settings.lua",
    "protocol/include/vw_protocol_types.h",
    "worker/src/vw_worker.c",
]:
    text = Path(path).read_text()
    for marker in ("<<<<<<<", "=======", ">>>>>>>"):
        if marker in text:
            raise RuntimeError(f"{path}: unresolved merge marker {marker}")
