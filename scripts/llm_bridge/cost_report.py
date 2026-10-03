#!/usr/bin/env python3
"""Summarizes bridge.py / agent_vs_agent.py decision logs (--log / --log-dir, one JSON object per line) into a
token/latency/cost report. Works on either log shape: bridge.py's single-seat log and agent_vs_agent.py's one
file per seat share the fields this reads (decision, turn, reasons, think_seconds, sent, refused, model), so both
can be passed together for a combined report.

    python3 scripts/llm_bridge/cost_report.py run.jsonl
    python3 scripts/llm_bridge/cost_report.py --dir out/agent-logs          # every *.jsonl in a directory
    python3 scripts/llm_bridge/cost_report.py run.jsonl --prices prices.json --model claude-sonnet-5

Token counts come straight from the log (real, from the API's own usage reply -- anthropic_policy.py); a dollar
figure is only shown when a price file is given, since model prices change and this tool has no live source for
them -- see model_prices.example.json for the format. Absent that, only token counts are reported: a wrong,
stale cost is worse than none.

`model` in each log line is anthropic_policy.py's ctx["metrics"], which accumulates across a session (bridge.py
keeps one ctx for the whole run; agent_vs_agent.py keeps one per seat) rather than resetting each decision -- so
a file's totals are its LAST line's `model` values, not a sum over lines. think_seconds is per decision and is
summed for a total.
"""
import argparse
import glob
import json
import os
import sys


def load_log(path):
    """One file's decisions (list of dicts) plus its final cumulative `model` metrics ({} if the policy never
    recorded any, e.g. ScriptedPolicy)."""
    decisions = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                decisions.append(json.loads(line))
    metrics = decisions[-1].get("model") or {} if decisions else {}
    return decisions, metrics


def summarize_file(path):
    decisions, metrics = load_log(path)
    if not decisions:
        return None
    think = [d.get("think_seconds", 0.0) for d in decisions]
    turns = [d["turn"] for d in decisions if "turn" in d]
    sent = sum(len(d.get("sent", [])) for d in decisions)
    refused = sum(len(d.get("refused", [])) for d in decisions)
    return {
        "path": path,
        "seat": decisions[0].get("seat"),
        "decisions": len(decisions),
        "turn_span": (min(turns), max(turns)) if turns else None,
        "orders_sent": sent,
        "orders_refused": refused,
        "think_seconds_total": round(sum(think), 3),
        "think_seconds_avg": round(sum(think) / len(think), 3) if think else 0.0,
        "think_seconds_max": round(max(think), 3) if think else 0.0,
        "calls": metrics.get("calls", 0),
        "input_tokens": metrics.get("input_tokens", 0),
        "output_tokens": metrics.get("output_tokens", 0),
        "model_seconds": round(metrics.get("seconds", 0.0), 3),
    }


def load_prices(path):
    """{"<model id>": {"input_per_mtok": X, "output_per_mtok": Y}, ...}, or None if no file was given."""
    if not path:
        return None
    with open(path) as f:
        return json.load(f)


def estimate_cost(summary, prices, model):
    if not prices or model not in prices:
        return None
    p = prices[model]
    return summary["input_tokens"] / 1e6 * p["input_per_mtok"] + summary["output_tokens"] / 1e6 * p["output_per_mtok"]


def render(summaries, prices, model):
    lines = []
    total_decisions = total_sent = total_refused = 0
    total_think = total_input = total_output = 0.0
    total_cost = 0.0
    have_cost = prices is not None and model in (prices or {})
    for s in summaries:
        cost = estimate_cost(s, prices, model)
        seat_tag = (" seat %s" % s["seat"]) if s["seat"] is not None else ""
        span = "turns %d-%d" % s["turn_span"] if s["turn_span"] else "no decisions"
        lines.append("%s%s: %d decisions, %s" % (s["path"], seat_tag, s["decisions"], span))
        lines.append("  orders: %d sent, %d refused" % (s["orders_sent"], s["orders_refused"]))
        lines.append("  think time: total %.1fs, avg %.2fs, max %.2fs" % (s["think_seconds_total"], s["think_seconds_avg"], s["think_seconds_max"]))
        if s["calls"]:
            cost_str = (" (~$%.4f)" % cost) if cost is not None else ""
            lines.append("  model: %d call(s), %d input tok, %d output tok, %.1fs%s" % (s["calls"], s["input_tokens"], s["output_tokens"], s["model_seconds"], cost_str))
        total_decisions += s["decisions"]
        total_sent += s["orders_sent"]
        total_refused += s["orders_refused"]
        total_think += s["think_seconds_total"]
        total_input += s["input_tokens"]
        total_output += s["output_tokens"]
        if cost is not None:
            total_cost += cost
    if len(summaries) > 1:
        lines.append("---")
        lines.append("total: %d decisions across %d file(s), %d sent / %d refused, %.1fs think time" % (
            total_decisions, len(summaries), total_sent, total_refused, total_think))
        if total_input or total_output:
            cost_str = (" (~$%.4f)" % total_cost) if have_cost else ""
            lines.append("total tokens: %d input, %d output%s" % (total_input, total_output, cost_str))
    return "\n".join(lines)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("logs", nargs="*", help="one or more --log JSONL files")
    p.add_argument("--dir", default=None, help="every *.jsonl file in this directory (e.g. agent_vs_agent.py's --log-dir)")
    p.add_argument("--prices", default=None, help="JSON file of per-model $/million-token rates; see model_prices.example.json")
    p.add_argument("--model", default=None, help="which entry in --prices to use for the cost estimate")
    args = p.parse_args(argv)

    paths = list(args.logs)
    if args.dir:
        paths += sorted(glob.glob(os.path.join(args.dir, "*.jsonl")))
    if not paths:
        p.error("no log files given (pass paths or --dir)")

    prices = load_prices(args.prices)
    summaries = [s for s in (summarize_file(path) for path in paths) if s is not None]
    if not summaries:
        print("no decisions found in any given log")
        return 1
    print(render(summaries, prices, args.model))
    return 0


if __name__ == "__main__":
    sys.exit(main())
