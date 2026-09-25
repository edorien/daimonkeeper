#ifndef KFXMAIN_H
#define KFXMAIN_H

#include <stdint.h>
// Declares kfxmain() -- defined in main.cpp, the real composition-root entry
// point -- for src/native_entry.cpp, the only other file that calls it. Used
// to live in kfx_platform's platform.h (declared there, called from
// PlatformLinux.cpp/PlatformWindows.cpp's main()/WinMain()), the one
// documented case of a lower-ranked library calling up into app_entry -- see
// docs/refactor/todo/remove-kfxmain-symbol-residual.md. Moved here now that
// both the caller (native_entry.cpp) and the definition (main.cpp) are
// app_entry files.

#ifdef __cplusplus
extern "C" {
#endif

int64_t kfxmain(int64_t argc, char *argv[]);

#ifdef __cplusplus
}
#endif

#endif // KFXMAIN_H
