HD asset packs
==============

Put an HD asset pack here, one folder per pack, then choose it in
Options > Graphics > Asset Pack, where it is listed by the name in its
pack.toml; Original is the game's own art. daimonkeeper.cfg records the folder
(ASSET_PACK=<folder>, or NONE).

An asset pack folder holds:
  pack.toml       name = "..."
  sprites/        HD creatures, objects and effects (an HD sprite pack)
  textures/       HD terrain textures (an HD terrain texture pack)
A pack can have either or both; what it doesn't have stays original.

sprites/ holds:
  pack.toml                    scale = 2 | 3 | 4
  animlist.txt                 the filelists the pack has, one a line
  <folder>/filelist_rotN.txt   a header line, then one PNG a line (FXGraphics' sprites-* layout)
  <folder>/*.png               each frame's whole canvas at scale x the original's, RGBA
                               (alpha below 128 transparent)
  keys/animlist.idx            which original frame each line is for (spritepack.py keys)

textures/ holds:
  pack.toml                 size = 64 | 128 | 256
  filelist_tmapaNNN.txt     one "sheet.png x y w h" line per block (FXGraphics layout)
  packNNN/<sheet>.png       the art, RGB or RGBA (alpha below 128 draws black)
  keys/tmapaNNN.idx         which original 32x32 block each HD block is for

Options > Graphics > Generate Missing HD Art (ASSET_PACK_FILL=ON) makes HD art in
the game, smoothed from the originals, for whatever no pack covers -- everything,
with no pack selected, mods' sprites and terrain included.

Options > Graphics > Asset Pack Memory (ASSET_PACK_MEMORY=AUTO, or 1GB to 32GB)
sets how much memory the HD art may take; Auto is a quarter of the computer's
memory, at least 3 GiB.

HD art only replaces the exact original frame or block it was made from;
anything a pack doesn't cover stays as it was. daimonkeeper.log lists what a
pack covered. Mods' own sprites can be drawn in their true colours with
Options > Graphics > Mod Sprites in True Colour.

Making packs (layout, keys, the spritepack.py and texpack.py tools, rendering
from 3D models): docs/hd_sprite_packs.md and docs/hd_texture_packs.md in the
dAImon Keeper source repository.
