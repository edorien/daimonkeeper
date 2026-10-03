#!/bin/sh
# Adds dAImon Keeper to the desktop's application menu, for the current user.
#
#   ./install-desktop-entry.sh              install (or refresh after moving the folder)
#   ./install-desktop-entry.sh --uninstall  remove the menu entry and icons
#
# Run it from the game folder (next to the daimonkeeper binary). The game reads its data
# from the working directory, so the entry records this folder's absolute path; run the
# script again if you move the folder. Writes only under $XDG_DATA_HOME (~/.local/share).
set -eu

APP=daimonkeeper
DIR=$(cd "$(dirname "$0")" && pwd)
DATA=${XDG_DATA_HOME:-$HOME/.local/share}
ENTRY="$DATA/applications/$APP.desktop"
ICONS="$DATA/icons/hicolor"
SIZES="16 24 32 48 64 128 256"

refresh() {
    command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q "$DATA/applications" || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$ICONS" 2>/dev/null || true
}

if [ "${1:-}" = "--uninstall" ]; then
    rm -f "$ENTRY"
    for s in $SIZES; do rm -f "$ICONS/${s}x${s}/apps/$APP.png"; done
    refresh
    echo "Removed $ENTRY and its icons."
    exit 0
fi

if [ ! -x "$DIR/$APP" ]; then
    echo "No $APP executable next to this script (in $DIR)." >&2
    exit 1
fi

for s in $SIZES; do
    src="$DIR/icons/${APP}_icon$(printf '%03d' "$s").png"
    [ -f "$src" ] || continue
    mkdir -p "$ICONS/${s}x${s}/apps"
    cp -f "$src" "$ICONS/${s}x${s}/apps/$APP.png"
done

# Exec is quoted per the Desktop Entry spec: \, ", ` and $ escaped inside the quotes.
exec_path=$(printf '%s' "$DIR/$APP" | sed 's/[\\"`$]/\\\\&/g')

mkdir -p "$DATA/applications"
cat > "$ENTRY" <<EOF
[Desktop Entry]
Type=Application
Version=1.5
Name=dAImon Keeper
GenericName=Dungeon Management Game
Comment=Open-source Dungeon Keeper engine, based on KeeperFX (needs the original game data)
Exec="$exec_path"
Path=$DIR
Icon=$APP
Terminal=false
Categories=Game;StrategyGame;
Keywords=dungeon;keeper;strategy;keeperfx;
StartupWMClass=$APP
EOF
chmod 644 "$ENTRY"
refresh
echo "Installed $ENTRY"
