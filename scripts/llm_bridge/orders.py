"""Turning decided orders into submit_action calls, shared by bridge.py's autonomous loop and mcp_session.py's per-call
step (MCP tools; see mcp_server.py). Kept out of both so neither has to duplicate it."""


def summarize(order):
    """A short label for an order, for the model's own decision log (memory.py)."""
    bits = [order.get("verb", "?")]
    for k in ("kind", "power"):
        if order.get(k):
            bits.append(str(order[k]))
    if order.get("slab_rect"):
        bits.append("slabs%s" % (order["slab_rect"],))
    if order.get("pos"):
        bits.append("at%s" % (order["pos"],))
    if order.get("name"):
        bits.append(order["name"])
    elif order.get("thing_id") is not None:
        bits.append("#%s" % order["thing_id"])
    if order.get("enabled") is not None:
        bits.append("on" if order["enabled"] else "off")
    return " ".join(bits)


def submit_batch(api, seat, orders, decision_turn, max_age_turns, dry_run=False, memory=None):
    """Submits each order (queued, expiring: prompt.order_to_request) in the order given. With dry_run=True, each is
    validated (the same checks and error codes a real submit would give) but never queued or spent -- see
    external_seat.c's extseat_check_verb. `memory` (if given) resolves an order's `name` to the creature's thing_id.
    Returns (sent, refused); a dry-run "sent" entry carries would_succeed and steps rather than an id, since nothing
    was actually queued."""
    import prompt   # local import: avoids a hard dependency for callers that only want summarize()

    sent, refused = [], []
    for order in orders:
        req = prompt.order_to_request(order, seat, decision_turn, max_age_turns, memory=memory)
        if dry_run:
            req["dry_run"] = True
        r = api.call(**req)
        if r.get("success"):
            entry = {"verb": order.get("verb"), "summary": summarize(order)}
            if dry_run:
                entry.update({"would_succeed": True, "steps": r["data"].get("steps")})
            else:
                entry.update({"id": r["data"].get("id"), "behind": r["data"].get("queued_behind")})
            sent.append(entry)
        else:
            refused.append({"verb": order.get("verb"), "error": r.get("error"), "summary": summarize(order)})
    return sent, refused
