-- function_source.lua
-- How a function is kept in a saved game (security pass S03). Internal; not for API users.
--
-- A save used to hold a function as its compiled bytecode (string.dump). Bytecode from a file is not checked by the
-- Lua VM, so a save (or a multiplayer host's resync data) could carry code that corrupts memory, and any function it
-- carried would run. A function is now kept as a *reference* that can only point at code the player already has:
--
--   "global"  the name of a global function. Loading looks the name up again.
--   "source"  the exact text of an anonymous function ("function(a, b) ... end"), found in a script the game loaded.
--             Loading accepts the text only if a script that is loaded *now* contains that very text as a function,
--             and compiles it from the text (never from bytecode). A save can pick among the functions of the
--             installed scripts, but cannot bring new code.
--
-- The scripts' text comes from the game's loader (lua_sandbox.c keeps the source of every file it loads), through
-- two functions it gives the game's own scripts and then takes away from everyone else.

local debug = debug                       -- captured here: removed from the globals before untrusted scripts run
local getinfo = debug.getinfo
local chunk_text = __kfx_chunk_text       -- chunk name -> the script's text (or nil)
local chunk_names = __kfx_chunk_names     -- the names of the scripts whose text is kept
local compile = load                      -- the full load(): used with mode "t" only
local byte, sub, find, match, gsub = string.byte, string.sub, string.find, string.match, string.gsub

local M = {}

----------------------------------------------------------------------------------------------------------------------
-- Finding the functions in a script's text.
----------------------------------------------------------------------------------------------------------------------

local function count_newlines(text, from, to)
    local n = 0
    local at = from
    while true do
        local nl = find(text, "\n", at, true)
        if (nl == nil) or (nl > to) then
            return n
        end
        n = n + 1
        at = nl + 1
    end
end

-- Position just after the long bracket (comment or string) that starts at `pos`, or nil if there is none.
local function long_bracket_end(text, pos)
    local eq = match(text, "^%[(=*)%[", pos)
    if eq == nil then
        return nil
    end
    local _, e = find(text, "]" .. eq .. "]", pos + #eq + 2, true)
    return (e or #text) + 1
end

-- The functions of `text`, in the order their `end` comes, each as
--   { text, line, line2, last, named, nparams, vararg }
-- `line` is the line of the `function` keyword, `line2` that of the "(" after it (the line LuaJIT reports as the
-- function's first line), `last` that of its `end`. Only the block keywords matter, so this is a lexer, not a parser:
-- it skips comments and strings, and counts `function`, `if` and `do` against `end` (`for` and `while` carry a `do`;
-- `repeat` closes with `until`).
local function scan(text)
    local spans, stack = {}, {}
    local pos, len, line = 1, #text, 1
    while pos <= len do
        local c = byte(text, pos)
        if c == 10 then
            line = line + 1
            pos = pos + 1
        elseif c == 32 or c == 9 or c == 13 then
            pos = pos + 1
        elseif c == 45 and byte(text, pos + 1) == 45 then -- "--"
            local stop = long_bracket_end(text, pos + 2)
            if stop then
                line = line + count_newlines(text, pos, stop - 1)
                pos = stop
            else
                pos = find(text, "\n", pos, true) or (len + 1)
            end
        elseif c == 34 or c == 39 then -- a quoted string
            pos = pos + 1
            while pos <= len do
                local d = byte(text, pos)
                if d == 92 then -- backslash: the next character is part of the string
                    local n = byte(text, pos + 1)
                    if n == 10 then
                        line = line + 1
                    elseif n == 13 and byte(text, pos + 2) == 10 then
                        line = line + 1
                        pos = pos + 1
                    end
                    pos = pos + 2
                elseif d == c then
                    pos = pos + 1
                    break
                else
                    if d == 10 then
                        line = line + 1
                    end
                    pos = pos + 1
                end
            end
        elseif c == 91 then -- "[": a long string, or just a bracket
            local stop = long_bracket_end(text, pos)
            if stop then
                line = line + count_newlines(text, pos, stop - 1)
                pos = stop
            else
                pos = pos + 1
            end
        elseif (c >= 65 and c <= 90) or (c >= 97 and c <= 122) or c == 95 then
            local _, e = find(text, "^[%w_]+", pos)
            local word = sub(text, pos, e)
            if word == "function" then
                stack[#stack + 1] = { fn = true, pos = pos, line = line }
            elseif word == "if" or word == "do" then
                stack[#stack + 1] = { fn = false }
            elseif word == "end" then
                local opener = stack[#stack]
                stack[#stack] = nil
                if opener and opener.fn then
                    local body = sub(text, opener.pos, e)
                    local params = match(body, "^function%s*(%b())")
                    local named = (params == nil)
                    local nparams, vararg = 0, false
                    local line2 = opener.line
                    if params then
                        local inner = sub(params, 2, -2)
                        vararg = find(inner, "...", 1, true) ~= nil
                        inner = gsub(inner, "%.%.%.", "")
                        for _ in string.gmatch(inner, "[%a_][%w_]*") do
                            nparams = nparams + 1
                        end
                        line2 = opener.line + count_newlines(body, 1, (find(body, "(", 1, true) or 1))
                    end
                    spans[#spans + 1] = {
                        text = body, line = opener.line, line2 = line2, last = line,
                        named = named, nparams = nparams, vararg = vararg,
                    }
                end
            end
            pos = e + 1
        elseif c >= 48 and c <= 57 then
            local _, e = find(text, "^[%w_.]+", pos)
            pos = e + 1
        else
            pos = pos + 1
        end
    end
    return spans
end
M._scan = scan -- for the tests

local scanned = {} -- chunk name -> { text = ..., spans = ... }

local function spans_for(name)
    local text = chunk_text(name)
    if text == nil then
        return nil
    end
    local entry = scanned[name]
    if entry == nil or entry.text ~= text then
        entry = { text = text, spans = scan(text) }
        scanned[name] = entry
    end
    return entry.spans
end

----------------------------------------------------------------------------------------------------------------------
-- A function -> its reference.
----------------------------------------------------------------------------------------------------------------------

-- A function made again from saved text knows its text: its own debug info would name the chunk "restored-function".
local restored = setmetatable({}, { __mode = "k" })

-- Lua-defined functions that are the value of a global.
local function global_name_of(f)
    for name, value in pairs(_G) do
        if value == f and type(name) == "string" then
            return name
        end
    end
    return nil
end

--- The reference to keep for a function: "global", name   or   "source", text   (nil, reason if it can't be kept).
function M.describe(f)
    local info = getinfo(f, "Su")
    if info == nil or info.what == "C" then
        return nil, "a built-in function (it is saved by name when it is one the game registered)"
    end
    local name = global_name_of(f)
    if name then
        return "global", name
    end
    if restored[f] then
        return "source", restored[f]
    end
    local spans = spans_for(info.source)
    if spans == nil then
        return nil, "it was not defined in a script file the game loaded (" .. tostring(info.short_src) .. ")"
    end
    local found, ambiguous
    for _, span in ipairs(spans) do
        if (span.line == info.linedefined or span.line2 == info.linedefined) and span.last == info.lastlinedefined
            and span.nparams == info.nparams and span.vararg == info.isvararg then
            if span.named then
                return nil, "it is a named local function (save it as an anonymous function or a global one)"
            end
            if found and found ~= span.text then
                ambiguous = true
            end
            found = found or span.text
        end
    end
    if ambiguous then
        return nil, "another function with the same shape starts on the same line of " .. tostring(info.short_src)
            .. " (put each function on a line of its own)"
    end
    if not found then
        return nil, "its text was not found in " .. tostring(info.short_src)
    end
    return "source", found
end

--- Raises an error saying why `f` can't be kept in a saved game; returns f if it can.
function M.check(f)
    local kind, what = M.describe(f)
    if not kind then
        error("function can't be kept in a saved game: " .. what, 3)
    end
    return f
end

----------------------------------------------------------------------------------------------------------------------
-- A reference -> a function.
----------------------------------------------------------------------------------------------------------------------

local known_texts = nil -- the texts of every anonymous function of the scripts loaded now (built when a load starts)

--- Starts loading a saved game: the functions a save may refer to are those of the scripts loaded at this moment.
function M.begin_restore()
    known_texts = {}
    for _, name in ipairs(chunk_names()) do
        for _, span in ipairs(spans_for(name) or {}) do
            if not span.named then
                known_texts[span.text] = true
            end
        end
    end
end

function M.end_restore()
    known_texts = nil
end

function M.resolve(kind, what)
    if type(what) ~= "string" then
        error("saved function reference is malformed")
    end
    if kind == "global" then
        local f = rawget(_G, what)
        if type(f) ~= "function" or getinfo(f, "S").what == "C" then
            error("saved game refers to the global function '" .. what .. "', which this game does not define")
        end
        return f
    elseif kind == "source" then
        if known_texts == nil then
            M.begin_restore()
        end
        if not known_texts[what] then
            error("saved game holds a function that is not part of the scripts loaded now "
                .. "(was the save made with other scripts?)")
        end
        local chunk, err = compile("return " .. what, "=restored-function", "t", _G)
        if not chunk then
            error("saved function does not compile: " .. tostring(err))
        end
        local f = chunk()
        if type(f) ~= "function" then
            error("saved function text is not a function")
        end
        restored[f] = what
        return f
    end
    error("saved game holds a function of a kind this game does not know ('" .. tostring(kind) .. "')")
end

return M
