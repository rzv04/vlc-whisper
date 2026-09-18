-- VLC-Whisper Settings launcher for VLC 3.0.x Lua 5.1.
-- Lua opens one local VLC access URI and reads its one-byte launch result. The
-- native bridge invokes the Qt --launch-detached bootstrap without a shell.

local error_dlg = nil

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

local function show_launch_error()
  log_error("[VLC-Whisper] standalone settings process could not be started")
  error_dlg = vlc.dialog("VLC-Whisper Settings Error")
  error_dlg:add_label(
    "The VLC-Whisper settings application could not be summoned. Try reinstalling VLC-Whisper.", 1, 1, 3, 1)
  error_dlg:add_button("Close", function() pcall(function() vlc.deactivate() end) end, 2, 2, 1, 1)
  error_dlg:show()
end

local function launch_settings()
  local ok, stream = pcall(function() return vlc.stream("vlc-whisper-settings://launch") end)
  if not ok or stream == nil then return false end
  local read_ok, result = pcall(function() return stream:read(2) end)
  return read_ok and result ~= nil and result:sub(1, 1) == "1"
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
