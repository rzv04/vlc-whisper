-- vlc_whisper_settings.lua -- VLC-Whisper Settings GUI (Lua extension).
-- VLC 3.0.23 Lua 5.1 runtime. Validates with `luac -p` (Lua 5.1).
-- Wired version: reads/writes plugin config namespace (whisper-backend,
-- model-path, whisper-language, whisper-threads) via cfg_get/set.
-- No translation code, network, timers, polling, or waits. Download progress is rendered by the C plugin/worker
-- path; see docs/plans/model_download_no_wait_plan.md for the command-only Lua architecture.

local dlg = nil
local w_engine = nil
local w_model = nil
local w_language = nil
local w_threads = nil
local w_status = nil

-- Robust config bridge: VLC 3.0 Lua exposes config as `config` in some builds
-- and `vlc.config` in others. Try both so the extension loads on either.
local function cfg_get(name)
  local ok, val = pcall(function()
    if vlc and vlc.config and vlc.config.get then return vlc.config.get(name) end
    if config and config.get then return config.get(name) end
    return nil
  end)
  if ok then return val end
  return nil
end
local function cfg_set(name, value)
  local ok, result = pcall(function()
    if vlc and vlc.config and vlc.config.set then return vlc.config.set(name, value) ~= false end
    if config and config.set then return config.set(name, value) ~= false end
    return false
  end)
  return ok and result == true
end

-- id -> string maps for dropdown get_value() results (Lua 5.1-safe).
local engine_map = { [1] = "auto", [2] = "gpu", [3] = "cpu" }
local engine_labels = { [1] = "auto (default)", [2] = "GPU (Vulkan)", [3] = "CPU only" }
local model_map = {
  [1] = "tiny.en",
  [2] = "tiny",
  [3] = "base.en",
  [4] = "base",
  [5] = "small",
  [6] = "medium",
  [7] = "large",
}
-- Catalog ids mirror model_map labels (plugin catalog ids are these strings).
local catalog_id_map = {
  [1] = "tiny.en",
  [2] = "tiny",
  [3] = "base.en",
  [4] = "base",
  [5] = "small",
  [6] = "medium",
  [7] = "large",
}
-- Relative model paths under models/ (selection allowed even if file absent;
-- expected E_MODEL_MISSING disables captions until provisioned -- see README).
local model_path_map = {
  [1] = "models/ggml-tiny.en.bin",
  [2] = "models/ggml-tiny.bin",
  [3] = "models/ggml-base.en.bin",
  [4] = "models/ggml-base.bin",
  [5] = "models/ggml-small.bin",
  [6] = "models/ggml-medium.bin",
  [7] = "models/ggml-large-v3.bin",
}
-- Reverse lookup: path -> id (for preselection from current model-path).
-- Derived with a numeric loop: the VLC 3.0 scan pass runs this file in a bare
-- Lua state with NO standard libraries (no pairs/ipairs), so top-level code
-- must be library-free. `#` is an operator, not a library call.
local model_path_to_id = {}
for _id = 1, #model_path_map do
  model_path_to_id[model_path_map[_id]] = _id
end

-- Language dropdown: concrete codes ONLY -- no "auto" entry in this dialog.
-- Automatic language selection is a later UI step even though bundled tiny is multilingual.
local language_map = {
  [1] = "en",
  [2] = "ro",
  [3] = "tr",
  [4] = "de",
  [5] = "fr",
  [6] = "es",
}
local language_labels = {
  [1] = "English (en)",
  [2] = "Romanian (ro)",
  [3] = "Turkish (tr)",
  [4] = "German (de)",
  [5] = "French (fr)",
  [6] = "Spanish (es)",
}

-- Reverse lookups for preselection.
local engine_to_id = { ["auto"] = 1, ["gpu"] = 2, ["cpu"] = 3 }
local language_to_id = { ["en"] = 1, ["ro"] = 2, ["tr"] = 3, ["de"] = 4, ["fr"] = 5, ["es"] = 6 }

local default_model_id = 2
local default_model_path = model_path_map[default_model_id] or "models/ggml-tiny.bin"

local function clamp_threads(v)
  local n = tonumber(v)
  if n == nil then n = 4 end
  n = math.floor(n)
  if n < 1 then n = 1 end
  if n > 16 then n = 16 end
  return n
end

local function resolve_model_id_from_path(path)
  if path == nil or path == "" then return default_model_id end
  -- Direct hit (relative path as stored).
  if model_path_to_id[path] ~= nil then return model_path_to_id[path] end
  -- Suffix match: handles absolute or bare filename forms (e.g. installed
  -- location "C:\\...\\models\\ggml-tiny.bin" or just filename).
  for _id = 1, #model_path_map do
    local _rel = model_path_map[_id]
    local fname = _rel:match("([^/\\]+)$")
    if fname and path:find(fname, 1, true) then
      return _id
    end
  end
  -- Fallback: try label substring (e.g. "tiny" in a custom path).
  for _id = 1, #model_map do
    local _label = model_map[_id]
    if _label and path:find(_label, 1, true) then
      return _id
    end
  end
  return default_model_id
