    {"console:", 0xf9bd343279e55d09ULL}, // 85 bytes; ret=0 "command is empty"
    {"console:notacommand", 0xaae3a10f8e9f2af0ULL}, // 87 bytes; ret=0 "unsupported command"
    {"console:STATS", 0xbb12cce07f94adf0ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-stats", 0x91f27d0bc63faff5ULL}, // 95 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-reveal", 0x55dc693eb2cf9262ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-notacommand", 0x8e283c3e34d25158ULL}, // 0 bytes; ret=0
    {"console:-thing.get $C0", 0x55dc693eb2cf9262ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-room.get $ROOM", 0x55dc693eb2cf9262ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:stats", 0xbb12cce07f94adf0ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:fps", 0x34d7bfa3445b2e56ULL}, // 93 bytes; ret=1 "Framerate/Turn is 20 fps"
    {"console:fps 30", 0xa37f99f373191d4bULL}, // 2 bytes; ret=1
    {"console:fps.draw 60", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:ft", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:frametime.max", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:ft.max ; ft.max", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:netstats ; netstats", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:quit", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:time", 0xb62a2610b78d25dfULL}, // 83 bytes; ret=1 "Time: 00:00:01"
    {"console:time 1200 20", 0xad8ed57623c5a105ULL}, // 83 bytes; ret=1 "Time: 00:01:00"
    {"console:timer.toggle", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:timer.switch", 0xd58709fd3d0610a8ULL}, // 2 bytes; ret=1
    {"console:turn", 0x670572069ce693cdULL}, // 76 bytes; ret=1 "turn 30"
    {"console:pause", 0x4d4ab272fe89ea40ULL}, // 2 bytes; ret=1
    {"console:pause ; pause", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:step", 0x4c76c6ccfec497f6ULL}, // 2 bytes; ret=1
    {"console:game.save 9 golden ; game.load 9", 0x1843821abafec4adULL}, // 0 bytes; ret=1 ; ret=1
    {"console:game.save -1", 0xe04406599788d004ULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load -1", 0xe04406599788d004ULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load 9999", 0xef08340a30e819ddULL}, // 92 bytes; ret=0 "Unable to load game 9999"
    {"console:cls", 0xe6c03d11c8d3d058ULL}, // 59 bytes; ret=1
    {"console:ver", 0xf8a68a150bd06e40ULL}, // 114 bytes; ret=1 "dAImon Keeper 1.0.0 - KFX 1.4 (ver <version>)"
    {"console:volume", 0x68656af0bad5b768ULL}, // 89 bytes; ret=1 "Sound FX: 50 Music: 35"
    {"console:volume.sound", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:volume.sound abc", 0x0be928b48c29bf17ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:volume.sound 50", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:volume.sfx 40", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:volume.music 30", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:volume.soundtrack abc", 0x0be928b48c29bf17ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:compuchat", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:compuchat scarce", 0x71d7beb301ce0989ULL}, // 80 bytes; ret=1 "Ai model 7"
    {"console:compuchat 2", 0x7baf95b9b75a0522ULL}, // 71 bytes; ret=1 "2"
    {"console:compuchat none", 0x6b7221d000367cbeULL}, // 73 bytes; ret=1 "none"
    {"console:comp.procs 1 ; cheat.menu 0", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.events 1 ; cheat.menu 0", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.checks 1 ; cheat.menu 0", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.procs", 0xfd3a940960d27ae5ULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.events 9", 0xc0b7d51cb930c58dULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.checks -1", 0xa20683b4552e0cd0ULL}, // 96 bytes; ret=0 "player idx [-1] exceeds [0,8]"
    {"console:comp.kill 1", 0xec861b043744822cULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill 2", 0xec861b043744822cULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill", 0xfd3a940960d27ae5ULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.kill 99", 0x2ef12cb8f274afc4ULL}, // 96 bytes; ret=0 "player idx [99] exceeds [0,8]"
    {"console:comp.kill 9", 0xc0b7d51cb930c58dULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.me 1", 0x7f414b5965113417ULL}, // 1021 bytes; ret=1 "computer assistant is 1"
    {"console:comp.me", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:spectate", 0x4b405f5569710c80ULL}, // 21721 bytes; ret=1 "player 0 is now AI-controlled; map revealed"
    {"console:spectate 1", 0xba0cd62648c855e5ULL}, // 94 bytes; ret=0 "unable to spectate player 1"
    {"console:reveal", 0x8dea6d64632e318dULL}, // 21610 bytes; ret=1
    {"console:reveal 10", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:conceal", 0x7bce9ad7383da4afULL}, // 7228 bytes; ret=1
    {"console:conceal 10", 0x3886216c6d4cf682ULL}, // 145 bytes; ret=1
    {"console:slab.place lava", 0xb90c185175893038ULL}, // 2137 bytes; ret=1
    {"console:slab.place claimed", 0x1c0fd279a7415a4dULL}, // 2114 bytes; ret=1
    {"console:slab.place rock", 0x6459309de4e1acd1ULL}, // 2170 bytes; ret=1
    {"console:slab.place gold 1", 0x7c2095e49ac66f0bULL}, // 2171 bytes; ret=1
    {"console:slab.place temple", 0xe32fff3c031add1aULL}, // 2123 bytes; ret=1
    {"console:slab.place 10", 0xad771d44a57207fdULL}, // 2111 bytes; ret=1
    {"console:slab.place 99", 0x429003ff5164cbf0ULL}, // 94 bytes; ret=0 "slbkind [99] exceeds [0,62]"
    {"console:slab.place notaslab", 0x6bb3ec233dab38c4ULL}, // 94 bytes; ret=0 "slbkind [-1] exceeds [0,62]"
    {"console:slab.place", 0x3c8d5c2a2c15298fULL}, // 98 bytes; ret=0 "require parameter 1 as slbkind"
    {"console:slab.place earth", 0x7ce734a76398ebe3ULL}, // 2171 bytes; ret=1
    {"console:slab.place reinforced", 0x35136a5c81cc9895ULL}, // 2172 bytes; ret=1
    {"console:slab.place fortified", 0x35136a5c81cc9895ULL}, // 2172 bytes; ret=1
    {"console:slab.health", 0x99a725de6f691db6ULL}, // 83 bytes; ret=1 "Slab health: 20"
    {"console:slab.health 100", 0xf7e6a0cdc0534e76ULL}, // 2 bytes; ret=1
    {"console:look treasure", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:look troll", 0xfea17c9259686c11ULL}, // 2 bytes; ret=1
    {"console:look 1", 0xa234e146e998c617ULL}, // 19 bytes; ret=1
    {"console:look notalocation", 0xcb2e2f011ed0044eULL}, // 91 bytes; ret=0 "subtile coord is invalid"
    {"console:look", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:zoomto.slabcoord 10 10", 0xf4ba6c6d17f1748fULL}, // 31 bytes; ret=1
    {"console:zoomto.slabcoord 10", 0x611801a5786b6631ULL}, // 96 bytes; ret=0 "require parameter 2 as slb_y"
    {"console:zoomto.slabcoord", 0x8d07fdc8e5c87359ULL}, // 96 bytes; ret=0 "require parameter 1 as slb_x"
    {"console:zoomto.slabcoord 999 999", 0x7bd6fcbc92d9b450ULL}, // 107 bytes; ret=0 "Slab coordinates specified are invalid"
    {"console:zoomto.subtilecoord 40 40", 0xa3da076d17f1748fULL}, // 31 bytes; ret=1
    {"console:zoomto.subtilecoord 40", 0x7b0d61a3f0aa5c73ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:zoomto.subtilecoord 999 1", 0x1596363676124b1eULL}, // 108 bytes; ret=0 "Subtile coordinates specified are invalid"
    {"console:actionpoint.pos 1", 0x60fb21741b87f388ULL}, // 105 bytes; ret=1 "Action Point idx: 1 num: 1 X: 225 Y: 130"
    {"console:actionpoint.pos 99", 0x3db2f554d7e32bdbULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.pos", 0xae548363b9d6fc63ULL}, // 108 bytes; ret=0 "require parameter 1 as actionpoint number"
    {"console:actionpoint.zoomto 1", 0xa234e146e998c617ULL}, // 19 bytes; ret=1
    {"console:zoomto.actionpoint 99", 0x3db2f554d7e32bdbULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.reset 1", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:actionpoint.reset 1 0", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:herogate.zoomto 1", 0xe703f63e33551a48ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:zoomto.herogate 9", 0xe703f63e33551a48ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:herogate.zoomto", 0x64cfa7449a079aeeULL}, // 107 bytes; ret=0 "require parameter 1 as hero gate number"
    {"console:mapwho.info", 0xcf033cd3f2991197ULL}, // 247 bytes; ret=1 "first_thing:19 creature IMP" "flags: 02,filled: 9, wib: 1, col: 0532" "mapwho: 0019, rev: 1" "stl_x: 126, stl_y:1
    {"console:mapwho.info 40 40", 0x450513ce79f99c5dULL}, // 251 bytes; ret=1 "first_thing:0 EMPTY model 0" "flags: 00,filled: 12, wib: 2, col: 0393" "mapwho: 0000, rev: 0" "stl_x: 40, stl_y:4
    {"console:mapwho.info 40", 0x7b0d61a3f0aa5c73ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:mapwho.info 999 999", 0xd153ed039b27b3b5ULL}, // 83 bytes; ret=0 "invalid location"
    {"console:cursor.pos", 0xadab1210e34a0a62ULL}, // 90 bytes; ret=1 "Cursor at 126, 131, 3"
    {"console:player.score", 0x59fd89f97aae9bd9ULL}, // 88 bytes; ret=1 "Player 0 score: 436"
    {"console:player.score 1", 0x3ec3ea04a0b2c424ULL}, // 86 bytes; ret=1 "Player 1 score: 0"
    {"console:player.score 99", 0x5ae776c274b9164dULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.flag 0 3", 0x3a0060e7111dc599ULL}, // 93 bytes; ret=1 "Player 0 flag 3 value: 0"
    {"console:player.flag 0 3 9 ; player.flag 0 3", 0x6057ef9ad40b8207ULL}, // 94 bytes; ret=1 ; ret=1 "Player 0 flag 3 value: 9"
    {"console:player.flag 0 99", 0x0da62c86f0ea7329ULL}, // 93 bytes; ret=0 "script flag [99] exceeds"
    {"console:player.flag 99", 0x5ae776c274b9164dULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.heart.health", 0xbf8e7870a2f4bd81ULL}, // 114 bytes; ret=1 "Player 0 heart health: 30000 (100.00 per cent)"
    {"console:player.heart.health 0 1000", 0xc0d93b2c087f3fe3ULL}, // 3 bytes; ret=1
    {"console:player.heart.health 1", 0xec861b043744822cULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.heart.health 0 0", 0x25dfcea465e2df21ULL}, // 5 bytes; ret=1
    {"console:player.heart.health 99", 0xec861b043744822cULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.gold.add 0 1000", 0xa2f97e23a226988eULL}, // 5 bytes; ret=1
    {"console:player.addgold 1 500", 0xfda911b27d70622dULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.gold.add 0", 0xb5693c0aa6466612ULL}, // 101 bytes; ret=0 "require parameter 2 as gold amount"
    {"console:player.gold.add 99 1", 0xfda911b27d70622dULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.colour 0 blue", 0xa9efaad6261f78a3ULL}, // 2013 bytes; ret=1
    {"console:player.color 1 green", 0xbba7e328324d8002ULL}, // 2 bytes; ret=1
    {"console:player.colour 0", 0xb1a8b37ce5210551ULL}, // 96 bytes; ret=0 "require parameter 2 as color"
    {"console:player.colour", 0x9a6dc087bf7d0ba2ULL}, // 97 bytes; ret=0 "require parameter 1 as player"
    {"console:player.colour 0 notacolour", 0x763d207c34990600ULL}, // 85 bytes; ret=0 "colour is invalid"
    {"console:possession.lock", 0xab9fb351cb769682ULL}, // 2 bytes; ret=1
    {"console:possession.unlock", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:string.show 5", 0x9ca571631a332ff5ULL}, // 23 bytes; ret=1
    {"console:string.show", 0x84804d393892a635ULL}, // 15 bytes; ret=1
    {"console:quick.show 5", 0x5aee87631a332ff5ULL}, // 23 bytes; ret=1
    {"console:quick.show -1", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:give.trap boulder 2", 0xdffe5c54cf85c0a1ULL}, // 77 bytes; ret=1 "done!"
    {"console:trap.give gas", 0x447a63b75727e057ULL}, // 77 bytes; ret=1 "done!"
    {"console:give.trap 99", 0x85c1f83af363c857ULL}, // 100 bytes; ret=0 "trap number [99] exceeds (0,10]"
    {"console:give.trap", 0xae1f717d58d812c3ULL}, // 100 bytes; ret=0 "trap number [-1] exceeds (0,10]"
    {"console:give.door wooden", 0xc66e675baddfc461ULL}, // 77 bytes; ret=1 "done!"
    {"console:door.give braced 3", 0x1a646e0a0df4edf8ULL}, // 77 bytes; ret=1 "done!"
    {"console:give.door 99", 0xf335129bdf991ccfULL}, // 100 bytes; ret=0 "door number [99] exceeds (0, 7])"
    {"console:give.door", 0x0633fbc4024f888bULL}, // 100 bytes; ret=0 "door number [-1] exceeds (0, 7])"
    {"console:room.available temple", 0x55036a3c85b9306bULL}, // 2 bytes; ret=1
    {"console:room.available all", 0x81777e63ce4d073cULL}, // 6 bytes; ret=1
    {"console:room.available temple 0", 0x9cc92c3f78e2bd6bULL}, // 2 bytes; ret=1
    {"console:room.available hatchery", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:room.available guard 1 1", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:room.available", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give all", 0x58b0d12c960759e5ULL}, // 118 bytes; ret=1
    {"console:spell.give POWER_LIGHTNING", 0x36aac80118331aa9ULL}, // 2 bytes; ret=1
    {"console:power.give", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give 999", 0xd1ceadd7108f07e0ULL}, // 83 bytes; ret=0 "power is invalid"
    {"console:creature.available troll", 0xbca3180551f8cb69ULL}, // 2 bytes; ret=1
    {"console:creature.available troll 0", 0x126c0d174fcc07e9ULL}, // 2 bytes; ret=1
    {"console:creature.available notacreature", 0x65e436e82938d4dbULL}, // 1 bytes; ret=0
    {"console:magic.instance imp 1 fireball", 0xd8ab63aeda883020ULL}, // 6 bytes; ret=1
    {"console:magic.instance imp 1 2", 0xa7450a8a685799ecULL}, // 6 bytes; ret=1
    {"console:magic.instance", 0xf0ebe79238f339cbULL}, // 97 bytes; ret=0 "require parameter 1 as creature"
    {"console:magic.instance imp", 0x0755a726ea6d1557ULL}, // 95 bytes; ret=0 "require parameter 2 as slot"
    {"console:magic.instance imp 1", 0xe1f122e54d4fd68dULL}, // 98 bytes; ret=0 "require parameter 3 as instance"
    {"console:magic.instance notacreature 1 fireball", 0xa2fd43f0a1013e65ULL}, // 84 bytes; ret=0 "Invalid creature"
    {"console:magic.instance imp 12 fireball", 0x5f62d1130bf967ccULL}, // 80 bytes; ret=0 "Invalid slot"
    {"console:magic.instance imp 1 0", 0xf78cb84fa238b933ULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:magic.instance imp 1 9999", 0xf78cb84fa238b933ULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:map.pool", 0xa2ec60ef920972b2ULL}, // 101 bytes; ret=1 "require parameter 1 as creature model"
    {"console:creature.pool troll", 0x4dc36b65ff9da112ULL}, // 89 bytes; ret=1 "Pool count troll: 20"
    {"console:map.pool troll 7", 0xff4a8af5584820e5ULL}, // 93 bytes; ret=1 "Set troll pool count: 7"
    {"console:map.pool troll -1", 0x5c882efebf5a05d7ULL}, // 93 bytes; ret=0 "Set troll pool count: -1"
    {"console:map.pool notacreature", 0xb281a9d8d3a3b71eULL}, // 95 bytes; ret=0 "Invalid creature: notacreature"
    {"console:creature.pool.add troll 3", 0x442f76fcad290831ULL}, // 2 bytes; ret=1
    {"console:creature.pool.sub troll 1", 0x0157fafcad290831ULL}, // 2 bytes; ret=1
    {"console:creature.pool.remove troll 1", 0x0157fafcad290831ULL}, // 2 bytes; ret=1
    {"console:creature.pool.add troll", 0xa7746ee8e46a5c36ULL}, // 104 bytes; ret=0 "require parameter 2 as creature amount"
    {"console:creature.pool.add 0 3", 0x21b7484244a37371ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add any 5", 0x21b7484244a37371ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add evil 5", 0x21b7484244a37371ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.sub 500 1", 0x21b7484244a37371ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:gold.create 500", 0x1bebf2ffca1b3d03ULL}, // 48 bytes; ret=1
    {"console:create.gold", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:gold.create abc", 0x0be928b48c29bf17ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:object.create barrel", 0x00863d498d6af257ULL}, // 44 bytes; ret=1
    {"console:create.object barrel 1", 0xddcbd2498d6af257ULL}, // 44 bytes; ret=1
    {"console:object.create 5", 0x201ae534ac7f2110ULL}, // 64 bytes; ret=1
    {"console:object.create", 0xb9e04cbacf491b91ULL}, // 102 bytes; ret=0 "require parameter 1 as object model"
    {"console:object.create notanobject", 0xf8401461cd6ded64ULL}, // 91 bytes; ret=0 "object model is invalid"
    {"console:creature.create troll", 0x219ba130ad2868e4ULL}, // 67 bytes; ret=1
    {"console:create.creature troll 5 2", 0x5fca90e8477766c4ULL}, // 129 bytes; ret=1
    {"console:creature.create troll 1 1 1", 0x2978ac4d150833caULL}, // 65 bytes; ret=1
    {"console:creature.create evil", 0xceb0423c4ea55260ULL}, // 64 bytes; ret=1
    {"console:creature.create good", 0xec1bb185f888fc83ULL}, // 67 bytes; ret=1
    {"console:creature.create any", 0x478f3e3372b0c3ccULL}, // 66 bytes; ret=1
    {"console:creature.create 999", 0xa8572e22601f3805ULL}, // 103 bytes; ret=0 "creature model [999] exceeds (0, 37)"
    {"console:creature.create notacreature", 0xa49da45de5c2f2dfULL}, // 103 bytes; ret=0 "creature model [-1] exceeds (0, 37)"
    {"console:creature.create", 0x7ef1ba206fb13243ULL}, // 101 bytes; ret=0 "require parameter 1 as creature model"
    {"console:thing.create object barrel", 0x0d294551ea9141faULL}, // 46 bytes; ret=1
    {"console:create.thing corpse troll", 0xfebff95ad23a64f7ULL}, // 35 bytes; ret=1
    {"console:thing.create creature imp", 0x3904fe4de9ddcab7ULL}, // 66 bytes; ret=1
    {"console:thing.create trap boulder", 0x98b969069c6806f1ULL}, // 39 bytes; ret=1
    {"console:thing.create effect EFFECT_EXPLOSION_1", 0xf9e0f698f0f5568fULL}, // 46 bytes; ret=1
    {"console:thing.create shot SHOT_FIREBALL", 0xe818d13b9a85a62fULL}, // 60 bytes; ret=1
    {"console:thing.create 1 5", 0xe4ab4e3c1dbe4a07ULL}, // 66 bytes; ret=1
    {"console:thing.create nonsense x", 0x38c7313ffb884a47ULL}, // 83 bytes; ret=0 "class is invalid"
    {"console:thing.create object", 0x648a1ef9ce710eefULL}, // 96 bytes; ret=0 "require parameter 2 as model"
    {"console:thing.create", 0xf8a82976a03fe010ULL}, // 95 bytes; ret=0 "require parameter 1 as class"
    {"console:thing.get", 0xc02d793c32faa7e3ULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get $C0", 0xc02d793c32faa7e3ULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get 0", 0xdf41148eaa13ab14ULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.get $HEART", 0x888b1ae0311b2bf8ULL}, // 106 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER"
    {"console:thing.get $HERO", 0xdf41148eaa13ab14ULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.info", 0xd49a4a01849daf2cULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.info", 0x9429bb1922d96f8dULL}, // 292 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "next_on_map: 0, next_of_class: 0" "health: 75" "pos: 126 131 3" "creature 
    {"console:thing.get $HEART ; thing.info", 0x0700e9861179c879ULL}, // 294 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1 "next_on_map: 0, next_of_class: 10" "health: 30000" "pos: 127 130 
    {"console:thing.show_id ; thing.show_id 0", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:thing.get $C0 ; thing.health", 0x391686d7d1dc8676ULL}, // 143 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "Thing ID: 19 health: 75"
    {"console:thing.get $C0 ; thing.health 5", 0xeca55fb374e29e64ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.health", 0xd49a4a01849daf2cULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.move 40 40", 0x4ac3fe05e048c751ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 40 40 3", 0x10185064da81c667ULL}, // 103 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move", 0xb760852ba1daf42fULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 999 999", 0x81c44aa24abc6be0ULL}, // 144 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "invalid location"
    {"console:thing.move", 0xd49a4a01849daf2cULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.destroy", 0xd49a4a01849daf2cULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get 0 ; thing.destroy", 0x5d66a4942d92f78cULL}, // 151 bytes; ret=0 "thing is invalid" ; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.destroy", 0xefe295ca9a3d1bc8ULL}, // 204 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $HEART ; thing.destroy", 0x251cb5e668d93fa2ULL}, // 177 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1
    {"console:creature.addhealth 10", 0x50c5a67f7e8efd16ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.addhealth 10", 0x76a40fb374e29e64ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.health.sub 10", 0xe4c28bb374e29e64ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.subhealth abc", 0x3727827f0095ec5cULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "parameter 1 requires a number"
    {"console:creature.health.add", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:thing.get $C0 ; creature.level 5", 0xf997e45ea669835eULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.level", 0x3e3f56f804e1bae2ULL}, // 102 bytes; ret=0 "require parameter 1 as creature level"
    {"console:creature.level 5", 0x50c5a67f7e8efd16ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.freeze", 0xe2fa581d633020f0ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.slow", 0x59f3422d5c963ad7ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.freeze", 0x50c5a67f7e8efd16ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.chicken", 0x7f4221e6c57c7df4ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.chicken", 0x50c5a67f7e8efd16ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.instance.set 2", 0xd481478a95b3046dULL}, // 101 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.instance.set", 0xddd34b38455fd34bULL}, // 98 bytes; ret=0 "require parameter 1 as instance"
    {"console:creature.instance.set 2", 0x50c5a67f7e8efd16ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.state.set 1", 0xcf855b3caf398362ULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.state.set 0", 0x0a463a3caf398362ULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.state.set", 0x11a901ab8d05a51cULL}, // 96 bytes; ret=0 "require parameter 1 as state"
    {"console:thing.get $C0 ; creature.job.set 1", 0x05bb77a6eaf0cc54ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 1."
    {"console:thing.get $C0 ; creature.job.set 3", 0xaa223c0e4bf9c595ULL}, // 124 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.job.set", 0xaa8205365617e841ULL}, // 94 bytes; ret=0 "require parameter 1 as job"
    {"console:thing.get $C0 ; creature.job.set 70", 0x545c2c5d0bcbe0d8ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 70."
    {"console:thing.get $C0 ; creature.attackheart 1", 0x29b82eeac7581eecULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:thing.get $C0 ; creature.attackheart 99", 0x29b82eeac7581eecULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:creature.attackheart", 0x2eca577d76f34cf3ULL}, // 104 bytes; ret=0 "require parameter 1 as player number"
    {"console:creature.attackheart 1", 0x50c5a67f7e8efd16ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:digger.sendto", 0x83512c764eb77ad0ULL}, // 109 bytes; ret=1 "creature IMP 22 will dig to PLAYER0"
    {"console:digger.sendto 1", 0xfda911b27d70622dULL}, // 84 bytes; ret=0 "player no exist"
    {"console:thing.get $C0 ; digger.sendto 0", 0x4198e486f8d9cbfeULL}, // 157 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "creature IMP 19 will dig to PLAYER0"
    {"console:digger.sendto 99", 0xfda911b27d70622dULL}, // 84 bytes; ret=0 "player no exist"
    {"console:room.get", 0xd10d47e0f9461dfeULL}, // 83 bytes; ret=1 "Got room ID 7"
    {"console:room.get $ROOM", 0xd99c99e0db1388e4ULL}, // 83 bytes; ret=1 "Got room ID 1"
    {"console:room.get 0", 0x15f46a220dc04dbaULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get 511", 0x15f46a220dc04dbaULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get $ROOM ; room.health", 0xbc9d37ce4b6afc99ULL}, // 138 bytes; ret=1 "Got room ID 1" ; ret=1 "Room ID 1 health: 18"
    {"console:room.get $ROOM ; room.health 10", 0xe22fc3a0fe198e8eULL}, // 84 bytes; ret=1 "Got room ID 1" ; ret=1
    {"console:room.health", 0x61f8ca968da48ea0ULL}, // 97 bytes; ret=0 "no thing selected or not room"
    {"console:bug.toggle", 0x7cf3b5505a9b6687ULL}, // 99 bytes; ret=0 "require parameter 1 as bug name"
    {"console:bug.toggle 3", 0x295854c67a9a0223ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle RESURRECT_FOREVER", 0x9b97c1d0b3bba5d7ULL}, // 94 bytes; ret=1 "RESURRECT_FOREVER enabled"
    {"console:bug.toggle CLAIM_ROOM_ALL_THINGS", 0x295854c67a9a0223ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle ALWAYS_TUNNEL_TO_RED", 0xede472cb8e7cd2bfULL}, // 98 bytes; ret=1 "ALWAYS_TUNNEL_TO_RED enabled"
    {"console:bug.toggle 19", 0x5a052b1e411d4309ULL}, // 87 bytes; ret=0 "bug [19] is invalid"
    {"console:bug.toggle 0", 0x796c34b9f2e77c47ULL}, // 87 bytes; ret=0 "bug [0] is invalid"
    {"console:bug.toggle notabug", 0xf49e74c69ea6e0e6ULL}, // 92 bytes; ret=0 "bug [notabug] is invalid"
    {"console:music.set 2", 0x1612117a1eb43161ULL}, // 2 bytes; ret=0
    {"console:music.set", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:sound.test 1", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:sound.test", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:speech.test 1", 0x65e836e8293cbdb2ULL}, // 1 bytes; ret=1
    {"console:speech.test", 0xf9e21245993780daULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:toggle.tooltip.land.coord ; toggle.tooltip.land.coord", 0xcc7dfb511089be25ULL}, // 147 bytes; ret=1 "tooltip_land_coord is turned on" ; ret=1 "tooltip_land_coord is turned off"
    {"console:toggle.lights", 0xb84067ab77af59ddULL}, // 4 bytes; ret=1
    {"console:lua PLAYER0.MONEY = 7", 0x4e4e8823a226988eULL}, // 5 bytes; ret=1
    {"console:lua not valid lua(", 0x3461db84d460ffb9ULL}, // 80 bytes; ret=1 "lua error, see log"
    {"console:cheat.menu 1 ; cheat.menu 0", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 2 ; cheat.menu 0", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu instance ; cheat.menu none", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu secondary ; cheat.menu 0", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 9", 0xee5846cca59cf202ULL}, // 128 bytes; ret=0 "menu type is invalid. possible values: 0/1/2/3/4, none/main/cre"
    {"console:cheat.menu", 0x109d2f9add816930ULL}, // 100 bytes; ret=0 "require parameter 1 as menu type"
    {"console:dbc ; dbc", 0x9c1d016cebf01476ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:resync", 0x4d2390c6df2c9e64ULL}, // 4 bytes; ret=1
