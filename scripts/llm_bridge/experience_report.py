#!/usr/bin/env python3
"""Shows and curates what the agent has learnt across games: the experience store (experience.py; docs/refactor/AI/
omissions/09-persistent-memory.md section 6.7). The agent writes its lessons itself when a game ends; this is how a
person reads them, corrects them, or undoes a bad rewrite.

    python3 scripts/llm_bridge/experience_report.py games                    # every finished game, newest first
    python3 scripts/llm_bridge/experience_report.py games --level keeporig:5
    python3 scripts/llm_bridge/experience_report.py game GAME_ID             # one game's attempts, checkpoints, debriefs
    python3 scripts/llm_bridge/experience_report.py lessons                  # every set of lessons
    python3 scripts/llm_bridge/experience_report.py lessons general
    python3 scripts/llm_bridge/experience_report.py history general          # every version, with its id
    python3 scripts/llm_bridge/experience_report.py rollback general 12      # make version 12 the current one again
    python3 scripts/llm_bridge/experience_report.py set general notes.txt    # replace a set with a file's text ('-': stdin)
    python3 scripts/llm_bridge/experience_report.py forget-lessons level:keeporig:5
    python3 scripts/llm_bridge/experience_report.py forget-game GAME_ID

--db picks the store (default: the one the bridge uses, experience.sqlite in its own folder). Every change to lessons
is kept as a new version, so a rollback or a forget can itself be undone with rollback.
"""
import argparse
import datetime
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import experience  # noqa: E402


def _when(ts):
    return datetime.datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M") if ts else "-"


def cmd_games(store, args, out):
    rows = store.games(args.level, limit=args.limit)
    if not rows:
        out.write("no finished games%s\n" % (" on " + args.level if args.level else ""))
        return 0
    for r in rows:
        out.write("%s  %-9s %-22s turn %6s  %3s decisions  %s  game %s/%s%s\n" % (
            _when(r["ended_at"]), r["result"], (r["level_key"] or "?")[:22], r["end_turn"], r["decisions"], r["agent"] or "-",
            r["game_id"][:8], r["branch"], ("\n    " + r["summary"]) if r["summary"] else ""))
    return 0


def cmd_game(store, args, out):
    rows = [dict(r) for r in store.db.execute("SELECT * FROM games WHERE game_id LIKE ? ORDER BY started_at", (args.game_id + "%",))]
    if not rows:
        out.write("no game %s\n" % args.game_id)
        return 1
    for r in rows:
        out.write("attempt %s%s: %s on %s (%s), turns %s-%s, %s decisions, %s sent, %s refused\n" % (
            r["branch"], (" (from %s)" % r["parent_branch"]) if r["parent_branch"] else "", r["result"] or "unfinished",
            r["level_key"], r["level_name"] or "", r["start_turn"], r["end_turn"], r["decisions"], r["orders_sent"], r["orders_refused"]))
        refusals = json.loads(r["refusals_by_error"] or "{}")
        if refusals:
            out.write("  refused: %s\n" % ", ".join("%s x%d" % kv for kv in sorted(refusals.items(), key=lambda kv: -kv[1])))
        for cp in store.checkpoints(r["game_id"], r["branch"]):
            out.write("  turn %s: gold %s, fighters %s (score %s), workers %s, rooms %s\n" % (
                cp.get("turn"), cp.get("gold"), cp.get("fighters"), cp.get("score"), cp.get("workers"), sum((cp.get("rooms") or {}).values())))
        if r["summary"]:
            out.write("  debrief: %s\n" % r["summary"])
    return 0


def cmd_lessons(store, args, out):
    scopes = [args.scope] if args.scope else store.scopes()
    if not scopes:
        out.write("no lessons yet\n")
    for scope in scopes:
        text = store.lessons(scope)
        out.write("[%s]\n%s\n\n" % (scope, text or "(empty)"))
    return 0


def cmd_history(store, args, out):
    versions = store.lessons_history(args.scope)
    if not versions:
        out.write("no versions of %s\n" % args.scope)
    for v in versions:
        out.write("--- version %d, %s%s\n%s\n" % (v["id"], _when(v["updated_at"]), (", game " + v["game_id"][:8]) if v["game_id"] else "",
                                                v["text"] or "(cleared)"))
    return 0


def cmd_rollback(store, args, out):
    store.rollback_lessons(args.scope, args.version)
    out.write("%s is version %d's text again\n" % (args.scope, args.version))
    return 0


def cmd_set(store, args, out):
    text = sys.stdin.read() if args.file == "-" else open(args.file).read()
    warn = store.set_lessons(args.scope, text.strip())
    out.write((warn + "\n") if warn else "%s replaced\n" % args.scope)
    return 0


def cmd_forget_lessons(store, args, out):
    store.set_lessons(args.scope, "")
    out.write("%s cleared (the old text stays in its history)\n" % args.scope)
    return 0


def cmd_forget_game(store, args, out):
    n = store.forget_game(args.game_id)
    out.write("deleted %d attempt(s) of game %s\n" % (n, args.game_id))
    return 0 if n else 1


def main(argv=None, out=sys.stdout):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--db", default=None, help="experience store (default: %s)" % experience.default_path())
    sub = p.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("games")
    g.add_argument("--level", default=None)
    g.add_argument("--limit", type=int, default=50)
    sub.add_parser("game").add_argument("game_id")
    sub.add_parser("lessons").add_argument("scope", nargs="?")
    sub.add_parser("history").add_argument("scope")
    r = sub.add_parser("rollback")
    r.add_argument("scope")
    r.add_argument("version", type=int)
    s = sub.add_parser("set")
    s.add_argument("scope")
    s.add_argument("file")
    sub.add_parser("forget-lessons").add_argument("scope")
    sub.add_parser("forget-game").add_argument("game_id")
    args = p.parse_args(argv)
    path = args.db or experience.default_path()
    if not os.path.exists(path):
        out.write("no experience store at %s (the bridge creates it on its first game)\n" % path)
        return 1
    store = experience.Experience(path)
    try:
        return {"games": cmd_games, "game": cmd_game, "lessons": cmd_lessons, "history": cmd_history, "rollback": cmd_rollback,
                "set": cmd_set, "forget-lessons": cmd_forget_lessons, "forget-game": cmd_forget_game}[args.cmd](store, args, out)
    finally:
        store.close()


if __name__ == "__main__":
    sys.exit(main())
