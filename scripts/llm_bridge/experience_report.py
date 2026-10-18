#!/usr/bin/env python3
"""Shows and curates what the agent has learnt across games: the experience store (experience.py; docs/refactor/AI/
omissions/09-persistent-memory.md section 6.7). The agent writes its lessons itself when a game ends; this is how a
person reads them, corrects them, or undoes a bad rewrite.

    python3 scripts/llm_bridge/experience_report.py games                    # every finished game, newest first
    python3 scripts/llm_bridge/experience_report.py games --level keeporig:5
    python3 scripts/llm_bridge/experience_report.py game GAME_ID             # one game's attempts, checkpoints, debriefs
    python3 scripts/llm_bridge/experience_report.py lessons                  # every scope's lessons, with their ids
    python3 scripts/llm_bridge/experience_report.py lessons general
    python3 scripts/llm_bridge/experience_report.py items general --all      # with topic, counts, status (retired too)
    python3 scripts/llm_bridge/experience_report.py add general "Wall in early." --topic defence
    python3 scripts/llm_bridge/experience_report.py revise 12 "A better wording."
    python3 scripts/llm_bridge/experience_report.py retire 12 "wrong on water maps"
    python3 scripts/llm_bridge/experience_report.py confirm 12
    python3 scripts/llm_bridge/experience_report.py merge 12 14 "Both, said once."
    python3 scripts/llm_bridge/experience_report.py move 12 --scope campaign:keeporig --topic economy
    python3 scripts/llm_bridge/experience_report.py history 12               # one lesson's changes
    python3 scripts/llm_bridge/experience_report.py history general          # every version of a scope's text, with its id
    python3 scripts/llm_bridge/experience_report.py rollback general 12      # make version 12 the current text again
    python3 scripts/llm_bridge/experience_report.py set general notes.txt    # replace a scope's text with a file's ('-': stdin)
    python3 scripts/llm_bridge/experience_report.py forget-lessons level:keeporig:5
    python3 scripts/llm_bridge/experience_report.py forget-game GAME_ID

--db picks the store (default: the one the bridge uses, experience.sqlite in its own folder). A lesson is never deleted:
a revised, merged or retired one is kept, marked, and every change is in its history; a scope's text after each change
is kept as a version too, so a set, rollback or forget can itself be undone with rollback.
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
        items = store.items(scope)
        out.write("[%s]\n%s\n\n" % (scope, "\n".join(l for i in items for l in experience.item_lines(i, "")) or "(empty)"))
    return 0


def cmd_items(store, args, out):
    rows = store.items(args.scope, status=None if args.all else "active")
    if not rows:
        out.write("no lessons%s\n" % (" in " + args.scope if args.scope else ""))
    for i in rows:
        extra = ""
        if i["status"] != "active":
            extra = "  [%s%s%s]" % (i["status"], (": " + i["why"]) if i["why"] else "",
                                    (" -> L%d" % i["superseded_by"]) if i["superseded_by"] else "")
        out.write("L%-5d %-22s %-9s +%d/-%d %s %s%s\n" % (i["id"], i["scope"][:22], i["topic"], i["confirmed"], i["contradicted"],
                                                     _when(i["updated_at"]), i["text"].replace("\n", " / "), extra))
    return 0


def _report(ok, warn, what, out):
    if warn:
        out.write(warn + "\n")
    if ok is None or ok is False:
        out.write("nothing changed\n")
        return 1
    out.write(what + "\n")
    return 0


def cmd_add(store, args, out):
    item_id, warn = store.add_item(args.scope, args.text, args.topic, source="report")
    return _report(item_id, warn, "added L%s" % item_id, out)


def cmd_revise(store, args, out):
    new_id, warn = store.revise_item(args.id, args.text, source="report")
    return _report(new_id, warn, "L%d revised as L%s" % (args.id, new_id), out)


def cmd_retire(store, args, out):
    return _report(store.retire_item(args.id, args.why or "retired by hand"), "", "L%d retired" % args.id, out)


def cmd_confirm(store, args, out):
    return _report(store.confirm_item(args.id), "", "L%d confirmed" % args.id, out)


def cmd_merge(store, args, out):
    new_id, warn = store.merge_items(args.ids, args.text)
    return _report(new_id, warn, "merged %s into L%s" % (", ".join("L%d" % i for i in args.ids), new_id), out)


def cmd_move(store, args, out):
    if args.topic and args.topic not in experience.TOPICS:
        out.write("topic is one of %s\n" % ", ".join(experience.TOPICS))
        return 1
    return _report(store.move_item(args.id, args.scope, args.topic), "", "L%d moved" % args.id, out)


def cmd_history(store, args, out):
    if args.scope.upper().lstrip("L").isdigit():
        item_id = int(args.scope.upper().lstrip("L"))
        item = store.item(item_id)
        if item is None:
            out.write("no lesson L%d\n" % item_id)
            return 1
        out.write("L%d [%s, %s, %s] %s\n" % (item_id, item["scope"], item["topic"], item["status"], item["text"]))
        for e in store.item_events(item_id):
            out.write("  %s %s%s%s\n" % (_when(e["at"]), e["event"], (" (" + e["note"] + ")") if e["note"] else "",
                                         (", game " + e["game_id"][:8]) if e["game_id"] else ""))
        return 0
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
    i = sub.add_parser("items")
    i.add_argument("scope", nargs="?")
    i.add_argument("--all", action="store_true", help="retired lessons too")
    a = sub.add_parser("add")
    a.add_argument("scope")
    a.add_argument("text")
    a.add_argument("--topic", default=None)
    v = sub.add_parser("revise")
    v.add_argument("id", type=int)
    v.add_argument("text")
    t = sub.add_parser("retire")
    t.add_argument("id", type=int)
    t.add_argument("why", nargs="?")
    sub.add_parser("confirm").add_argument("id", type=int)
    m = sub.add_parser("merge")
    m.add_argument("ids", type=int, nargs="+")
    m.add_argument("text")
    mv = sub.add_parser("move")
    mv.add_argument("id", type=int)
    mv.add_argument("--scope", default=None)
    mv.add_argument("--topic", default=None)
    sub.add_parser("history").add_argument("scope", help="a scope (its text's versions) or a lesson id (L12 or 12: its changes)")
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
        return {"games": cmd_games, "game": cmd_game, "lessons": cmd_lessons, "items": cmd_items, "add": cmd_add,
                "revise": cmd_revise, "retire": cmd_retire, "confirm": cmd_confirm, "merge": cmd_merge, "move": cmd_move,
                "history": cmd_history, "rollback": cmd_rollback,
                "set": cmd_set, "forget-lessons": cmd_forget_lessons, "forget-game": cmd_forget_game}[args.cmd](store, args, out)
    finally:
        store.close()


if __name__ == "__main__":
    sys.exit(main())
