"""A policy that asks a Claude model (Anthropic Messages API, plain urllib) what to do each beat.

Needs ANTHROPIC_API_KEY. ANTHROPIC_BASE_URL (default https://api.anthropic.com) exists so tests can point it at a local mock;
ANTHROPIC_MODEL picks the model. The model may call `look` any number of times (up to max_rounds) and ends by calling
`submit_orders`. Latency and token usage of every decision are recorded in ctx["metrics"].
"""
import json
import os
import time
import urllib.error
import urllib.request

import prompt


class AnthropicPolicy:
    name = "anthropic"

    def __init__(self, model=None, base_url=None, api_key=None, max_rounds=4, max_tokens=1500, timeout=120):
        self.model = model or os.environ.get("ANTHROPIC_MODEL", "claude-sonnet-5")
        self.base_url = (base_url or os.environ.get("ANTHROPIC_BASE_URL", "https://api.anthropic.com")).rstrip("/")
        self.api_key = api_key or os.environ.get("ANTHROPIC_API_KEY", "")
        self.max_rounds, self.max_tokens, self.timeout = max_rounds, max_tokens, timeout
        self.first = True
        self.history = []   # earlier decisions as (state text, orders text), kept short: the state text carries the news

    def _post(self, body):
        req = urllib.request.Request(
            self.base_url + "/v1/messages", data=json.dumps(body).encode(),
            headers={"content-type": "application/json", "x-api-key": self.api_key, "anthropic-version": "2023-06-01"})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                return json.loads(r.read())
        except urllib.error.HTTPError as e:
            raise RuntimeError("Anthropic API error %d: %s" % (e.code, e.read().decode(errors="replace")[:500]))

    def decide(self, state, ctx):
        text = prompt.render_state(state, first=self.first)
        self.first = False
        recent = "\n".join("Earlier decision: %s" % h for h in self.history[-3:])
        messages = [{"role": "user", "content": (recent + "\n\n" if recent else "") + text}]
        metrics = ctx.setdefault("metrics", {"calls": 0, "input_tokens": 0, "output_tokens": 0, "seconds": 0.0})
        for _ in range(self.max_rounds):
            t0 = time.time()
            resp = self._post({"model": self.model, "max_tokens": self.max_tokens, "system": prompt.SYSTEM_PROMPT,
                               "tools": prompt.TOOLS, "messages": messages})
            metrics["seconds"] += time.time() - t0
            metrics["calls"] += 1
            usage = resp.get("usage", {})
            metrics["input_tokens"] += usage.get("input_tokens", 0)
            metrics["output_tokens"] += usage.get("output_tokens", 0)
            blocks = resp.get("content", [])
            uses = [b for b in blocks if b.get("type") == "tool_use"]
            submit = next((b for b in uses if b["name"] == "submit_orders"), None)
            if submit is not None:
                inp = submit["input"]
                self.history.append("%s -> %d orders" % (inp.get("reasoning", ""), len(inp.get("orders", []))))
                return {"reasoning": inp.get("reasoning", ""), "orders": inp.get("orders", [])}
            if not uses:   # the model answered in words only: treat as "wait"
                return {"reasoning": "no tool call: " + " ".join(b.get("text", "") for b in blocks)[:200], "orders": []}
            messages.append({"role": "assistant", "content": blocks})
            results = []
            for b in uses:
                if b["name"] == "look":
                    try:
                        out = prompt.render_window(state.view, b["input"]["slab_rect"])
                    except (KeyError, IndexError, TypeError, ValueError) as e:
                        out = "bad rectangle: %r" % (e,)
                else:
                    out = "unknown tool"
                results.append({"type": "tool_result", "tool_use_id": b["id"], "content": out})
            messages.append({"role": "user", "content": results})
        return {"reasoning": "gave up after %d rounds" % self.max_rounds, "orders": []}
