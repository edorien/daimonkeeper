dAImon Keeper
-------------

dAImon Keeper is a free, open-source reimplementation of Dungeon Keeper,
derived from KeeperFX (Dungeon Keeper Fan Expansion). It plays the original
game's campaign and the campaigns and map packs made for KeeperFX.

  It is made by fans and not supported by the original developer, nor by
  the KeeperFX team. It contains none of the original Dungeon Keeper data:
  you need your own copy of the game -- the original CD, or the GOG, Steam
  or EA (Origin) version -- to play it.

Installation:

  Unpack the archive into a folder, and put the files the game needs from
  your original Dungeon Keeper into it. Which files, and where to find
  them on each edition, is listed in "files_required_from_original_dk.txt"
  (in the docs of the source repository).
    CD: the files are in the "keeper" folder on the disc.
    GOG: the installation folder (inside the CD image it ships with).
    Origin/EA: the "data" sub-folder.
    Steam: steam/steamapps/common/Dungeon Keeper/.

  You can also unpack it into a folder where KeeperFX is already installed:
  dAImon Keeper keeps its own settings, log and saves, so both keep working
  side by side.

  There is no launcher of dAImon Keeper's own; run the game directly (see
  "Running the game").

Available languages:

  The following languages are fully functional:
    ENG ITA FRE SPA DUT GER POL SWE JPN RUS CZE KOR CHI
  The following languages are partially functional:
    CHT LAT UKR POR
  Note that some campaigns may not support your language.
  In that case the default language will be used.

Running the game:

  Run "daimonkeeper.exe" on Windows, or "daimonkeeper" on Linux.

  On Linux, "./install-desktop-entry.sh" in the game folder adds the game to
  your desktop's application menu (for your user only). Run it again if you
  move the folder; "./install-desktop-entry.sh --uninstall" removes it.

  Settings are in "daimonkeeper.cfg" (most of them are also in the Options
  screen). If the folder has a KeeperFX "keeperfx.cfg" but no
  "daimonkeeper.cfg" yet, the first run copies its settings; KeeperFX's own
  file is never changed. Saves, settings and campaign progress are in
  "save/", replays in "replays/".

  Install dAImon Keeper in its own folder, not into a KeeperFX folder: both
  games use "fxdata/", "save/" and "replays/", and their "fxdata/" differ. To
  share files between the two (the original game's data, campaigns, maps),
  use symlinks (on Windows, "mklink /D").

  The version is shown on the main menu, e.g. "dAImon Keeper 1.0.0 - KFX 1.4":
  the last part says which KeeperFX release's campaigns and maps it plays.

Campaigns and maps made for KeeperFX:

  Maps and campaigns from the KeeperFX workshop generally work. The workshop's
  "Min. game version" says which KeeperFX they need; compare it with the
  "KFX" part of the version.

  A map whose script uses commands this version doesn't know is marked [!]
  in the level and campaign lists -- hover over it to see which. If a level
  uses something this version doesn't support, the game tells you before it
  starts, and you can go back or play anyway.

  Saves, replays and multiplayer are not shared with KeeperFX: the two
  games don't load each other's saves or replays, and don't see each other's
  multiplayer games.

Logging and reporting problems:

  The game writes a log, "daimonkeeper.log", next to the executable. How much
  it writes is Options -> Game -> Logging (LOG_LEVEL in daimonkeeper.cfg):
  NORMAL by default, DEBUG and DEBUGMAX for more detail, OFF for nothing
  except crash reports. The DEBUG levels can be slow and make large logs;
  use them only to capture a problem. Running the game again overwrites the
  log, so copy it first if you want to keep it.

  If the game crashes, look at the first and last lines of the log -- the
  error is often in plain English. Lines starting with "COMPAT:" list what a
  level used that this version doesn't support.

  Report bugs in dAImon Keeper here -- not to the KeeperFX developers:
  https://github.com/edorien/daimonkeeper/issues
  Include what you did, the first and last 20 or so lines of the log (or the
  whole log, compressed), and if it happens in a saved game, that save: it is
  "save/fx1gNNNN.sav", NNNN being its slot in the Load menu.

Command line options and controls:

  dAImon Keeper accepts KeeperFX's command line options and in-game
  commands, described on KeeperFX's wiki:
  https://github.com/dkfans/keeperfx/wiki/Command-Line-Options
  https://github.com/dkfans/keeperfx/wiki/New-Game-Controls-and-Commands

Changes:

  https://github.com/edorien/daimonkeeper/commits

Licences:

  dAImon Keeper is free software under the GNU General Public License,
  version 2 or later (LICENSE.txt). Where it comes from, and what each
  licence covers, is in NOTICE.txt; the licences of the libraries it uses
  are in THIRD_PARTY_NOTICES.txt; the graphics licence is in
  LICENSE-graphics.txt.

Dungeon Keeper is a trademark of Electronic Arts. The original game was made
by Bullfrog Productions. Some data files are copyrighted by Bullfrog
Productions.
