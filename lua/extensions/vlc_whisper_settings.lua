-- VLC-Whisper Settings launcher for VLC 3.0.x Lua 5.1.
-- The extension does bounded local discovery + detached spawn only. It never polls,
-- waits for the child, performs HTTP, hashes models, or owns the settings form.

local error_dlg = nil

local function env_get(name)
  local ok, value = pcall(function()
    if os and os.getenv then return os.getenv(name) end
    return nil
  end)
  return ok and value or nil
end

local function config_dir(name)
  local ok, value = pcall(function()
    if vlc and vlc.config and vlc.config[name] then return vlc.config[name]() end
    if config and config[name] then return config[name]() end
    return nil
  end)
  return ok and value or nil
end

local function join_path(base, suffix)
  if base == nil or base == "" then return nil end
  if base:sub(-1) == "/" or base:sub(-1) == "\\" then return base .. suffix end
  return base .. "/" .. suffix
end

local function file_exists(path)
  if path == nil or path == "" then return false end
  local ok, file = pcall(function()
    if vlc and vlc.io and vlc.io.open then return vlc.io.open(path, "rb") end
    return nil
  end)
  if not ok or file == nil then return false end
  pcall(function() file:close() end)
  return true
end

local function logging_enabled()
  local ok, value = pcall(function()
    if vlc and vlc.config and vlc.config.get then return vlc.config.get("whisper-logging") end
    if config and config.get then return config.get("whisper-logging") end
    return false
  end)
  return ok and (value == true or value == 1 or value == "1" or value == "true")
end

local function log_error(message)
  if logging_enabled() then pcall(function() vlc.msg.err(message) end) end
end

local function resolve_settings_executable()
  local is_windows = env_get("OS") == "Windows_NT"
  if is_windows then
    local datadir = config_dir("datadir")
    local candidate = join_path(datadir, "vlc-whisper-settings/vlc-whisper-settings.exe")
    if file_exists(candidate) then return candidate, true end
    local program_files = env_get("ProgramFiles")
    candidate = join_path(program_files, "VideoLAN/VLC/vlc-whisper-settings/vlc-whisper-settings.exe")
    if file_exists(candidate) then return candidate, true end
    return nil, true
  end
  local candidate = "/usr/bin/vlc-whisper-settings"
  if file_exists(candidate) then return candidate, false end
  return nil, false
end

local function show_launch_error()
  log_error("[VLC-Whisper] standalone settings process could not be started")
  error_dlg = vlc.dialog("VLC-Whisper Settings Error")
  error_dlg:add_label(
    "The VLC-Whisper settings application could not be summoned. Try reinstalling VLC-Whisper.", 1, 1, 3, 1)
  error_dlg:add_button("Close", function() pcall(function() vlc.deactivate() end) end, 2, 2, 1, 1)
  error_dlg:show()
end

local function launch_settings()
  local path, is_windows = resolve_settings_executable()
  if path == nil then return false end
  if path:find('"', 1, true) then return false end

  local command = nil
  if is_windows then
    command = 'start "" "' .. path .. '"'
  else
    command = '"' .. path .. '" >/dev/null 2>&1 &'
  end

  local ok, result = pcall(function() return os.execute(command) end)
  if not ok then return false end
  return result == true or result == 0
end

function descriptor()
  return {
    title = "VLC-Whisper Settings",
    version = "0.2.0",
    author = "vlc-whisper",
    url = "https://github.com/rzv04/vlc-whisper",
    shortdesc = "VLC-Whisper Settings",
    description = "Launches the standalone VLC-Whisper Settings application.",
    capabilities = { "menu" },
  }
end

function activate()
  if launch_settings() then
    pcall(function() vlc.deactivate() end)
  else
    show_launch_error()
  end
  return true
end

function deactivate()
  if error_dlg ~= nil then
    pcall(function() error_dlg:hide() end)
    error_dlg = nil
  end
end

function close()
  pcall(function() vlc.deactivate() end)
end

function menu()
  return { "Open VLC-Whisper Settings" }
end

function trigger_menu(_id)
  if not launch_settings() then show_launch_error() end
end

function meta_changed()
end

function input_changed()
end

function playing_changed()
end