end

-- VLC 3.0 selects the first added dropdown value and exposes no selection setter.
local function populate_dropdown(widget, labels, selected_id)
  widget:add_value(labels[selected_id], selected_id)
  for _id = 1, #labels do
    if _id ~= selected_id then widget:add_value(labels[_id], _id) end
  end
end

local function on_apply()
  local eng_id = w_engine and w_engine:get_value() or 1
  local mod_id = w_model and w_model:get_value() or default_model_id
  local lang_id = w_language and w_language:get_value() or 1
  local thr_text = w_threads and w_threads:get_text() or "4"

  local engine = engine_map[eng_id] or "auto"
  local model_label = model_map[mod_id] or "tiny"
  local model_path = model_path_map[mod_id] or default_model_path
  local language = language_map[lang_id] or "en"
  local threads = clamp_threads(thr_text)

  -- Reflect clamped value back into the text input when possible.
  if w_threads ~= nil then
    pcall(function() w_threads:set_text(tostring(threads)) end)
  end

  -- Write via cfg_set (Lua bridge to config_PutPsz / config_PutInt).
  -- All four keys are registered by the plugin (add_string / add_integer).
  pcall(function() cfg_set("whisper-backend", engine) end)
  pcall(function() cfg_set("model-path", model_path) end)
  pcall(function() cfg_set("whisper-language", language) end)
  pcall(function() cfg_set("whisper-threads", threads) end)

  vlc.msg.info("[VLC-Whisper] applied whisper-backend=" .. engine)
  vlc.msg.info("[VLC-Whisper] applied model-path=" .. model_path .. " (" .. model_label .. ")")
  vlc.msg.info("[VLC-Whisper] applied whisper-language=" .. language)
  vlc.msg.info("[VLC-Whisper] applied whisper-threads=" .. tostring(threads))

  -- Refresh detected-backend status label if present (reflects last STATUS
  -- drain; meaningful after first session STARTED).
  if w_status ~= nil then
    local active = nil
    pcall(function() active = cfg_get("whisper-backend-active") end)
    if active == nil or active == "" then active = "(pending -- start playback)" end
    pcall(function() w_status:set_text("Detected backend: " .. tostring(active)) end)
  end
end

local function on_abort_download()
  pcall(function() cfg_set("whisper-model-download", "abort") end)
  pcall(function() cfg_set("whisper-model-status", "aborting") end)
  vlc.msg.info("[VLC-Whisper] abort requested")
  pcall(function()
    if w_status ~= nil then
      w_status:set_text("Model download: aborting...")
    end
  end)
end

local function on_download()
  -- Determine selected model id: w_model:get_value() if dialog open, else resolve from cfg.
  local sel_id = nil
  pcall(function()
    if w_model ~= nil then sel_id = w_model:get_value() end
  end)
  if sel_id == nil then
    local cur_path = nil
    pcall(function() cur_path = cfg_get("model-path") end)
    sel_id = resolve_model_id_from_path(cur_path)
  end
  -- Map to catalog id (plugin expects these exact strings).
  local catalog_id = catalog_id_map[sel_id] or "tiny"

  -- Check if playback is active / worker is connected.
  local is_playing = false
  pcall(function()
    if vlc and vlc.input and vlc.input.is_playing then
      is_playing = vlc.input.is_playing()
    elseif vlc and vlc.playlist and vlc.playlist.is_playing then
      is_playing = vlc.playlist.is_playing()
    end
  end)

  -- Trigger download via whisper-model-download control variable without
  -- changing active model-path yet (worker must stay alive on existing model).
  local control_written = cfg_set("whisper-model-download", catalog_id)
  local control_visible = nil
  pcall(function()
    local pending = cfg_get("whisper-model-download")
    if pending ~= nil then control_visible = tostring(pending) == catalog_id end
  end)
  cfg_set("whisper-model-status", "downloading")
  cfg_set("whisper-model-progress", 0)
  -- Some VLC 3.0 builds expose config.set without a useful config.get readback.
  -- Treat nil as unverifiable, but reject an explicit mismatched value.
  if not control_written or control_visible == false then
    vlc.msg.err("[VLC-Whisper] download control was not retained by VLC config: " .. tostring(catalog_id))
    if w_status ~= nil then
      pcall(function() w_status:set_text("Model config unavailable; restart VLC and retry") end)
    end
    return
  end
  vlc.msg.info("[VLC-Whisper] download requested " .. tostring(catalog_id))

  if w_status ~= nil then
    local status = is_playing and "download requested" or "queued (play media to start worker)"
    pcall(function() w_status:set_text("Model " .. tostring(catalog_id) .. ": " .. status) end)
  end
end

