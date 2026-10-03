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
    if order.get("to_room") is not None:
        bits.append("to room %s" % order["to_room"])
    if order.get("thing_ids"):
        bits.append("things %s" % (order["thing_ids"],))
    if order.get("direction"):
        bits.append("towards %s" % order["direction"])
    if order.get("message"):
        bits.append('"%s"' % order["message"])
    for flag in ("release", "sacrifice"):
        if order.get(flag):
            bits.append(flag)
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
    batch_digs = []   # a dry run queues nothing, so the batch's earlier mark_digs are passed on as already marked
    for order in orders:
        req = prompt.order_to_request(order, seat, decision_turn, max_age_turns, memory=memory)
        if dry_run:
            req["dry_run"] = True
            if batch_digs:
                req["assume_dig_rects"] = list(batch_digs)
        r = api.call(**req)
        if r.get("success"):
            entry = {"verb": order.get("verb"), "summary": summarize(order)}
            if dry_run:
                entry.update({"would_succeed": True, "steps": r["data"].get("steps")})
            else:
                entry.update({"id": r["data"].get("id"), "behind": r["data"].get("queued_behind")})
            if r["data"].get("warning") == "UNREACHABLE":
                slabs = r["data"].get("unreachable_slabs") or []
                entry["warning"] = "UNREACHABLE: %d slab(s) no imp can reach, e.g. %s -- extend the rectangle to touch walkable floor" % (
                    r["data"].get("unreachable_count", len(slabs)), " ".join("(%d,%d)" % (x, y) for x, y in slabs[:8]))
            elif r["data"].get("warning") == "PARTLY_UNBUILDABLE":
                slabs = r["data"].get("unbuildable_slabs") or []
                entry["warning"] = ("PARTLY_UNBUILDABLE: %d slab(s) will not take the room, e.g. %s -- rooms need your claimed floor; "
                                    "a bridge needs water/lava with your land beside it" % (
                                        r["data"].get("unbuildable_count", len(slabs)), " ".join("(%d,%d)" % (x, y) for x, y in slabs[:8])))
            if order.get("verb") == "mark_dig" and order.get("slab_rect"):
                batch_digs.append(list(order["slab_rect"]))
            sent.append(entry)
        else:
            refused.append({"verb": order.get("verb"), "error": r.get("error"), "summary": summarize(order)})
    return sent, refused
