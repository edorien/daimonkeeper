    {"console:", 0x7d1331405764b6aaULL}, // 85 bytes; ret=0 "command is empty"
    {"console:notacommand", 0xcacf637f858cf68dULL}, // 87 bytes; ret=0 "unsupported command"
    {"console:STATS", 0x804f3b35f1a89c76ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-stats", 0x222fc997d7c89db0ULL}, // 95 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-reveal", 0xb2bc44598eb5aad1ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-notacommand", 0x8e283c3e34d25158ULL}, // 0 bytes; ret=0
    {"console:-thing.get $C0", 0xb2bc44598eb5aad1ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-room.get $ROOM", 0xb2bc44598eb5aad1ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:stats", 0x804f3b35f1a89c76ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:fps", 0xedfb50d2dc1dfe79ULL}, // 93 bytes; ret=1 "Framerate/Turn is 20 fps"
    {"console:fps 30", 0x0ff1cf73be80123eULL}, // 2 bytes; ret=1
    {"console:fps.draw 60", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:ft", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:frametime.max", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:ft.max ; ft.max", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:netstats ; netstats", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:quit", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:time", 0xb83442132ae6c69cULL}, // 83 bytes; ret=1 "Time: 00:00:01"
    {"console:time 1200 20", 0xa4ac165f6b062d3eULL}, // 83 bytes; ret=1 "Time: 00:01:00"
    {"console:timer.toggle", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:timer.switch", 0x7a3c936936d69226ULL}, // 2 bytes; ret=1
    {"console:turn", 0x5ab104140be1459bULL}, // 76 bytes; ret=1 "turn 30"
    {"console:pause", 0x361f0dac80a974cfULL}, // 2 bytes; ret=1
    {"console:pause ; pause", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:step", 0xf6762007e7266565ULL}, // 2 bytes; ret=1
    {"console:game.save 9 golden ; game.load 9", 0x1843821abafec4adULL}, // 0 bytes; ret=1 ; ret=1
    {"console:game.save -1", 0x27202f9c24f2586eULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load -1", 0x27202f9c24f2586eULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load 9999", 0xee3bfd27f09d78a5ULL}, // 92 bytes; ret=0 "Unable to load game 9999"
    {"console:cls", 0x5989f5dfe12e6c43ULL}, // 59 bytes; ret=1
    {"console:ver", 0x6dadef842c256cb2ULL}, // 114 bytes; ret=1 "dAImon Keeper 1.0.0 - KFX 1.4 (ver <version>)"
    {"console:volume", 0x516a315304657e19ULL}, // 89 bytes; ret=1 "Sound FX: 50 Music: 35"
    {"console:volume.sound", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:volume.sound abc", 0xb3261a5853301ed2ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:volume.sound 50", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:volume.sfx 40", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:volume.music 30", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:volume.soundtrack abc", 0xb3261a5853301ed2ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:compuchat", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:compuchat scarce", 0x4aac664129a101ddULL}, // 80 bytes; ret=1 "Ai model 7"
    {"console:compuchat 2", 0x08c6d89a906bdd3bULL}, // 71 bytes; ret=1 "2"
    {"console:compuchat none", 0x3c6e0c8b701f981dULL}, // 73 bytes; ret=1 "none"
    {"console:comp.procs 1 ; cheat.menu 0", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.events 1 ; cheat.menu 0", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.checks 1 ; cheat.menu 0", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.procs", 0xe28add290161cefeULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.events 9", 0x011eaac937d880e5ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.checks -1", 0x8321bab7ec588eb0ULL}, // 96 bytes; ret=0 "player idx [-1] exceeds [0,8]"
    {"console:comp.kill 1", 0x49ff4a7865934135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill 2", 0x49ff4a7865934135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill", 0xe28add290161cefeULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.kill 99", 0x397be74d86835124ULL}, // 96 bytes; ret=0 "player idx [99] exceeds [0,8]"
    {"console:comp.kill 9", 0x011eaac937d880e5ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.me 1", 0x7dcacfe1eb2f31e4ULL}, // 1021 bytes; ret=1 "computer assistant is 1"
    {"console:comp.me", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:spectate", 0x659261ca93bfecc9ULL}, // 21721 bytes; ret=1 "player 0 is now AI-controlled; map revealed"
    {"console:spectate 1", 0x0ac8533418012115ULL}, // 94 bytes; ret=0 "unable to spectate player 1"
    {"console:reveal", 0xa23672c57ceead9cULL}, // 21610 bytes; ret=1
    {"console:reveal 10", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:conceal", 0x2be574ebe99e3a4aULL}, // 7228 bytes; ret=1
    {"console:conceal 10", 0x9521cf665508e1f7ULL}, // 145 bytes; ret=1
    {"console:slab.place lava", 0x993452f73ba14b2cULL}, // 2136 bytes; ret=1
    {"console:slab.place claimed", 0x71e37e5c31dc5cebULL}, // 2113 bytes; ret=1
    {"console:slab.place rock", 0x1be9d3986dceeb08ULL}, // 2169 bytes; ret=1
    {"console:slab.place gold 1", 0xc07f05b3704a4eb2ULL}, // 2170 bytes; ret=1
    {"console:slab.place temple", 0xba77324506508d92ULL}, // 2122 bytes; ret=1
    {"console:slab.place 10", 0x5f9305dfe42ba3bbULL}, // 2110 bytes; ret=1
    {"console:slab.place 99", 0xcf0218daef7a8c78ULL}, // 94 bytes; ret=0 "slbkind [99] exceeds [0,62]"
    {"console:slab.place notaslab", 0xceb882479c1a35fcULL}, // 94 bytes; ret=0 "slbkind [-1] exceeds [0,62]"
    {"console:slab.place", 0xc7e519d8fa501929ULL}, // 98 bytes; ret=0 "require parameter 1 as slbkind"
    {"console:slab.place earth", 0xb88326e751d940eaULL}, // 2170 bytes; ret=1
    {"console:slab.place reinforced", 0x98baa73dc0b407ccULL}, // 2171 bytes; ret=1
    {"console:slab.place fortified", 0x98baa73dc0b407ccULL}, // 2171 bytes; ret=1
    {"console:slab.health", 0xd3cd715974f43ea5ULL}, // 83 bytes; ret=1 "Slab health: 20"
    {"console:slab.health 100", 0xca40440af4623603ULL}, // 2 bytes; ret=1
    {"console:look treasure", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:look troll", 0x5d7d667b143f85d0ULL}, // 2 bytes; ret=1
    {"console:look 1", 0xce62dee2a672806fULL}, // 19 bytes; ret=1
    {"console:look notalocation", 0x73a839df6aa4a3a9ULL}, // 91 bytes; ret=0 "subtile coord is invalid"
    {"console:look", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:zoomto.slabcoord 10 10", 0x6fbe17924a0d1c47ULL}, // 31 bytes; ret=1
    {"console:zoomto.slabcoord 10", 0xaab27153830b9a13ULL}, // 96 bytes; ret=0 "require parameter 2 as slb_y"
    {"console:zoomto.slabcoord", 0xa94bf1041dacbbc7ULL}, // 96 bytes; ret=0 "require parameter 1 as slb_x"
    {"console:zoomto.slabcoord 999 999", 0x6ce6b6acfeaed61bULL}, // 107 bytes; ret=0 "Slab coordinates specified are invalid"
    {"console:zoomto.subtilecoord 40 40", 0x72c5d0924a0d1c47ULL}, // 31 bytes; ret=1
    {"console:zoomto.subtilecoord 40", 0xafe6127040fc8ad1ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:zoomto.subtilecoord 999 1", 0x03aa9baca1d6fdc8ULL}, // 108 bytes; ret=0 "Subtile coordinates specified are invalid"
    {"console:actionpoint.pos 1", 0x5ae1ce5d56f66795ULL}, // 105 bytes; ret=1 "Action Point idx: 1 num: 1 X: 225 Y: 130"
    {"console:actionpoint.pos 99", 0x96b769a692870460ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.pos", 0x317c1027de9457dfULL}, // 108 bytes; ret=0 "require parameter 1 as actionpoint number"
    {"console:actionpoint.zoomto 1", 0xce62dee2a672806fULL}, // 19 bytes; ret=1
    {"console:zoomto.actionpoint 99", 0x96b769a692870460ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.reset 1", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:actionpoint.reset 1 0", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:herogate.zoomto 1", 0x3846fe7aa2aa1dc8ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:zoomto.herogate 9", 0x3846fe7aa2aa1dc8ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:herogate.zoomto", 0x58241681bc09bd9dULL}, // 107 bytes; ret=0 "require parameter 1 as hero gate number"
    {"console:mapwho.info", 0xb217df0f86fd9716ULL}, // 247 bytes; ret=1 "first_thing:19 creature IMP" "flags: 02,filled: 9, wib: 1, col: 0532" "mapwho: 0019, rev: 1" "stl_x: 126, stl_y:1
    {"console:mapwho.info 40 40", 0x3381e90b46b14076ULL}, // 251 bytes; ret=1 "first_thing:0 EMPTY model 0" "flags: 00,filled: 12, wib: 2, col: 0393" "mapwho: 0000, rev: 0" "stl_x: 40, stl_y:4
    {"console:mapwho.info 40", 0xafe6127040fc8ad1ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:mapwho.info 999 999", 0xec3883ce45f0578cULL}, // 83 bytes; ret=0 "invalid location"
    {"console:cursor.pos", 0xd09592bcc67ddeacULL}, // 90 bytes; ret=1 "Cursor at 126, 131, 3"
    {"console:player.score", 0x8187e1c92ddb9943ULL}, // 88 bytes; ret=1 "Player 0 score: 436"
    {"console:player.score 1", 0xf321a6241c212372ULL}, // 86 bytes; ret=1 "Player 1 score: 0"
    {"console:player.score 99", 0x844bb22c5b8f0e5aULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.flag 0 3", 0x000b11654d99c39aULL}, // 93 bytes; ret=1 "Player 0 flag 3 value: 0"
    {"console:player.flag 0 3 9 ; player.flag 0 3", 0x1455b22e4a749850ULL}, // 94 bytes; ret=1 ; ret=1 "Player 0 flag 3 value: 9"
    {"console:player.flag 0 99", 0xcef9d7ea80e5f376ULL}, // 93 bytes; ret=0 "script flag [99] exceeds"
    {"console:player.flag 99", 0x844bb22c5b8f0e5aULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.heart.health", 0xc589e427fb55873fULL}, // 114 bytes; ret=1 "Player 0 heart health: 30000 (100.00 per cent)"
    {"console:player.heart.health 0 1000", 0x5b8ccf5f3ea8cbdeULL}, // 3 bytes; ret=1
    {"console:player.heart.health 1", 0x49ff4a7865934135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.heart.health 0 0", 0xded6a1773ea08c10ULL}, // 5 bytes; ret=1
    {"console:player.heart.health 99", 0x49ff4a7865934135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.gold.add 0 1000", 0x79357b834086142bULL}, // 5 bytes; ret=1
    {"console:player.addgold 1 500", 0x5d41531938f2a0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.gold.add 0", 0xc78636edaef22997ULL}, // 101 bytes; ret=0 "require parameter 2 as gold amount"
    {"console:player.gold.add 99 1", 0x5d41531938f2a0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.colour 0 blue", 0x59dffb3d5e9c59afULL}, // 2013 bytes; ret=1
    {"console:player.color 1 green", 0xb1516c8dee755407ULL}, // 2 bytes; ret=1
    {"console:player.colour 0", 0xc826965721789337ULL}, // 96 bytes; ret=0 "require parameter 2 as color"
    {"console:player.colour", 0xff3e12ffa2e914ddULL}, // 97 bytes; ret=0 "require parameter 1 as player"
    {"console:player.colour 0 notacolour", 0xef66cc8f535b8019ULL}, // 85 bytes; ret=0 "colour is invalid"
    {"console:possession.lock", 0x7c95a16cc75516dfULL}, // 2 bytes; ret=1
    {"console:possession.unlock", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:string.show 5", 0xb8747e64a1206286ULL}, // 23 bytes; ret=1
    {"console:string.show", 0xf9a6333157c62e36ULL}, // 15 bytes; ret=1
    {"console:quick.show 5", 0x4fb88864a1206286ULL}, // 23 bytes; ret=1
    {"console:quick.show -1", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:give.trap boulder 2", 0xc365bdffd6b2fae7ULL}, // 77 bytes; ret=1 "done!"
    {"console:trap.give gas", 0x31c7aec692d3d55dULL}, // 77 bytes; ret=1 "done!"
    {"console:give.trap 99", 0x4026169ca5f8f6edULL}, // 100 bytes; ret=0 "trap number [99] exceeds (0,10]"
    {"console:give.trap", 0xdcc06e8954f97181ULL}, // 100 bytes; ret=0 "trap number [-1] exceeds (0,10]"
    {"console:give.door wooden", 0x2d4fae80aa50daa7ULL}, // 77 bytes; ret=1 "done!"
    {"console:door.give braced 3", 0x14e8bcfe15ab819eULL}, // 77 bytes; ret=1 "done!"
    {"console:give.door 99", 0x614c5f5c29ade229ULL}, // 100 bytes; ret=0 "door number [99] exceeds (0, 7])"
    {"console:give.door", 0x88ecc816edc7f055ULL}, // 100 bytes; ret=0 "door number [-1] exceeds (0, 7])"
    {"console:room.available temple", 0x4033a3992fe8f416ULL}, // 2 bytes; ret=1
    {"console:room.available all", 0x71afb964534cad25ULL}, // 6 bytes; ret=1
    {"console:room.available temple 0", 0x69f9ac8e08a09516ULL}, // 2 bytes; ret=1
    {"console:room.available hatchery", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:room.available guard 1 1", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:room.available", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give all", 0x6163d166b87d0cc4ULL}, // 118 bytes; ret=1
    {"console:spell.give POWER_LIGHTNING", 0x2f27b573d53db6d0ULL}, // 2 bytes; ret=1
    {"console:power.give", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give 999", 0xd03aee8ff0ab82d3ULL}, // 83 bytes; ret=0 "power is invalid"
    {"console:creature.available troll", 0x5a00909e2e4fad90ULL}, // 2 bytes; ret=1
    {"console:creature.available troll 0", 0x6a188caebe502a10ULL}, // 2 bytes; ret=1
    {"console:creature.available notacreature", 0x4a9c7777fca6fafeULL}, // 1 bytes; ret=0
    {"console:magic.instance imp 1 fireball", 0x3949f02cddd2a287ULL}, // 6 bytes; ret=1
    {"console:magic.instance imp 1 2", 0xb8ebba439489ee1bULL}, // 6 bytes; ret=1
    {"console:magic.instance", 0x3249c3b9ae5464f6ULL}, // 97 bytes; ret=0 "require parameter 1 as creature"
    {"console:magic.instance imp", 0x2bc2a23e5c35689cULL}, // 95 bytes; ret=0 "require parameter 2 as slot"
    {"console:magic.instance imp 1", 0xeb925bf63eecd187ULL}, // 98 bytes; ret=0 "require parameter 3 as instance"
    {"console:magic.instance notacreature 1 fireball", 0x6451b1a9c7e7639dULL}, // 84 bytes; ret=0 "Invalid creature"
    {"console:magic.instance imp 12 fireball", 0x2eaf308bc6f2c184ULL}, // 80 bytes; ret=0 "Invalid slot"
    {"console:magic.instance imp 1 0", 0xacc73842b5b8da7bULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:magic.instance imp 1 9999", 0xacc73842b5b8da7bULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:map.pool", 0x5c4bb24215f0992dULL}, // 101 bytes; ret=1 "require parameter 1 as creature model"
    {"console:creature.pool troll", 0x576cb71090f3c8c9ULL}, // 89 bytes; ret=1 "Pool count troll: 20"
    {"console:map.pool troll 7", 0xbc24730523d90404ULL}, // 93 bytes; ret=1 "Set troll pool count: 7"
    {"console:map.pool troll -1", 0x33f634b2b8744aacULL}, // 93 bytes; ret=0 "Set troll pool count: -1"
    {"console:map.pool notacreature", 0xbc76af772eb53e07ULL}, // 95 bytes; ret=0 "Invalid creature: notacreature"
    {"console:creature.pool.add troll 3", 0xe5aebddb6bba3fd5ULL}, // 2 bytes; ret=1
    {"console:creature.pool.sub troll 1", 0xa2d741db6bba3fd5ULL}, // 2 bytes; ret=1
    {"console:creature.pool.remove troll 1", 0xa2d741db6bba3fd5ULL}, // 2 bytes; ret=1
    {"console:creature.pool.add troll", 0x14ac13e2d483d272ULL}, // 104 bytes; ret=0 "require parameter 2 as creature amount"
    {"console:creature.pool.add 0 3", 0xf1964fa9bb8ed8c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add any 5", 0xf1964fa9bb8ed8c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add evil 5", 0xf1964fa9bb8ed8c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.sub 500 1", 0xf1964fa9bb8ed8c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:gold.create 500", 0x6ff7ad00fc5bf3fcULL}, // 48 bytes; ret=1
    {"console:create.gold", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:gold.create abc", 0xb3261a5853301ed2ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:object.create barrel", 0x52cb73c6f402c617ULL}, // 44 bytes; ret=1
    {"console:create.object barrel 1", 0xb436d8c6f402c617ULL}, // 44 bytes; ret=1
    {"console:object.create 5", 0x11c16c34cf4b44eeULL}, // 64 bytes; ret=1
    {"console:object.create", 0xbaa9b0e6583c3461ULL}, // 102 bytes; ret=0 "require parameter 1 as object model"
    {"console:object.create notanobject", 0x977896e2f360ce13ULL}, // 91 bytes; ret=0 "object model is invalid"
    {"console:creature.create troll", 0x2ea4dc0efb2100bbULL}, // 67 bytes; ret=1
    {"console:create.creature troll 5 2", 0xb3a5ccc426afaf09ULL}, // 129 bytes; ret=1
    {"console:creature.create troll 1 1 1", 0x0bc79b14e5f33121ULL}, // 65 bytes; ret=1
    {"console:creature.create evil", 0xdd74d3c365afd1abULL}, // 64 bytes; ret=1
    {"console:creature.create good", 0x67c4f020e444e2c4ULL}, // 67 bytes; ret=1
    {"console:creature.create any", 0x539f1da6ec330187ULL}, // 66 bytes; ret=1
    {"console:creature.create 999", 0x480621f01ed846feULL}, // 103 bytes; ret=0 "creature model [999] exceeds (0, 37)"
    {"console:creature.create notacreature", 0xef2f39ae56180304ULL}, // 103 bytes; ret=0 "creature model [-1] exceeds (0, 37)"
    {"console:creature.create", 0x78c5a6cef2e0291cULL}, // 101 bytes; ret=0 "require parameter 1 as creature model"
    {"console:thing.create object barrel", 0x7da4b5f9f83a1063ULL}, // 46 bytes; ret=1
    {"console:create.thing corpse troll", 0x7423cc202d9862a2ULL}, // 35 bytes; ret=1
    {"console:thing.create creature imp", 0x71ae52cf1fa6578dULL}, // 66 bytes; ret=1
    {"console:thing.create trap boulder", 0x59d0ac764ed989d3ULL}, // 39 bytes; ret=1
    {"console:thing.create effect EFFECT_EXPLOSION_1", 0x00b9c6d41abad2b3ULL}, // 46 bytes; ret=1
    {"console:thing.create shot SHOT_FIREBALL", 0xc87c125640c080f6ULL}, // 60 bytes; ret=1
    {"console:thing.create 1 5", 0xcd8c5a9777118689ULL}, // 66 bytes; ret=1
    {"console:thing.create nonsense x", 0xaae656c5988536bcULL}, // 83 bytes; ret=0 "class is invalid"
    {"console:thing.create object", 0x36a33195f24d8e39ULL}, // 96 bytes; ret=0 "require parameter 2 as model"
    {"console:thing.create", 0xb406c9e51587c481ULL}, // 95 bytes; ret=0 "require parameter 1 as class"
    {"console:thing.get", 0x273b682318f485daULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get $C0", 0x273b682318f485daULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get 0", 0xa550f94211a1d4faULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.get $HEART", 0x820af71f094dbb17ULL}, // 106 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER"
    {"console:thing.get $HERO", 0xa550f94211a1d4faULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.info", 0x60189b417b54261dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.info", 0x799d83a2c3890188ULL}, // 292 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "next_on_map: 0, next_of_class: 0" "health: 75" "pos: 126 131 3" "creature
    {"console:thing.get $HEART ; thing.info", 0x40ef6179d9618928ULL}, // 294 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1 "next_on_map: 0, next_of_class: 10" "health: 30000" "pos: 127 130
    {"console:thing.show_id ; thing.show_id 0", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:thing.get $C0 ; thing.health", 0x7e4b9afca986c0d0ULL}, // 143 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "Thing ID: 19 health: 75"
    {"console:thing.get $C0 ; thing.health 5", 0x45e1008f12501594ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.health", 0x60189b417b54261dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.move 40 40", 0x64cf26e688b7e976ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 40 40 3", 0x8f56a4bee2aaa935ULL}, // 103 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move", 0x8386fb96d03fe6feULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 999 999", 0xb5e725b8b6ec9b97ULL}, // 144 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "invalid location"
    {"console:thing.move", 0x60189b417b54261dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.destroy", 0x60189b417b54261dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get 0 ; thing.destroy", 0xa082f0ea28ae0ea3ULL}, // 151 bytes; ret=0 "thing is invalid" ; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.destroy", 0x3eb88abe5e4f1272ULL}, // 204 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $HEART ; thing.destroy", 0x3afab33e3e6f2e3eULL}, // 177 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1
    {"console:creature.addhealth 10", 0xbfcb1d53be62ef7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.addhealth 10", 0xfd73708f12501594ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.health.sub 10", 0xb37ebc8f12501594ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.subhealth abc", 0x9bb501c2a3570aa7ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "parameter 1 requires a number"
    {"console:creature.health.add", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:thing.get $C0 ; creature.level 5", 0x98e40b2e82b78693ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.level", 0x56bb3b555d5f5d52ULL}, // 102 bytes; ret=0 "require parameter 1 as creature level"
    {"console:creature.level 5", 0xbfcb1d53be62ef7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.freeze", 0x083cdfda69f333a5ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.slow", 0x8abe306009afbde6ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.freeze", 0xbfcb1d53be62ef7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.chicken", 0x6acadf7d84ef08e9ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.chicken", 0xbfcb1d53be62ef7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.instance.set 2", 0x7fbf0f07b548ac00ULL}, // 101 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.instance.set", 0xabec76e62bd92a79ULL}, // 98 bytes; ret=0 "require parameter 1 as instance"
    {"console:creature.instance.set 2", 0xbfcb1d53be62ef7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.state.set 1", 0x4282570dcbefd0ffULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.state.set 0", 0x03809e0dcbefd0ffULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.state.set", 0x4a627ba6d2a96772ULL}, // 96 bytes; ret=0 "require parameter 1 as state"
    {"console:thing.get $C0 ; creature.job.set 1", 0x3eef7095e401cd01ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 1."
    {"console:thing.get $C0 ; creature.job.set 3", 0xc8c598bd35eeaa97ULL}, // 124 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.job.set", 0x065154f61adecfe3ULL}, // 94 bytes; ret=0 "require parameter 1 as job"
    {"console:thing.get $C0 ; creature.job.set 70", 0x123c5899f73f61bfULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 70."
    {"console:thing.get $C0 ; creature.attackheart 1", 0x8fd4fe0e4ca423ffULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:thing.get $C0 ; creature.attackheart 99", 0x8fd4fe0e4ca423ffULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:creature.attackheart", 0x3a66a11f55ac7ce5ULL}, // 104 bytes; ret=0 "require parameter 1 as player number"
    {"console:creature.attackheart 1", 0xbfcb1d53be62ef7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:digger.sendto", 0xbed626f97551f063ULL}, // 109 bytes; ret=1 "creature IMP 22 will dig to PLAYER0"
    {"console:digger.sendto 1", 0x5d41531938f2a0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:thing.get $C0 ; digger.sendto 0", 0xe1138c7fa07502abULL}, // 157 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "creature IMP 19 will dig to PLAYER0"
    {"console:digger.sendto 99", 0x5d41531938f2a0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:room.get", 0xa80289085e01ef14ULL}, // 83 bytes; ret=1 "Got room ID 7"
    {"console:room.get $ROOM", 0x50cda3087b7e466eULL}, // 83 bytes; ret=1 "Got room ID 1"
    {"console:room.get 0", 0x9ade80493c264920ULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get 511", 0x9ade80493c264920ULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get $ROOM ; room.health", 0x3ad42a4eda66f1d0ULL}, // 138 bytes; ret=1 "Got room ID 1" ; ret=1 "Room ID 1 health: 18"
    {"console:room.get $ROOM ; room.health 10", 0x23d0132cbf340ae0ULL}, // 84 bytes; ret=1 "Got room ID 1" ; ret=1
    {"console:room.health", 0x32448e219424b281ULL}, // 97 bytes; ret=0 "no thing selected or not room"
    {"console:bug.toggle", 0xa222cbebc4666ac4ULL}, // 99 bytes; ret=0 "require parameter 1 as bug name"
    {"console:bug.toggle 3", 0xa6ea0cfa6b996759ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle RESURRECT_FOREVER", 0xaf9059d61645d2ceULL}, // 94 bytes; ret=1 "RESURRECT_FOREVER enabled"
    {"console:bug.toggle CLAIM_ROOM_ALL_THINGS", 0xa6ea0cfa6b996759ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle ALWAYS_TUNNEL_TO_RED", 0x612cafdefad24602ULL}, // 98 bytes; ret=1 "ALWAYS_TUNNEL_TO_RED enabled"
    {"console:bug.toggle 19", 0xaa115e062f9b7067ULL}, // 94 bytes; ret=1 "NEUTRAL_ROOM_JOBS enabled"
    {"console:bug.toggle 20", 0x2a6cdbe6cc739174ULL}, // 87 bytes; ret=0 "bug [20] is invalid"
    {"console:bug.toggle 0", 0xf28db4b12b43c2d8ULL}, // 87 bytes; ret=0 "bug [0] is invalid"
    {"console:bug.toggle notabug", 0x8d5fc4df715584e6ULL}, // 92 bytes; ret=0 "bug [notabug] is invalid"
    {"console:music.set 2", 0x1bb433a525b55dc6ULL}, // 2 bytes; ret=0
    {"console:music.set", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:sound.test 1", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:sound.test", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:speech.test 1", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
    {"console:speech.test", 0xcb182335639bfae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:toggle.tooltip.land.coord ; toggle.tooltip.land.coord", 0x2e36d84d9212464aULL}, // 147 bytes; ret=1 "tooltip_land_coord is turned on" ; ret=1 "tooltip_land_coord is turned off"
    {"console:toggle.lights", 0x313c0c2cb4cf6b75ULL}, // 4 bytes; ret=1
    {"console:lua PLAYER0.MONEY = 7", 0x72bff1834086142bULL}, // 5 bytes; ret=1
    {"console:lua not valid lua(", 0x2e8bedbc395110d5ULL}, // 80 bytes; ret=1 "lua error, see log"
    {"console:cheat.menu 1 ; cheat.menu 0", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 2 ; cheat.menu 0", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu instance ; cheat.menu none", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu secondary ; cheat.menu 0", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 9", 0x959acc22231298c0ULL}, // 128 bytes; ret=0 "menu type is invalid. possible values: 0/1/2/3/4, none/main/cre"
    {"console:cheat.menu", 0x4044ed77de316ff2ULL}, // 100 bytes; ret=0 "require parameter 1 as menu type"
    {"console:dbc ; dbc", 0x289a3484641e7a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:resync", 0x4a9a7777fca67827ULL}, // 1 bytes; ret=1
