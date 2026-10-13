#!/usr/bin/env python3
"""Seed corpora for the fuzz targets (security pass S08, fuzz/README.md).

    scripts/fuzz_seeds.py [out-dir]       default out/scratch/fuzz/corpus; one folder per target

The repository's own config/ files and the content of core_files/ (the original game's data and the community
campaigns kept there; git-ignored) are the seeds. Targets whose first input byte selects a file (config_file,
map_file) write theirs themselves: kfx_fuzz <target> seeds <dir>; this script runs that too if the binary is built.
"""
import os, shutil, subprocess, sys, glob

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "out/scratch/fuzz/corpus")
fuzz_bin = os.path.join(root, "out/fuzz/fuzz/kfx_fuzz")

def put(target, src, tag, limit=60000, prefix=b""):
    d = os.path.join(out, target)
    os.makedirs(d, exist_ok=True)
    try:
        data = open(src, "rb").read(limit)
    except OSError:
        return 0
    name = "%s-%s" % (tag, os.path.basename(src).replace(" ", "_"))
    open(os.path.join(d, name), "wb").write(prefix + data)
    return 1

counts = {}
def add(target, src, tag, **kw):
    counts[target] = counts.get(target, 0) + put(target, src, tag, **kw)

core = os.path.join(root, "core_files")
# campaign files
for f in glob.glob(os.path.join(core, "campgns", "*.cfg")) + glob.glob(os.path.join(root, "config", "campgns", "*.cfg")):
    add("campaign_file", f, "camp")
# level scripts
scripts = glob.glob(os.path.join(core, "campgns", "*", "map*.txt")) + glob.glob(os.path.join(core, "levels", "*", "map*.txt")) \
        + glob.glob(os.path.join(core, "campgns", "*", "MAP*.TXT"))
for i, f in enumerate(sorted(scripts)[:400]):
    add("level_script", f, "s%03d" % i, limit=16000)
# creature files
for i, f in enumerate(sorted(glob.glob(os.path.join(root, "config", "creatrs", "*.cfg")))):
    add("creature_file", f, "c%02d" % i, prefix=bytes([i % 40 + 1]))
# language strings
for f in glob.glob(os.path.join(core, "**", "*.dat"), recursive=True):
    if os.path.basename(f).lower().startswith(("gtext", "text", "strings")) or ".eng." in f.lower():
        add("strings_file", f, "str")
for f in glob.glob(os.path.join(root, "config", "fxdata", "gtext*.dat")):
    add("strings_file", f, "gtext")
# RNC: every packed file of the original data, plus what is not packed
for f in glob.glob(os.path.join(core, "data", "*.dat"))[:60] + glob.glob(os.path.join(core, "data", "*.pal"))[:20]:
    add("rnc_file", f, "d", limit=8000)

# sound files and sprite packs: small ones only (a zip cannot be cut short and still be a zip)
def small(files, limit):
    return [f for f in sorted(files) if os.path.getsize(f) <= limit]
audio = []
for ext in ("wav", "ogg", "mp3", "flac"):
    audio += glob.glob(os.path.join(core, "**", "*." + ext), recursive=True) + glob.glob(os.path.join(root, "config", "**", "*." + ext), recursive=True)
for i, f in enumerate(small(audio, 60000)[:80]):
    add("audio_buffer", f, "a%02d" % i, limit=64000)
zips = glob.glob(os.path.join(core, "**", "*.zip"), recursive=True) + glob.glob(os.path.join(root, "config", "**", "*.zip"), recursive=True)
for i, f in enumerate(small(zips, 64000)[:80]):
    add("sprite_zip", f, "z%02d" % i, limit=65000)

# the targets that write their own
if os.path.exists(fuzz_bin):
    for t in ("config_file", "map_file", "lua_serialised", "api_json", "packet", "replay_file", "net_message"):
        d = os.path.join(out, t)
        os.makedirs(d, exist_ok=True)
        subprocess.run([fuzz_bin, t, "seeds", d], check=False)
else:
    print("(build kfx_fuzz for the config_file and map_file seeds: scripts/build-fuzz.sh fuzz)")
for t, n in sorted(counts.items()):
    print("%-16s %d seeds" % (t, n))
