-- serialization.lua
-- Internal serialization logic for Lua<->C data.
-- Used by the engine to serialize complex Lua state; not intended for API users.
--
-- What a saved game's script data can hold (security pass S03): tables, strings, numbers, booleans, objects of the
-- classes registered below, built-in functions the game registered by name (seal()), and functions as *references*
-- (core/function_source.lua) -- never bytecode. Loading never executes anything but the loaded game's own scripts.

local binser = require 'external.binser'
local function_source = require 'core.function_source'
require 'classes.Pos3d'
local serialization_version = 3
-- Data of these versions can be read: 2 was the version before functions became references; it is read the same way,
-- except that a function kept as bytecode (which it could hold) is refused.
local readable_versions = { [2] = true, [3] = true }

-- Captured here: the debug library is removed from the globals before campaign, level and mod scripts run.
local debug = debug
local registry = debug.getregistry()
local engine_classes = { "Player", "Thing", "Slab", "Camera", "Room", "Map", "Pos3d" }
for _, name in ipairs(engine_classes) do
    binser.registerResource(registry[name], "metatable:" .. name)
end

-- Built-in (C) functions that a saved game may refer to by name: the game's own API, and the harmless parts of the
-- standard library. Everything that loads code, changes how tables or the environment behave, or reaches outside
-- the game is left out, so a save cannot put such a function where a script will call it.
local denied_globals = {
    Game = true, _G = true, package = true, os = true, io = true, debug = true, ffi = true, jit = true,
    load = true, loadstring = true, loadfile = true, dofile = true, require = true, module = true,
    setfenv = true, getfenv = true, setmetatable = true, getmetatable = true, rawset = true,
    newproxy = true, collectgarbage = true, __kfx_chunk_text = true, __kfx_chunk_names = true,
}

local registered_cfunctions = {}
local scanned_tables = {}
local function register_cfunctions(source, prefix, skip)
    if scanned_tables[source] then
        return
    end
    scanned_tables[source] = true
    local names = {}
    for name in pairs(source) do
        if type(name) == "string" and not (skip and skip[name]) then
            names[#names + 1] = name
        end
    end
    table.sort(names)
    for _, name in ipairs(names) do
        local value = rawget(source, name)
        local path = prefix .. name
        if type(value) == "function" and debug.getinfo(value, "S").what == "C" then
            if not registered_cfunctions[value] then
                binser.registerResource(value, path)
                registered_cfunctions[value] = true
            end
        elseif type(value) == "table" then
            register_cfunctions(value, path .. ".")
        end
    end
end

local M = {}
local sealed = false

--- Fixes which built-in functions a saved game may refer to. Called once by the game, after its own scripts have
--- loaded and the library lock-down (lua_sandbox.c) has removed what untrusted scripts must not reach, and before any
--- campaign, level or mod script runs -- so the set is the same when a game is saved and when it is loaded.
function M.seal()
    if sealed then
        return
    end
    sealed = true
    register_cfunctions(_G, "global:", denied_globals)
    for _, name in ipairs(engine_classes) do
        if registry[name] then
            register_cfunctions(registry[name], "registry:" .. name .. ".")
        end
    end
end

binser.setFunctionCodec(
    function(f)
        local kind, what = function_source.describe(f)
        if not kind then
            error("a function in the saved data can't be saved: " .. tostring(what), 0)
        end
        return kind, what
    end,
    function_source.resolve)

function GetSerializedData()
    local ok, result = pcall(function()
        return binser.serialize(Game, serialization_version)
    end)
    print("GetSerializedData ok: " .. tostring(ok))
    if not ok then
        error("binser failed: " .. result)
    end
    return result
end

function SetSerializedData(serialized_data)
    local ok, result = pcall(function()
        function_source.begin_restore()
        local values = binser.deserialize(serialized_data)
        if not readable_versions[values[2]] then
            error("saved script data has version " .. tostring(values[2]) .. ", which this game does not read", 0)
        end
        if type(values[1]) ~= "table" then
            error("saved script data holds no game table", 0)
        end
        return values[1]
    end)
    function_source.end_restore()
    if not ok then
        error("binser load failed: " .. result)
    end
    Game = result
end

return M
