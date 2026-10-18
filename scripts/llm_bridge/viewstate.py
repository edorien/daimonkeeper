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


NEWS_CAP = 400  # events and map runs kept in the news between two decisions (a scripted loop may never render one)


def _merge_news(news, diff):
    """Folds one diff reply into the news since the last decision's text, keeping only what the text reads from it: new
    events, map changes and reveals (all of them, in order), and the latest value of anything else (players, rules)."""
    for k, v in diff.items():
        if k in _META:
            continue
        if k == "map" and isinstance(v, dict):
            m = news.setdefault("map", {})
            for part in ("changes", "revealed"):
                if v.get(part):
                    lst = m.setdefault(part, [])
                    lst.extend(copy.deepcopy(v[part]))
                    del lst[:-NEWS_CAP]
        elif k == "own" and isinstance(v, dict):
            added = (v.get("events") or {}).get("added") or []
            if added:
                lst = news.setdefault("own", {}).setdefault("events", {}).setdefault("added", [])
                lst.extend(copy.deepcopy(e) for e in added if e not in lst)
                del lst[:-NEWS_CAP]
        elif k == "rules" and isinstance(v, dict):
            news.setdefault("rules", {}).update(copy.deepcopy(v))
        else:
            news[k] = copy.deepcopy(v)


class ViewState:
    """The latest view of one seat, kept current with diffs, plus what changed since the agent was last shown it."""

    def __init__(self):
        self.view = None
        self.view_id = None
        # What changed since the last decision's text (mark_rendered): every diff since, merged (_merge_news), so a look
        # or status in between, or the view taken while waiting, does not hide what happened while the model was
        # thinking. None while the agent has not yet been shown the latest full view (it then gets every event).
        self.last_diff = None
        self.raw_diff = None        # the raw diff reply of the latest update (None after a full view)
        self._full_unrendered = True
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
            self.raw_diff = reply
            if not self._full_unrendered:
                if self.last_diff is None:
                    self.last_diff = {}
                _merge_news(self.last_diff, reply)
            apply_diff(self.view, reply)
        else:
            if self.view is not None:
                self.resyncs += 1
            self.full_bytes += size
            self.raw_diff = None
            self.last_diff = None
            self._full_unrendered = True
            self.view = {k: v for k, v in reply.items() if k not in _META}
        self.view_id = reply["view_id"]
        self.view["view_id"] = self.view_id
        return self.view

    def mark_rendered(self):
        """The agent has been shown the view as it is now: news starts again from here."""
        self.last_diff = {}
        self._full_unrendered = False
