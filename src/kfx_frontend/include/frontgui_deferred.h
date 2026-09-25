#ifndef FRONTGUI_DEFERRED_H
#define FRONTGUI_DEFERRED_H

#include <stdint.h>
// A frame-boundary action queue. ImGui click handlers that must not run
// heavy state transitions (turn_on/off_menu, load_game, recursive packet
// sends) from inside an open window enqueue an action here; the frame
// owner drains it once at the very top of its next frame, before any
// window opens. docs/refactor/ingame-gui/10-maintainability-refactors.md §3.
//
// One instance per frame owner -- drain timing differs, so not a global.
// C++ only (used from the .cpp frame modules).

#ifdef __cplusplus

struct FeDeferredQueue {
    static const int64_t CAP = 8;
    void (*fns[CAP])(void) = {};
    int64_t count = 0;

    // Enqueue. Ignores nullptr and silently drops past CAP (a HUD frame
    // never legitimately queues that many actions).
    void push(void (*fn)(void))
    {
        if (fn != nullptr && count < CAP)
            fns[count++] = fn;
    }

    // Run everything queued, in order, then clear. Snapshots count first so
    // an action that itself pushes is picked up next frame, not this one.
    void drain(void)
    {
        const int64_t n = count;
        count = 0;
        for (int64_t i = 0; i < n; i++)
            fns[i]();
    }
};

#endif // __cplusplus
#endif // FRONTGUI_DEFERRED_H
