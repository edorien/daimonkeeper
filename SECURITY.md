# Security policy

dAImon Keeper plays content that other people made -- campaigns, map packs, mods, saved games, replays -- and talks to
other players over the network. Opening a map or joining a game should never be able to run programs on your computer,
read or change files outside the game's own folders, or corrupt the game's memory. This page says what we treat as a
security problem and how to tell us about one.

## Reporting a vulnerability

Please report it **privately**, not in a public issue:

- On GitHub, open the repository's **Security** tab and choose **Report a vulnerability**
  (<https://github.com/edorien/daimonkeeper/security/advisories/new>). Only the maintainers see it.

Include what you can of: the version (the line at the top of `daimonkeeper.log`, or the title screen), your
operating system, a file or steps that trigger it (a level, a save, a replay, a packet capture, a script), and what you
think the impact is. A crash that reproduces from a file is already a useful report.

This is a volunteer project. We will acknowledge a report as soon as we can, tell you whether we consider it a
vulnerability, and keep you informed while it is fixed. We ask for a reasonable period -- typically up to 90 days, less
if a fix ships sooner -- before you publish details, so that players can update first. We are happy to credit you in the
fix and the release notes unless you prefer otherwise.

If the problem is in code that dAImon Keeper shares with KeeperFX, we may also need to tell the KeeperFX project; we will
agree with you how and when before we do.

## What counts

The game treats everything it reads from outside as **data**. Data may make the game do anything *inside the game*; it
must never do the following. A report of any of these is a vulnerability:

- **Running code outside the game** from a level, campaign, mod, sprite or sound pack, saved game, replay, or another
  player: starting a program, loading a native library, or a script reaching the operating system.
- **Touching files outside the game's folders**: reading or writing a path the content names (absolute paths, `..`,
  drive letters, device names, symlink tricks), or a script making files.
- **Memory corruption** reachable from any of those inputs or from the network: an out-of-bounds read or write, a
  use-after-free, an integer overflow that leads to either. (A plain crash from a malformed file is a bug we want to hear
  about, but it is lower priority.)
- **The local remote API** (`127.0.0.1:5599`, off unless enabled) being usable by a web page in your browser or by
  another user of the machine without the token, if one is set.
- **A multiplayer client or host** being able to corrupt or take over another player's game, impersonate another player,
  or hang their game, beyond what the game's rules already let a player do.
- **Matchmaking and router port forwarding**: a way around the server certificate check, or a port left open on your
  router after the game has stopped.
- **A release** containing a dependency version with a known, fixable vulnerability that the game can reach.

## What does not

- Anything that needs a program already running as you, or administrator access: it already owns your account.
- Cheating or unfairness in multiplayer that does not corrupt memory or break out of the game.
- What a player chooses to do to their own single-player game (cheat mode, the console, the editor).
- Problems in the original game's data files.
- A level that is merely slow or large. (A script that never returns is stopped by the game's own limits, which is
  working as intended.)

## Supported versions

Fixes go into the current release and the `master` branch. Older versions are not patched; update to the current one.
Saved games and replays from before a format hardening are refused rather than loaded -- that is deliberate, and the
message says so.

## How the game defends itself

For people reviewing or contributing; details in `docs/Architecture/architecture.md` (§8a) and the merge checklist in
`docs/Architecture/upstream-merge-workflow.md` (§6b).

- Level and mod Lua runs in a locked-down interpreter: no file, process, native-code or debug access, source text only
  (no bytecode), a fixed set of modules, and an instruction and memory budget per game turn.
- Saved games, replays and a host's resync carry the game's memory as raw blocks; each is validated before it replaces
  anything, and functions in script data are saved as references to code the player already has.
- Every game packet passes one validator before it is acted on.
- Paths named by content are resolved in one place and cannot leave the game's folders.
- Parsers of downloaded content, and the network and API entry points, are fuzzed under AddressSanitizer and
  UndefinedBehaviorSanitizer (`fuzz/`, `scripts/build-fuzz.sh`).
- The game's own code is built with stack protection, fortify, PIE/RELRO (Linux) and ASLR/DEP (Windows)
  (`scripts/check_hardening.py` verifies each binary); dependencies are pinned by hash and checked against published
  advisories (`scripts/check_dep_vulns.py`).
