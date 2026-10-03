// Test helper: override entries of a callback table for one scope.
//
//     ScopedPortOverride<GamePort> port(game_port, set_game_port);
//     port->get_event_button_info = [](EventKind) -> const struct EventTypeInfo* { return &info; };
//     ... code under test sees the overridden entry ...
//     // leaving the scope reinstalls whatever table was installed before
//
// The constructor copies the currently installed table and installs the
// copy, so every entry the test doesn't touch keeps its current behaviour.
// Restoring on destruction means a failing CHECK/REQUIRE can't leak the
// override into the next test case (the manual copy / set / set(nullptr)
// pattern does). Include as "kfx_config/tests/scoped_port_override.h";
// any kfx_*/tests binary can, since src/ is on every target's include path.
// See docs/refactor-pass2/stage-01-callback-hygiene.md.
#ifndef KFX_TESTS_SCOPED_PORT_OVERRIDE_H
#define KFX_TESTS_SCOPED_PORT_OVERRIDE_H

template <typename T>
class ScopedPortOverride {
public:
    using Setter = void (*)(const T *);

    ScopedPortOverride(const T *installed, Setter set)
        : set_(set), saved_(installed), table_(*installed)
    {
        set_(&table_);
    }
    ~ScopedPortOverride() { set_(saved_); }

    ScopedPortOverride(const ScopedPortOverride &) = delete;
    ScopedPortOverride &operator=(const ScopedPortOverride &) = delete;

    T *operator->() { return &table_; }
    T &table() { return table_; }

private:
    Setter set_;
    const T *saved_;
    T table_;
};

#endif
