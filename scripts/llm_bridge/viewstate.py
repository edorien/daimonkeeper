"""Keeps the agent's picture of the game: a full get_player_view, then patches from `since` diffs.

The diff format is described in src/kfx_script/include/api_seat_diff.h. apply_diff() is its inverse; the smoke test checks
that patching an old view with a diff gives exactly the current full view.
"""
import copy

_META = ("mode", "base", "view_id", "reason")


def _find(lst, ident):
    for i, el in enumerate(lst):
        if isinstance(el, dict) and el.get("id") == ident:
            return i
    return -1


def _apply_array(cur, patch):
    for r in patch.get("removed", []):
        if isinstance(r, int) and any(isinstance(e, dict) and "id" in e for e in cur):
            i = _find(cur, r)
            if i >= 0:
                del cur[i]
        elif r in cur:
            cur.remove(r)
    for ch in patch.get("changed", []):
        i = _find(cur, ch["id"])
        if i >= 0:
            _apply_dict(cur[i], {k: v for k, v in ch.items() if k != "id"}, ())
    cur.extend(copy.deepcopy(patch.get("added", [])))


def _apply_map_runs(m, d):
    rows = m.get("rows")
    if rows is None:
        return
    for run in d.get("changes", []):
        y, x, cells = run["y"], run["x"], run["cells"]
        rows[y] = rows[y][: 2 * x] + cells + rows[y][2 * x + len(cells):]


def _apply_dict(dst, d, path):
    for k in d.get("_removed", []):
        dst.pop(k, None)
    is_map = path == ("map",)
    if is_map:
        _apply_map_runs(dst, d)
    for k, v in d.items():
        if k == "_removed" or (is_map and k in ("changes", "revealed")):
            continue
        cur = dst.get(k)
        if isinstance(v, dict) and isinstance(cur, dict):
            _apply_dict(cur, v, path + (k,))
        elif isinstance(v, dict) and isinstance(cur, list) and set(v) <= {"added", "removed", "changed"}:
            _apply_array(cur, v)
        else:
            dst[k] = copy.deepcopy(v)


def apply_diff(view, diff):
    """Patch `view` (a full view dict) in place with a diff reply. Returns view."""
    _apply_dict(view, {k: v for k, v in diff.items() if k not in _META}, ())
    return view


class ViewState:
    """The latest view of one seat, kept current with diffs, plus what changed in the last update."""

    def __init__(self):
        self.view = None
        self.view_id = None
        self.last_diff = None       # the raw diff reply of the latest update (None after a full view)
        self.full_bytes = 0
        self.diff_bytes = 0
        self.resyncs = 0
        self.diff_count = 0

    def update(self, api, seat):
        """Fetch the newest view (a diff when possible). Returns the view."""
        req = dict(action="get_player_view", player=seat)
        if self.view_id is not None:
            req["since"] = self.view_id
        before = api.bytes_received
        reply = api.data(**req)
        size = api.bytes_received - before
        if reply.get("mode") == "diff":
            self.diff_bytes += size
            self.diff_count += 1
            self.last_diff = reply
            apply_diff(self.view, reply)
        else:
            if self.view is not None:
                self.resyncs += 1
            self.full_bytes += size
            self.last_diff = None
            self.view = {k: v for k, v in reply.items() if k not in _META}
        self.view_id = reply["view_id"]
        self.view["view_id"] = self.view_id
        return self.view
