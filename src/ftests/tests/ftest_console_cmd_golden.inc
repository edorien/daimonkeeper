    {"console:", 0xf7a6cb1c7133e454ULL}, // 85 bytes; ret=0 "command is empty"
    {"console:notacommand", 0x38619b785339897fULL}, // 87 bytes; ret=0 "unsupported command"
    {"console:STATS", 0xb8500d3fec12ce34ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-stats", 0x7dbf1daf1e9c04f4ULL}, // 95 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-reveal", 0xac52db17e8689b6dULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-notacommand", 0x8e283c3e34d25158ULL}, // 0 bytes; ret=0
    {"console:-thing.get $C0", 0xac52db17e8689b6dULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-room.get $ROOM", 0xac52db17e8689b6dULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:stats", 0xb8500d3fec12ce34ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:fps", 0x2e2fc357c4180aabULL}, // 93 bytes; ret=1 "Framerate/Turn is 20 fps"
    {"console:fps 30", 0x1b264aafa24739cdULL}, // 2 bytes; ret=1
    {"console:fps.draw 60", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:ft", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:frametime.max", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:ft.max ; ft.max", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:netstats ; netstats", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:quit", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:time", 0xb5ea85460fa0897eULL}, // 83 bytes; ret=1 "Time: 00:00:01"
    {"console:time 1200 20", 0x0fff042974071da0ULL}, // 83 bytes; ret=1 "Time: 00:01:00"
    {"console:timer.toggle", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:timer.switch", 0x4928bdad24efc388ULL}, // 2 bytes; ret=1
    {"console:turn", 0xe6e8e86666681bfdULL}, // 76 bytes; ret=1 "turn 30"
    {"console:pause", 0xb359226c929bb5d4ULL}, // 2 bytes; ret=1
    {"console:pause ; pause", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:step", 0xba400a4a7080bf63ULL}, // 2 bytes; ret=1
    {"console:game.save 9 golden ; game.load 9", 0x1843821abafec4adULL}, // 0 bytes; ret=1 ; ret=1
    {"console:game.save -1", 0x22dc6f7c3b632f30ULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load -1", 0x22dc6f7c3b632f30ULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load 9999", 0x664f25cd17fc4d5bULL}, // 92 bytes; ret=0 "Unable to load game 9999"
    {"console:cls", 0x851b7d1675bb6691ULL}, // 59 bytes; ret=1
    {"console:ver", 0xb58b6daa8f39b054ULL}, // 114 bytes; ret=1 "dAImon Keeper 1.0.0 - KFX 1.4 (ver <version>)"
    {"console:volume", 0xd85f2a82643d67afULL}, // 89 bytes; ret=1 "Sound FX: 50 Music: 35"
    {"console:volume.sound", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:volume.sound abc", 0x32fb2fea1ea87e20ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:volume.sound 50", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:volume.sfx 40", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:volume.music 30", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:volume.soundtrack abc", 0x32fb2fea1ea87e20ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:compuchat", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:compuchat scarce", 0xea1c22f23794d340ULL}, // 80 bytes; ret=1 "Ai model 7"
    {"console:compuchat 2", 0x5666bda5878641d8ULL}, // 71 bytes; ret=1 "2"
    {"console:compuchat none", 0xe0fd7d4ede2e218bULL}, // 73 bytes; ret=1 "none"
    {"console:comp.procs 1 ; cheat.menu 0", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.events 1 ; cheat.menu 0", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.checks 1 ; cheat.menu 0", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.procs", 0x8033d359feb85b14ULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.events 9", 0xc719ee633b950447ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.checks -1", 0xbcbf5724dc7ce902ULL}, // 96 bytes; ret=0 "player idx [-1] exceeds [0,8]"
    {"console:comp.kill 1", 0x7197754bec1a0547ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill 2", 0x7197754bec1a0547ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill", 0x8033d359feb85b14ULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.kill 99", 0x110d9a5990908abeULL}, // 96 bytes; ret=0 "player idx [99] exceeds [0,8]"
    {"console:comp.kill 9", 0xc719ee633b950447ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.me 1", 0x4e4bd9b505a9821eULL}, // 1021 bytes; ret=1 "computer assistant is 1"
    {"console:comp.me", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:spectate", 0x75a9798c3c73f15fULL}, // 21721 bytes; ret=1 "player 0 is now AI-controlled; map revealed"
    {"console:spectate 1", 0x0b9500ca6143654bULL}, // 94 bytes; ret=0 "unable to spectate player 1"
    {"console:reveal", 0x642a661eb6100c36ULL}, // 21610 bytes; ret=1
    {"console:reveal 10", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:conceal", 0x67f90cc4b0eee360ULL}, // 7228 bytes; ret=1
    {"console:conceal 10", 0x3fd316aa797b7671ULL}, // 145 bytes; ret=1
    {"console:slab.place lava", 0xa8b7ad1f99d8f811ULL}, // 2137 bytes; ret=1
    {"console:slab.place claimed", 0xd237199fe593b78cULL}, // 2114 bytes; ret=1
    {"console:slab.place rock", 0xcde0428ec7f12bf8ULL}, // 2170 bytes; ret=1
    {"console:slab.place gold 1", 0x4143ae323109b4c2ULL}, // 2171 bytes; ret=1
    {"console:slab.place temple", 0x80a34158a1872b23ULL}, // 2123 bytes; ret=1
    {"console:slab.place 10", 0x2e0fa38ab8fe379cULL}, // 2111 bytes; ret=1
    {"console:slab.place 99", 0x8a424b55fa6fa49eULL}, // 94 bytes; ret=0 "slbkind [99] exceeds [0,62]"
    {"console:slab.place notaslab", 0x5280aa8ad9b1936aULL}, // 94 bytes; ret=0 "slbkind [-1] exceeds [0,62]"
    {"console:slab.place", 0x9dfac33f62f2f7fbULL}, // 98 bytes; ret=0 "require parameter 1 as slbkind"
    {"console:slab.place earth", 0x769c4ee7dcbf66daULL}, // 2171 bytes; ret=1
    {"console:slab.place reinforced", 0x04a3700fcd61c6c4ULL}, // 2172 bytes; ret=1
    {"console:slab.place fortified", 0x04a3700fcd61c6c4ULL}, // 2172 bytes; ret=1
    {"console:slab.health", 0x76945eaba6ddf1fbULL}, // 83 bytes; ret=1 "Slab health: 20"
    {"console:slab.health 100", 0xb2b6e35794f43dcdULL}, // 2 bytes; ret=1
    {"console:look treasure", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:look troll", 0x425e1ccbf316fce2ULL}, // 2 bytes; ret=1
    {"console:look 1", 0xc07d9d0263770189ULL}, // 19 bytes; ret=1
    {"console:look notalocation", 0x1b480c5eeddad0bbULL}, // 91 bytes; ret=0 "subtile coord is invalid"
    {"console:look", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:zoomto.slabcoord 10 10", 0xd1202b892474ced1ULL}, // 31 bytes; ret=1
    {"console:zoomto.slabcoord 10", 0xeda8db2b68aa6f91ULL}, // 96 bytes; ret=0 "require parameter 2 as slb_y"
    {"console:zoomto.slabcoord", 0x5c00595759d7bb39ULL}, // 96 bytes; ret=0 "require parameter 1 as slb_x"
    {"console:zoomto.slabcoord 999 999", 0xe14e5e516addca39ULL}, // 107 bytes; ret=0 "Slab coordinates specified are invalid"
    {"console:zoomto.subtilecoord 40 40", 0x30f45a892474ced1ULL}, // 31 bytes; ret=1
    {"console:zoomto.subtilecoord 40", 0xad7aa0565d35b953ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:zoomto.subtilecoord 999 1", 0x99b324ea89ddc54aULL}, // 108 bytes; ret=0 "Subtile coordinates specified are invalid"
    {"console:actionpoint.pos 1", 0xa98ae6361770fb57ULL}, // 105 bytes; ret=1 "Action Point idx: 1 num: 1 X: 225 Y: 130"
    {"console:actionpoint.pos 99", 0xe6002558e3977742ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.pos", 0xa8234ea97d020e8dULL}, // 108 bytes; ret=0 "require parameter 1 as actionpoint number"
    {"console:actionpoint.zoomto 1", 0xc07d9d0263770189ULL}, // 19 bytes; ret=1
    {"console:zoomto.actionpoint 99", 0xe6002558e3977742ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.reset 1", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:actionpoint.reset 1 0", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:herogate.zoomto 1", 0x8a9526c3cf449f86ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:zoomto.herogate 9", 0x8a9526c3cf449f86ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:herogate.zoomto", 0xb578e0cf6640b23fULL}, // 107 bytes; ret=0 "require parameter 1 as hero gate number"
    {"console:mapwho.info", 0x995be28aa1681ba4ULL}, // 247 bytes; ret=1 "first_thing:19 creature IMP" "flags: 02,filled: 9, wib: 1, col: 0532" "mapwho: 0019, rev: 1" "stl_x: 126, stl_y:1
    {"console:mapwho.info 40 40", 0x62b52456ab5c4a58ULL}, // 251 bytes; ret=1 "first_thing:0 EMPTY model 0" "flags: 00,filled: 12, wib: 2, col: 0393" "mapwho: 0000, rev: 0" "stl_x: 40, stl_y:4
    {"console:mapwho.info 40", 0xad7aa0565d35b953ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:mapwho.info 999 999", 0xab2d1d8e19086eaaULL}, // 83 bytes; ret=0 "invalid location"
    {"console:cursor.pos", 0x6327004f72793886ULL}, // 90 bytes; ret=1 "Cursor at 126, 131, 3"
    {"console:player.score", 0x60f5739e870797f9ULL}, // 88 bytes; ret=1 "Player 0 score: 436"
    {"console:player.score 1", 0x2333c4f004c00e50ULL}, // 86 bytes; ret=1 "Player 1 score: 0"
    {"console:player.score 99", 0x47033cd6c5e9771cULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.flag 0 3", 0x95fb8918fbff1184ULL}, // 93 bytes; ret=1 "Player 0 flag 3 value: 0"
    {"console:player.flag 0 3 9 ; player.flag 0 3", 0xe4a732c90d43a58aULL}, // 94 bytes; ret=1 ; ret=1 "Player 0 flag 3 value: 9"
    {"console:player.flag 0 99", 0x4ab5df0891a2d750ULL}, // 93 bytes; ret=0 "script flag [99] exceeds"
    {"console:player.flag 99", 0x47033cd6c5e9771cULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.heart.health", 0xa0e38877e834d6b5ULL}, // 114 bytes; ret=1 "Player 0 heart health: 30000 (100.00 per cent)"
    {"console:player.heart.health 0 1000", 0x5fd38087e8addbfcULL}, // 3 bytes; ret=1
    {"console:player.heart.health 1", 0x7197754bec1a0547ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.heart.health 0 0", 0x6bc63e1d781bfe52ULL}, // 5 bytes; ret=1
    {"console:player.heart.health 99", 0x7197754bec1a0547ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.gold.add 0 1000", 0xa3fc992561c32045ULL}, // 5 bytes; ret=1
    {"console:player.addgold 1 500", 0x2453b9f9d9cdb225ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.gold.add 0", 0xd7fbad88e1abcb71ULL}, // 101 bytes; ret=0 "require parameter 2 as gold amount"
    {"console:player.gold.add 99 1", 0x2453b9f9d9cdb225ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.colour 0 blue", 0xad31c59a4a446cbcULL}, // 2013 bytes; ret=1
    {"console:player.color 1 green", 0x157062b8124edaf1ULL}, // 2 bytes; ret=1
    {"console:player.colour 0", 0x8a81933544dcd571ULL}, // 96 bytes; ret=0 "require parameter 2 as color"
    {"console:player.colour", 0x7efe6a96e02b7b2bULL}, // 97 bytes; ret=0 "require parameter 1 as player"
    {"console:player.colour 0 notacolour", 0x57ea9a73b5ff66e7ULL}, // 85 bytes; ret=0 "colour is invalid"
    {"console:possession.lock", 0xb2eca88fd7a51671ULL}, // 2 bytes; ret=1
    {"console:possession.unlock", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:string.show 5", 0x58687b701462e9c9ULL}, // 23 bytes; ret=1
    {"console:string.show", 0xac1ef83e90454039ULL}, // 15 bytes; ret=1
    {"console:quick.show 5", 0x8cfef5701462e9c9ULL}, // 23 bytes; ret=1
    {"console:quick.show -1", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:give.trap boulder 2", 0x3b19f8afb2e0c945ULL}, // 77 bytes; ret=1 "done!"
    {"console:trap.give gas", 0x4f2258bbe05a1707ULL}, // 77 bytes; ret=1 "done!"
    {"console:give.trap 99", 0x098526df5ac6dddfULL}, // 100 bytes; ret=0 "trap number [99] exceeds (0,10]"
    {"console:give.trap", 0xd5ee27847821debbULL}, // 100 bytes; ret=0 "trap number [-1] exceeds (0,10]"
    {"console:give.door wooden", 0x5aba22abc54f4385ULL}, // 77 bytes; ret=1 "done!"
    {"console:door.give braced 3", 0x1ab99f9355e343fcULL}, // 77 bytes; ret=1 "done!"
    {"console:give.door 99", 0x82dbaf1ef84fa42bULL}, // 100 bytes; ret=0 "door number [99] exceeds (0, 7])"
    {"console:give.door", 0xc839cf633ebd28dfULL}, // 100 bytes; ret=0 "door number [-1] exceeds (0, 7])"
    {"console:room.available temple", 0x2de600839afd2da4ULL}, // 2 bytes; ret=1
    {"console:room.available all", 0xd0b0edf17dfaee23ULL}, // 6 bytes; ret=1
    {"console:room.available temple 0", 0x75abc2868e26baa4ULL}, // 2 bytes; ret=1
    {"console:room.available hatchery", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:room.available guard 1 1", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:room.available", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give all", 0xf61486798f7cff6eULL}, // 118 bytes; ret=1
    {"console:spell.give POWER_LIGHTNING", 0xb4eae4671e853e5aULL}, // 2 bytes; ret=1
    {"console:power.give", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give 999", 0xfcf1fe5328275809ULL}, // 83 bytes; ret=0 "power is invalid"
    {"console:creature.available troll", 0x3ed87745ff51201aULL}, // 2 bytes; ret=1
    {"console:creature.available troll 0", 0xe71cf6a9d0f7379aULL}, // 2 bytes; ret=1
    {"console:creature.available notacreature", 0x7cd9f527326cd118ULL}, // 1 bytes; ret=0
    {"console:magic.instance imp 1 fireball", 0xaf64cb2351617019ULL}, // 6 bytes; ret=1
    {"console:magic.instance imp 1 2", 0xf1711306be88759dULL}, // 6 bytes; ret=1
    {"console:magic.instance", 0x4ddf58b8893a1098ULL}, // 97 bytes; ret=0 "require parameter 1 as creature"
    {"console:magic.instance imp", 0x7e57f4531d322c0aULL}, // 95 bytes; ret=0 "require parameter 2 as slot"
    {"console:magic.instance imp 1", 0x34823f52caf77115ULL}, // 98 bytes; ret=0 "require parameter 3 as instance"
    {"console:magic.instance notacreature 1 fireball", 0x857cfbd591acf333ULL}, // 84 bytes; ret=0 "Invalid creature"
    {"console:magic.instance imp 12 fireball", 0xba4d90b644c26cbeULL}, // 80 bytes; ret=0 "Invalid slot"
    {"console:magic.instance imp 1 0", 0x3fbab95c4332968dULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:magic.instance imp 1 9999", 0x3fbab95c4332968dULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:map.pool", 0x730c7d3d733dde27ULL}, // 101 bytes; ret=1 "require parameter 1 as creature model"
    {"console:creature.pool troll", 0xb2ee1ab90e85f987ULL}, // 89 bytes; ret=1 "Pool count troll: 20"
    {"console:map.pool troll 7", 0x42f76143ec85be0eULL}, // 93 bytes; ret=1 "Set troll pool count: 7"
    {"console:map.pool troll -1", 0x46dd8c8cadc4b722ULL}, // 93 bytes; ret=0 "Set troll pool count: -1"
    {"console:map.pool notacreature", 0x6749e5eb334953fdULL}, // 95 bytes; ret=0 "Invalid creature: notacreature"
    {"console:creature.pool.add troll 3", 0xc617c1368c9b6323ULL}, // 2 bytes; ret=1
    {"console:creature.pool.sub troll 1", 0x834045368c9b6323ULL}, // 2 bytes; ret=1
    {"console:creature.pool.remove troll 1", 0x834045368c9b6323ULL}, // 2 bytes; ret=1
    {"console:creature.pool.add troll", 0x0688939b7b1707d0ULL}, // 104 bytes; ret=0 "require parameter 2 as creature amount"
    {"console:creature.pool.add 0 3", 0xe95b69121373e2f7ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add any 5", 0xe95b69121373e2f7ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add evil 5", 0xe95b69121373e2f7ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.sub 500 1", 0xe95b69121373e2f7ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:gold.create 500", 0x0289cdd34d43c0dcULL}, // 48 bytes; ret=1
    {"console:create.gold", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:gold.create abc", 0x32fb2fea1ea87e20ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:object.create barrel", 0xbc74e93e6557be28ULL}, // 44 bytes; ret=1
    {"console:create.object barrel 1", 0x99ba7e3e6557be28ULL}, // 44 bytes; ret=1
    {"console:object.create 5", 0xce7989b74124e3a7ULL}, // 64 bytes; ret=1
    {"console:object.create", 0x47e4aa6d7763423bULL}, // 102 bytes; ret=0 "require parameter 1 as object model"
    {"console:object.create notanobject", 0x22c87de601b45389ULL}, // 91 bytes; ret=0 "object model is invalid"
    {"console:creature.create troll", 0xfcf02d10b1923dabULL}, // 67 bytes; ret=1
    {"console:create.creature troll 5 2", 0x33ad75182353934bULL}, // 129 bytes; ret=1
    {"console:creature.create troll 1 1 1", 0xd02e64f242e9b279ULL}, // 65 bytes; ret=1
    {"console:creature.create evil", 0xe285d04ed5f413afULL}, // 64 bytes; ret=1
    {"console:creature.create good", 0x803c0207a7db3f5cULL}, // 67 bytes; ret=1
    {"console:creature.create any", 0x6da3eb5b869ffad3ULL}, // 66 bytes; ret=1
    {"console:creature.create 999", 0x0494a1ab5a3ef0ecULL}, // 103 bytes; ret=0 "creature model [999] exceeds (0, 37)"
    {"console:creature.create notacreature", 0x50466766dfd6013aULL}, // 103 bytes; ret=0 "creature model [-1] exceeds (0, 37)"
    {"console:creature.create", 0xa5bf86497371c7d6ULL}, // 101 bytes; ret=0 "require parameter 1 as creature model"
    {"console:thing.create object barrel", 0xd9b683878c08a489ULL}, // 46 bytes; ret=1
    {"console:create.thing corpse troll", 0xb38f6b4a1cd8c208ULL}, // 35 bytes; ret=1
    {"console:thing.create creature imp", 0xe87e78936a3552c8ULL}, // 66 bytes; ret=1
    {"console:thing.create trap boulder", 0x3ef0e976bc66ac02ULL}, // 39 bytes; ret=1
    {"console:thing.create effect EFFECT_EXPLOSION_1", 0x4a50b0f03e1eb6b4ULL}, // 46 bytes; ret=1
    {"console:thing.create shot SHOT_FIREBALL", 0xf4214720c77d7154ULL}, // 60 bytes; ret=1
    {"console:thing.create 1 5", 0xef7be2da699d766cULL}, // 66 bytes; ret=1
    {"console:thing.create nonsense x", 0x29499a3ceffa573aULL}, // 83 bytes; ret=0 "class is invalid"
    {"console:thing.create object", 0x4d255ab22cbcfd8fULL}, // 96 bytes; ret=0 "require parameter 2 as model"
    {"console:thing.create", 0x8805bd9d538c00d7ULL}, // 95 bytes; ret=0 "require parameter 1 as class"
    {"console:thing.get", 0x482219357d6fe1fcULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get $C0", 0x482219357d6fe1fcULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get 0", 0x44da937146411ba0ULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.get $HEART", 0x6ac844170e1fa345ULL}, // 106 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER"
    {"console:thing.get $HERO", 0x44da937146411ba0ULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.info", 0x1cff937244906c2bULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.info", 0x87491bfeb8a78c06ULL}, // 292 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "next_on_map: 0, next_of_class: 0" "health: 75" "pos: 126 131 3" "creature 
    {"console:thing.get $HEART ; thing.info", 0xf51e9d9303189b2eULL}, // 294 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1 "next_on_map: 0, next_of_class: 10" "health: 30000" "pos: 127 130 
    {"console:thing.show_id ; thing.show_id 0", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:thing.get $C0 ; thing.health", 0x6fcf5b1c718a8e96ULL}, // 143 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "Thing ID: 19 health: 75"
    {"console:thing.get $C0 ; thing.health 5", 0xb95fbcf3400b9d57ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.health", 0x1cff937244906c2bULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.move 40 40", 0xdd53624913c59a02ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 40 40 3", 0xeddc78634af42f80ULL}, // 103 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move", 0x1c2e56dfaf0c0818ULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 999 999", 0xb6285f8144ffa705ULL}, // 144 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "invalid location"
    {"console:thing.move", 0x1cff937244906c2bULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.destroy", 0x1cff937244906c2bULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get 0 ; thing.destroy", 0x9238f46c0258b775ULL}, // 151 bytes; ret=0 "thing is invalid" ; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.destroy", 0x26a3c6728e3de283ULL}, // 204 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $HEART ; thing.destroy", 0xeb4822e109b3364bULL}, // 177 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1
    {"console:creature.addhealth 10", 0x8bf3b7ad6d7b2c75ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.addhealth 10", 0x45356cf3400b9d57ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.health.sub 10", 0x2a0448f3400b9d57ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.subhealth abc", 0x5f1ce2dd41951d55ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "parameter 1 requires a number"
    {"console:creature.health.add", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:thing.get $C0 ; creature.level 5", 0x1ccb1261c553f949ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.level", 0xacb68f5ee0ad9d34ULL}, // 102 bytes; ret=0 "require parameter 1 as creature level"
    {"console:creature.level 5", 0x8bf3b7ad6d7b2c75ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.freeze", 0x5ec2e0bad3092bdbULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.slow", 0xd90fc9e5abcb1050ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.freeze", 0x8bf3b7ad6d7b2c75ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.chicken", 0x22ea08bd86100087ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.chicken", 0x8bf3b7ad6d7b2c75ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.instance.set 2", 0x34f6841ee0d649e6ULL}, // 101 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.instance.set", 0xc157874c1d1a4213ULL}, // 98 bytes; ret=0 "require parameter 1 as instance"
    {"console:creature.instance.set 2", 0x8bf3b7ad6d7b2c75ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.state.set 1", 0x0f061df24aa36355ULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.state.set 0", 0xdd475cf24aa36355ULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.state.set", 0x466a039f4f11afbcULL}, // 96 bytes; ret=0 "require parameter 1 as state"
    {"console:thing.get $C0 ; creature.job.set 1", 0x9e650e7215b585bfULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 1."
    {"console:thing.get $C0 ; creature.job.set 3", 0x81c1206fcfce8166ULL}, // 124 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.job.set", 0xf9dd7561d1830c69ULL}, // 94 bytes; ret=0 "require parameter 1 as job"
    {"console:thing.get $C0 ; creature.job.set 70", 0x19ff7fa8d410aebdULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 70."
    {"console:thing.get $C0 ; creature.attackheart 1", 0x4bfc13c1914b7e05ULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:thing.get $C0 ; creature.attackheart 99", 0x4bfc13c1914b7e05ULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:creature.attackheart", 0x4a54854633074a03ULL}, // 104 bytes; ret=0 "require parameter 1 as player number"
    {"console:creature.attackheart 1", 0x8bf3b7ad6d7b2c75ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:digger.sendto", 0x268eef7a69dc6701ULL}, // 109 bytes; ret=1 "creature IMP 22 will dig to PLAYER0"
    {"console:digger.sendto 1", 0x2453b9f9d9cdb225ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:thing.get $C0 ; digger.sendto 0", 0xf4aaf7b6b2e843cdULL}, // 157 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "creature IMP 19 will dig to PLAYER0"
    {"console:digger.sendto 99", 0x2453b9f9d9cdb225ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:room.get", 0x90ba67582adc4bb6ULL}, // 83 bytes; ret=1 "Got room ID 7"
    {"console:room.get $ROOM", 0x5df2c5580ca9b69cULL}, // 83 bytes; ret=1 "Got room ID 1"
    {"console:room.get 0", 0x8f4f382d86b9b84eULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get 511", 0x8f4f382d86b9b84eULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get $ROOM ; room.health", 0x1fd5bf6b350662ceULL}, // 138 bytes; ret=1 "Got room ID 1" ; ret=1 "Room ID 1 health: 18"
    {"console:room.get $ROOM ; room.health 10", 0xa66b28a1ff65a362ULL}, // 84 bytes; ret=1 "Got room ID 1" ; ret=1
    {"console:room.health", 0x018d68abfa7db2efULL}, // 97 bytes; ret=0 "no thing selected or not room"
    {"console:bug.toggle", 0xc4d25e7d4a299ed2ULL}, // 99 bytes; ret=0 "require parameter 1 as bug name"
    {"console:bug.toggle 3", 0xca336a69c9138567ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle RESURRECT_FOREVER", 0x0ec437df1666a8ccULL}, // 94 bytes; ret=1 "RESURRECT_FOREVER enabled"
    {"console:bug.toggle CLAIM_ROOM_ALL_THINGS", 0xca336a69c9138567ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle ALWAYS_TUNNEL_TO_RED", 0xb6dfac71afc3f7e0ULL}, // 98 bytes; ret=1 "ALWAYS_TUNNEL_TO_RED enabled"
    {"console:bug.toggle 19", 0xd9ef88b731b7d97eULL}, // 87 bytes; ret=0 "bug [19] is invalid"
    {"console:bug.toggle 0", 0x8de895f84686dfbeULL}, // 87 bytes; ret=0 "bug [0] is invalid"
    {"console:bug.toggle notabug", 0x0dbdfe4aaacc79fcULL}, // 92 bytes; ret=0 "bug [notabug] is invalid"
    {"console:music.set 2", 0x929ba9f4305f64d8ULL}, // 2 bytes; ret=0
    {"console:music.set", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:sound.test 1", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:sound.test", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:speech.test 1", 0x7cd5f5273268e841ULL}, // 1 bytes; ret=1
    {"console:speech.test", 0x4c284aa91ef457fbULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:toggle.tooltip.land.coord ; toggle.tooltip.land.coord", 0xc56e91bbc82cee34ULL}, // 147 bytes; ret=1 "tooltip_land_coord is turned on" ; ret=1 "tooltip_land_coord is turned off"
    {"console:toggle.lights", 0xba5201768e8e49d6ULL}, // 4 bytes; ret=1
    {"console:lua PLAYER0.MONEY = 7", 0x4f51a32561c32045ULL}, // 5 bytes; ret=1
    {"console:lua not valid lua(", 0xeddadb4dfb091117ULL}, // 80 bytes; ret=1 "lua error, see log"
    {"console:cheat.menu 1 ; cheat.menu 0", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 2 ; cheat.menu 0", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu instance ; cheat.menu none", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu secondary ; cheat.menu 0", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 9", 0x1d11317cd2c3af5aULL}, // 128 bytes; ret=0 "menu type is invalid. possible values: 0/1/2/3/4, none/main/cre"
    {"console:cheat.menu", 0x60d3cb131e77df94ULL}, // 100 bytes; ret=0 "require parameter 1 as menu type"
    {"console:dbc ; dbc", 0x91b58c95554e9cedULL}, // 1 bytes; ret=1 ; ret=1
    {"console:resync", 0xd5fe92550fc6fe2bULL}, // 4 bytes; ret=1
