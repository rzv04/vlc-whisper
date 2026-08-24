-- vlc_whisper_settings.lua — feasibility spike for VLC Lua extension settings GUI.
-- Scope: mechanism proof only. No wiring to the whisper plugin/worker, no network I/O.
-- VLC 3.0.23 Lua 5.1 runtime. Validates with `luac -p` (Lua 5.1).
-- Invariants preserved: never touches audio callbacks; zero network; C17/no-C++ not
-- applicable to this Lua file but the future C bridge will obey AGENTS.md rules.

local dlg = nil
local w_engine = nil
local w_model = nil
local w_language = nil
local w_threads = nil

-- In-memory spike state. Apply stores here; real GUI would persist via
-- config_Put* on the vlc-whisper plugin vars (see docs/plans/spike_lua_extension.md).
local spike_state = {
  engine = "auto",
  model = "tiny.en",
  language = "en",
  threads = "4",
}

-- id -> string maps for dropdown get_value() results (Lua 5.1-safe).
local engine_map = { [1] = "auto", [2] = "gpu", [3] = "cpu" }
local model_map = {
  [1] = "tiny.en",
  [2] = "tiny",
  [3] = "base.en",
  [4] = "base",
  [5] = "small",
  [6] = "medium",
  [7] = "large",
}
local language_map = {
  [1] = "auto",
  [2] = "en",
  [3] = "ro",
  [4] = "tr",
  [5] = "de",
  [6] = "fr",
  [7] = "es",
}

local function on_apply()
  -- Read dropdown selections (get_value returns the id passed to add_value).
  local eng_id = w_engine and w_engine:get_value() or 1
  local mod_id = w_model and w_model:get_value() or 1
  local lang_id = w_language and w_language:get_value() or 2
  local thr_text = w_threads and w_threads:get_text() or "4"

  spike_state.engine = engine_map[eng_id] or "auto"
  spike_state.model = model_map[mod_id] or "tiny.en"
  spike_state.language = language_map[lang_id] or "en"
  -- Keep threads as string; real bridge would tonumber + clamp to [1..16].
  -- Documented spinner gap: VLC Lua has no spinbox widget; text_input is used.
  spike_state.threads = thr_text

  -- SPIKE logging — filterable with Tools > Messages verbosity 2.
  vlc.msg.info("[VLC-Whisper][SPIKE] Apply engine=" .. spike_state.engine
    .. " model=" .. spike_state.model
    .. " language=" .. spike_state.language
    .. " threads=" .. spike_state.threads)

  -- Also log the stored table for programmatic verification.
  vlc.msg.info("[VLC-Whisper][SPIKE] state stored local table (no config write in spike)")

  -- Optional user feedback inside the dialog (if label widget exists, update it).
  -- No-op if extension already hides dialog; never blocks.
end

local function build_dialog()
  dlg = vlc.dialog("VLC-Whisper Settings (Spike)")

  -- Row 1: Engine
  dlg:add_label("Engine:", 1, 1, 1, 1)
  w_engine = dlg:add_dropdown(2, 1, 2, 1)
  w_engine:add_value("auto (default)", 1)
  w_engine:add_value("GPU (Vulkan)", 2)
  w_engine:add_value("CPU only", 3)

  -- Row 2: Model
  dlg:add_label("Model:", 1, 2, 1, 1)
  w_model = dlg:add_dropdown(2, 2, 2, 1)
  w_model:add_value("tiny.en (default)", 1)
  w_model:add_value("tiny (multilingual)", 2)
  w_model:add_value("base.en", 3)
  w_model:add_value("base (multilingual)", 4)
  w_model:add_value("small", 5)
  w_model:add_value("medium", 6)
  w_model:add_value("large", 7)

  -- Row 3: Language
  dlg:add_label("Language:", 1, 3, 1, 1)
  w_language = dlg:add_dropdown(2, 3, 2, 1)
  w_language:add_value("auto (detect)", 1)
  w_language:add_value("English (en)", 2)
  w_language:add_value("Romanian (ro)", 3)
  w_language:add_value("Turkish (tr)", 4)
  w_language:add_value("German (de)", 5)
  w_language:add_value("French (fr)", 6)
  w_language:add_value("Spanish (es)", 7)

  -- Row 4: Threads — spinner gap: VLC Lua extension toolkit has no
  -- EXTENSION_WIDGET_SPIN_ICON input / spinbox widget. Text input is the
  -- closest available control; PotPlayer's thread-count spinner is emulated
  -- as a plain text field with default "4". Real GUI outside VLC (standalone
  -- exe) would use a native spinner.
  dlg:add_label("Threads:", 1, 4, 1, 1)
  w_threads = dlg:add_text_input("4", 2, 4, 2, 1)

  -- Row 5: Apply
  dlg:add_button("Apply", on_apply, 1, 5, 4, 1)

  -- Row 6: Hint / spike banner
  dlg:add_label("Spike: Apply logs with [SPIKE] prefix; no config written.", 1, 6, 4, 1)
end

function descriptor()
  return {
    title = "VLC-Whisper Settings (Spike)",
    version = "0.1.0",
    author = "vlc-whisper",
    url = "https://github.com/rzv04/vlc-whisper",
    shortdesc = "VLC-Whisper Settings (Spike)",
    description = "Feasibility spike for VLC-Whisper settings GUI via Lua extension. "
      .. "Provides Engine/Model/Language dropdowns + Threads text input. "
      .. "Apply logs values with [SPIKE] prefix and stores in a local Lua table. "
      .. "No config writes, no worker wiring, no network.",
    capabilities = { "menu" },
  }
end

function activate()
  vlc.msg.info("[VLC-Whisper][SPIKE] extension activate — building dialog")
  if dlg ~= nil then
    -- Recreate if VLC re-activates without a deactivate.
    pcall(function() dlg:hide() end)
    dlg = nil
  end
  w_engine = nil
  w_model = nil
  w_language = nil
  w_threads = nil
  build_dialog()
  dlg:show()
  vlc.msg.info("[VLC-Whisper][SPIKE] dialog shown (Engine/Model/Language dropdowns + Threads default=4)")
  return true
end

function deactivate()
  vlc.msg.info("[VLC-Whisper][SPIKE] extension deactivate")
  if dlg ~= nil then
    pcall(function() dlg:hide() end)
    dlg = nil
  end
  w_engine = nil
  w_model = nil
  w_language = nil
  w_threads = nil
end

function close()
  vlc.msg.info("[VLC-Whisper][SPIKE] extension close (user closed dialog)")
  vlc.deactivate()
end

function menu()
  -- Single menu entry under View > Extensions (or Tools > Extensions on some skins).
  return { "VLC-Whisper Settings (Spike)" }
end

function trigger_menu(id)
  -- id == 1 for the single entry; ensure dialog is visible.
  vlc.msg.info("[VLC-Whisper][SPIKE] trigger_menu id=" .. tostring(id))
  if dlg == nil then
    activate()
  else
    dlg:show()
  end
end