local function build_dialog()
  dlg = vlc.dialog("VLC-Whisper Settings")

  -- Read current config values (nil-safe defaults).
  local cur_backend = nil
  local cur_model_path = nil
  local cur_language = nil
  local cur_threads = nil
  local cur_active = nil
  pcall(function() cur_backend = cfg_get("whisper-backend") end)
  pcall(function() cur_model_path = cfg_get("model-path") end)
  pcall(function() cur_language = cfg_get("whisper-language") end)
  pcall(function() cur_threads = cfg_get("whisper-threads") end)
  pcall(function() cur_active = cfg_get("whisper-backend-active") end)

  if cur_backend == nil or cur_backend == "" then cur_backend = "auto" end
  if cur_model_path == nil or cur_model_path == "" then cur_model_path = default_model_path end
  if cur_language == nil or cur_language == "" then cur_language = "en" end
  if cur_threads == nil or cur_threads == "" then cur_threads = "4" end
  cur_threads = tostring(cur_threads)
  if cur_active == nil or cur_active == "" then cur_active = "(pending -- start playback)" end

  local sel_engine = engine_to_id[cur_backend] or 1
  local sel_model = resolve_model_id_from_path(cur_model_path)
  local sel_language = language_to_id[cur_language] or 1

  -- Row 1: Engine
  dlg:add_label("Engine:", 1, 1, 1, 1)
  w_engine = dlg:add_dropdown(2, 1, 3, 1)
  populate_dropdown(w_engine, engine_labels, sel_engine)

  -- Row 2: Model (labels map to models/<name>.bin relative paths)
  dlg:add_label("Model:", 1, 2, 1, 1)
  w_model = dlg:add_dropdown(2, 2, 3, 1)
  local model_labels = {}
  for _id = 1, #model_path_map do
    local label = model_map[_id] or "model"
    if _id == 2 or _id == 4 then
      label = label .. " (multilingual)"
    end
    if _id == default_model_id then
      label = label .. " (bundled default)"
    end
    model_labels[_id] = label
  end
  populate_dropdown(w_model, model_labels, sel_model)

  -- Row 3: Language (concrete codes)
  dlg:add_label("Language:", 1, 3, 1, 1)
  w_language = dlg:add_dropdown(2, 3, 3, 1)
  populate_dropdown(w_language, language_labels, sel_language)

  -- Row 4: Threads -- text input (VLC Lua has no spinbox widget).
  dlg:add_label("Threads:", 1, 4, 1, 1)
  w_threads = dlg:add_text_input(cur_threads, 2, 4, 3, 1)

  -- Row 5: Action Buttons (Apply & Download Selected Model)
  dlg:add_button("Apply", on_apply, 1, 5, 2, 1)
  dlg:add_button("Download Selected Model", on_download, 3, 5, 2, 1)

  -- Row 6: Detected backend / download status
  w_status = dlg:add_label("Detected backend: " .. tostring(cur_active), 1, 6, 4, 1)

  -- Row 7: Hint
  dlg:add_label("Model selection allowed even if file absent (E_MODEL_MISSING disables captions).", 1, 7, 4, 1)
end

function descriptor()
  return {
    title = "VLC-Whisper Settings",
    version = "0.3.0",
    author = "vlc-whisper",
    url = "https://github.com/rzv04/vlc-whisper",
    shortdesc = "VLC-Whisper Settings",
    description = "Settings GUI for VLC-Whisper (Lua extension). "
      .. "Engine/Model/Language dropdowns + Threads input. "
      .. "Apply writes whisper-backend, model-path, whisper-language, whisper-threads via cfg_set; "
      .. "plugin polls config and respawns worker mid-play (brief caption gap); download progress is rendered by C. "
      .. "Detected backend label mirrors whisper-backend-active (STATUS v1.3 resolved_backend). "
      .. "Model dropdown maps labels to models/<name>.bin relative paths; selection allowed even if file absent. "
      .. "Menu entries: VLC-Whisper Settings, Download selected model, Abort model download.",
    capabilities = { "menu" },
  }
end

function activate()
  vlc.msg.info("[VLC-Whisper] extension activate -- building dialog")
  if dlg ~= nil then
    pcall(function() dlg:hide() end)
    dlg = nil
  end
  w_engine = nil
  w_model = nil
  w_language = nil
  w_threads = nil
  w_status = nil
  build_dialog()
  dlg:show()
  vlc.msg.info("[VLC-Whisper] dialog shown (Engine/Model/Language dropdowns + Threads)")
  return true
end

function deactivate()
  vlc.msg.info("[VLC-Whisper] extension deactivate")
  if dlg ~= nil then
    pcall(function() dlg:hide() end)
    dlg = nil
  end
  w_engine = nil
  w_model = nil
  w_language = nil
  w_threads = nil
  w_status = nil
end

function close()
  vlc.msg.info("[VLC-Whisper] extension close (user closed dialog)")
  pcall(function() vlc.deactivate() end)
end

function menu()
  return { "VLC-Whisper Settings", "Download selected model", "Abort model download" }
end

function trigger_menu(id)
  vlc.msg.info("[VLC-Whisper] trigger_menu id=" .. tostring(id))
  if id == 2 then
    on_download()
    return
  end
  if id == 3 then
    on_abort_download()
    return
  end
  if dlg == nil then
    activate()
  else
    pcall(function() dlg:show() end)
  end
end

-- Extension lifecycle callbacks (silences VLC lua warnings during media playback)
function meta_changed()
end

function input_changed()
end

function playing_changed()
end
