## tinyfiledialogs v3.21.3

Vendored from https://sourceforge.net/projects/tinyfiledialogs/ (Guillaume Vareille).
Single .c/.h, no build system of its own, no GUI-toolkit link-time dependency (shells out to
zenity/kdialog/etc. via `popen` on Linux at runtime; uses native common dialogs on Windows,
linked via `comdlg32`/`shell32`).

Used by kfx_platform (`bflib_filedialogs.h/.cpp`) for the editor's Open/Save As "Browse..."
folder picker (docs/refactor/editor/phase3/03-slice4-file-dialogs.md).

### Security note (from the header's own warning)

> DO NOT USE USER INPUT IN THE DIALOGS

The Linux fallback path invokes system dialog binaries via `popen`-style calls built from the
title/message/path strings passed in. Only pass literal strings we own and paths that have
already been validated/selected by the OS's own picker — never forward arbitrary/untrusted text
into a title, message, or filter argument.

### License

zlib (SPDX: Zlib) -- see the license block at the top of `tinyfiledialogs.h`.
