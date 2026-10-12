    {"console:", 0x2f1d0f1f27076492ULL}, // 85 bytes; ret=0 "command is empty"
    {"console:notacommand", 0x09f5ec6d736d1e85ULL}, // 87 bytes; ret=0 "unsupported command"
    {"console:STATS", 0xb9bed21eeba697d6ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-stats", 0xbe7624b5f59994a8ULL}, // 95 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-reveal", 0xccd8168803a861b9ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-notacommand", 0x8e283c3e34d25158ULL}, // 0 bytes; ret=0
    {"console:-thing.get $C0", 0xccd8168803a861b9ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-room.get $ROOM", 0xccd8168803a861b9ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:stats", 0xb9bed21eeba697d6ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:fps", 0x2b4164a1889153f1ULL}, // 93 bytes; ret=1 "Framerate/Turn is 20 fps"
    {"console:fps 30", 0xd79787875ca3439eULL}, // 2 bytes; ret=1
    {"console:fps.draw 60", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:ft", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:frametime.max", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:ft.max ; ft.max", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:netstats ; netstats", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:quit", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:time", 0x52ef1f20efd64564ULL}, // 83 bytes; ret=1 "Time: 00:00:01"
    {"console:time 1200 20", 0xfed0880409ab5ce6ULL}, // 83 bytes; ret=1 "Time: 00:01:00"
    {"console:timer.toggle", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:timer.switch", 0x05fe4d7510e39bb6ULL}, // 2 bytes; ret=1
    {"console:turn", 0x810751d22ba4766bULL}, // 76 bytes; ret=1 "turn 30"
    {"console:pause", 0xf283d0357b6eacbfULL}, // 2 bytes; ret=1
    {"console:pause ; pause", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:step", 0x9010fc32b2d41dddULL}, // 2 bytes; ret=1
    {"console:game.save 9 golden ; game.load 9", 0x1843821abafec4adULL}, // 0 bytes; ret=1 ; ret=1
    {"console:game.save -1", 0x26614b97a3b3abfeULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load -1", 0x26614b97a3b3abfeULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load 9999", 0x097dcdb6c30a6685ULL}, // 92 bytes; ret=0 "Unable to load game 9999"
    {"console:cls", 0x1d6c7a10301bd75bULL}, // 59 bytes; ret=1
    {"console:ver", 0x0a6da45446361cc2ULL}, // 114 bytes; ret=1 "dAImon Keeper 1.0.0 - KFX 1.4 (ver <version>)"
    {"console:volume", 0x17f7b7811d27a771ULL}, // 89 bytes; ret=1 "Sound FX: 50 Music: 35"
    {"console:volume.sound", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:volume.sound abc", 0x0d5f1a740db2dbaaULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:volume.sound 50", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:volume.sfx 40", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:volume.music 30", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:volume.soundtrack abc", 0x0d5f1a740db2dbaaULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:compuchat", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:compuchat scarce", 0x706d98329cdab9ddULL}, // 80 bytes; ret=1 "Ai model 7"
    {"console:compuchat 2", 0xf02d5d19336ba8d3ULL}, // 71 bytes; ret=1 "2"
    {"console:compuchat none", 0xb525f75f8370ae35ULL}, // 73 bytes; ret=1 "none"
    {"console:comp.procs 1 ; cheat.menu 0", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.events 1 ; cheat.menu 0", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.checks 1 ; cheat.menu 0", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.procs", 0x759029715db39ca6ULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.events 9", 0x494bfa4a1fd1d405ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.checks -1", 0x43935d84e97ed770ULL}, // 96 bytes; ret=0 "player idx [-1] exceeds [0,8]"
    {"console:comp.kill 1", 0x512b65085afc1f5dULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill 2", 0x512b65085afc1f5dULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill", 0x759029715db39ca6ULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.kill 99", 0x8bd721a8e1ba3be4ULL}, // 96 bytes; ret=0 "player idx [99] exceeds [0,8]"
    {"console:comp.kill 9", 0x494bfa4a1fd1d405ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.me 1", 0x99a296890b17bc5cULL}, // 1021 bytes; ret=1 "computer assistant is 1"
    {"console:comp.me", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:spectate", 0x11cd5e734e75bf31ULL}, // 21721 bytes; ret=1 "player 0 is now AI-controlled; map revealed"
    {"console:spectate 1", 0x36481e936e311525ULL}, // 94 bytes; ret=0 "unable to spectate player 1"
    {"console:reveal", 0xdda12dd8b9028f74ULL}, // 21610 bytes; ret=1
    {"console:reveal 10", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:conceal", 0xd9911a17b1f39ba2ULL}, // 7228 bytes; ret=1
    {"console:conceal 10", 0x7129d519e5609e8fULL}, // 145 bytes; ret=1
    {"console:slab.place lava", 0x0614439f7f80e672ULL}, // 2136 bytes; ret=1
    {"console:slab.place claimed", 0x4eaaa620c806e0f5ULL}, // 2113 bytes; ret=1
    {"console:slab.place rock", 0x9f159d4cbed7fa31ULL}, // 2169 bytes; ret=1
    {"console:slab.place gold 1", 0x8277b80b017edc93ULL}, // 2170 bytes; ret=1
    {"console:slab.place temple", 0x774626e19e0e170cULL}, // 2122 bytes; ret=1
    {"console:slab.place 10", 0x21f3ba95e0608cc5ULL}, // 2110 bytes; ret=1
    {"console:slab.place 99", 0x7a5563fd13be3308ULL}, // 94 bytes; ret=0 "slbkind [99] exceeds [0,62]"
    {"console:slab.place notaslab", 0x63d8d8bc608fe92cULL}, // 94 bytes; ret=0 "slbkind [-1] exceeds [0,62]"
    {"console:slab.place", 0x977c20fd4e094719ULL}, // 98 bytes; ret=0 "require parameter 1 as slbkind"
    {"console:slab.place earth", 0xea1ee8bfb455f9fbULL}, // 2170 bytes; ret=1
    {"console:slab.place reinforced", 0x75a221d4735cc2e5ULL}, // 2171 bytes; ret=1
    {"console:slab.place fortified", 0x75a221d4735cc2e5ULL}, // 2171 bytes; ret=1
    {"console:slab.health", 0x9f178676b7c2491dULL}, // 83 bytes; ret=1 "Slab health: 20"
    {"console:slab.health 100", 0xf0147bea2c2f705bULL}, // 2 bytes; ret=1
    {"console:look treasure", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:look troll", 0xdd630ab4fab41f38ULL}, // 2 bytes; ret=1
    {"console:look 1", 0xe804b4b4b0969497ULL}, // 19 bytes; ret=1
    {"console:look notalocation", 0x3f569b68600df321ULL}, // 91 bytes; ret=0 "subtile coord is invalid"
    {"console:look", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:zoomto.slabcoord 10 10", 0xaf69906443d382bfULL}, // 31 bytes; ret=1
    {"console:zoomto.slabcoord 10", 0x45c5106b743281f3ULL}, // 96 bytes; ret=0 "require parameter 2 as slb_y"
    {"console:zoomto.slabcoord", 0x897df6251c94fb27ULL}, // 96 bytes; ret=0 "require parameter 1 as slb_x"
    {"console:zoomto.slabcoord 999 999", 0xc72e8e0a00769843ULL}, // 107 bytes; ret=0 "Slab coordinates specified are invalid"
    {"console:zoomto.subtilecoord 40 40", 0xda646b6443d382bfULL}, // 31 bytes; ret=1
    {"console:zoomto.subtilecoord 40", 0xd1b7058833935431ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:zoomto.subtilecoord 999 1", 0xbbd59468b56f1e38ULL}, // 108 bytes; ret=0 "Subtile coordinates specified are invalid"
    {"console:actionpoint.pos 1", 0x94a81e7cb7d20dbdULL}, // 105 bytes; ret=1 "Action Point idx: 1 num: 1 X: 225 Y: 130"
    {"console:actionpoint.pos 99", 0x13ee655964b89d68ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.pos", 0x9dedbe3b6391f96fULL}, // 108 bytes; ret=0 "require parameter 1 as actionpoint number"
    {"console:actionpoint.zoomto 1", 0xe804b4b4b0969497ULL}, // 19 bytes; ret=1
    {"console:zoomto.actionpoint 99", 0x13ee655964b89d68ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.reset 1", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:actionpoint.reset 1 0", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:herogate.zoomto 1", 0xad8f7ee49f5cc2d8ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:zoomto.herogate 9", 0xad8f7ee49f5cc2d8ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:herogate.zoomto", 0x0e98ecd812571f65ULL}, // 107 bytes; ret=0 "require parameter 1 as hero gate number"
    {"console:mapwho.info", 0xa05ddf75d11cbeceULL}, // 247 bytes; ret=1 "first_thing:19 creature IMP" "flags: 02,filled: 9, wib: 1, col: 0532" "mapwho: 0019, rev: 1" "stl_x: 126, stl_y:1
    {"console:mapwho.info 40 40", 0xbed690321876480eULL}, // 251 bytes; ret=1 "first_thing:0 EMPTY model 0" "flags: 00,filled: 12, wib: 2, col: 0393" "mapwho: 0000, rev: 0" "stl_x: 40, stl_y:4
    {"console:mapwho.info 40", 0xd1b7058833935431ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:mapwho.info 999 999", 0xabfd0f70c890ee44ULL}, // 83 bytes; ret=0 "invalid location"
    {"console:cursor.pos", 0xd6af3f7b58f2204cULL}, // 90 bytes; ret=1 "Cursor at 126, 131, 3"
    {"console:player.score", 0x1c2fb0333bf26cb3ULL}, // 88 bytes; ret=1 "Player 0 score: 436"
    {"console:player.score 1", 0xa802250714a85eb2ULL}, // 86 bytes; ret=1 "Player 1 score: 0"
    {"console:player.score 99", 0xbc0b003d50566662ULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.flag 0 3", 0xc6eb9fd2ede1bdc2ULL}, // 93 bytes; ret=1 "Player 0 flag 3 value: 0"
    {"console:player.flag 0 3 9 ; player.flag 0 3", 0xeae1fa962dcf4de8ULL}, // 94 bytes; ret=1 ; ret=1 "Player 0 flag 3 value: 9"
    {"console:player.flag 0 99", 0xd815845e9a3addfeULL}, // 93 bytes; ret=0 "script flag [99] exceeds"
    {"console:player.flag 99", 0xbc0b003d50566662ULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.heart.health", 0x8a4beca02a89160fULL}, // 114 bytes; ret=1 "Player 0 heart health: 30000 (100.00 per cent)"
    {"console:player.heart.health 0 1000", 0x913238cb715f315eULL}, // 3 bytes; ret=1
    {"console:player.heart.health 1", 0x512b65085afc1f5dULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.heart.health 0 0", 0xaad939708bef3310ULL}, // 5 bytes; ret=1
    {"console:player.heart.health 99", 0x512b65085afc1f5dULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.gold.add 0 1000", 0x0f09af6119d0c3b3ULL}, // 5 bytes; ret=1
    {"console:player.addgold 1 500", 0x19dd4b8c5f90e337ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.gold.add 0", 0x45015bd2b5d4c6dfULL}, // 101 bytes; ret=0 "require parameter 2 as gold amount"
    {"console:player.gold.add 99 1", 0x19dd4b8c5f90e337ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.colour 0 blue", 0x5e4a6fb2c18b38feULL}, // 2013 bytes; ret=1
    {"console:player.color 1 green", 0x264a1b97f069507fULL}, // 2 bytes; ret=1
    {"console:player.colour 0", 0x3a903d57112e74d7ULL}, // 96 bytes; ret=0 "require parameter 2 as color"
    {"console:player.colour", 0xb8dff2a74c7ea4f5ULL}, // 97 bytes; ret=0 "require parameter 1 as player"
    {"console:player.colour 0 notacolour", 0xd8a2d2ce7eb21a61ULL}, // 85 bytes; ret=0 "colour is invalid"
    {"console:possession.lock", 0x62f4d3e3355584a7ULL}, // 2 bytes; ret=1
    {"console:possession.unlock", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:string.show 5", 0x4f57259a702a3e86ULL}, // 23 bytes; ret=1
    {"console:string.show", 0x32f82d02d0238be6ULL}, // 15 bytes; ret=1
    {"console:quick.show 5", 0xf2388f9a702a3e86ULL}, // 23 bytes; ret=1
    {"console:quick.show -1", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:give.trap boulder 2", 0x73face3137cc0227ULL}, // 77 bytes; ret=1 "done!"
    {"console:trap.give gas", 0xbb111b56fd17594dULL}, // 77 bytes; ret=1 "done!"
    {"console:give.trap 99", 0x02e9120ccb29284dULL}, // 100 bytes; ret=0 "trap number [99] exceeds (0,10]"
    {"console:give.trap", 0x3fb688b55df2c1e1ULL}, // 100 bytes; ret=0 "trap number [-1] exceeds (0,10]"
    {"console:give.door wooden", 0x845d7a30c494d767ULL}, // 77 bytes; ret=1 "done!"
    {"console:door.give braced 3", 0x07be0276b7deebceULL}, // 77 bytes; ret=1 "done!"
    {"console:give.door 99", 0x02d221eae4e40ef9ULL}, // 100 bytes; ret=0 "door number [99] exceeds (0, 7])"
    {"console:give.door", 0xb6d960790ba0ed85ULL}, // 100 bytes; ret=0 "door number [-1] exceeds (0, 7])"
    {"console:room.available temple", 0x761d87c004a5bc5eULL}, // 2 bytes; ret=1
    {"console:room.available all", 0x2f331b221e6cb8fdULL}, // 6 bytes; ret=1
    {"console:room.available temple 0", 0x73f5207ba883dd5eULL}, // 2 bytes; ret=1
    {"console:room.available hatchery", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:room.available guard 1 1", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:room.available", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give all", 0x903f94e0c87e160cULL}, // 118 bytes; ret=1
    {"console:spell.give POWER_LIGHTNING", 0x75148cc9a192f038ULL}, // 2 bytes; ret=1
    {"console:power.give", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give 999", 0x4ec1b0bd942a439bULL}, // 83 bytes; ret=0 "power is invalid"
    {"console:creature.available troll", 0xbb17770c64680df8ULL}, // 2 bytes; ret=1
    {"console:creature.available troll 0", 0x10e06c1e623b4a78ULL}, // 2 bytes; ret=1
    {"console:creature.available notacreature", 0x04a1339913c2e336ULL}, // 1 bytes; ret=0
    {"console:magic.instance imp 1 fireball", 0x82fa5e1d3685bcdfULL}, // 6 bytes; ret=1
    {"console:magic.instance imp 1 2", 0x68157a3045aae393ULL}, // 6 bytes; ret=1
    {"console:magic.instance", 0xd954fb86e646803eULL}, // 97 bytes; ret=0 "require parameter 1 as creature"
    {"console:magic.instance imp", 0x2a33980d95576ac4ULL}, // 95 bytes; ret=0 "require parameter 2 as slot"
    {"console:magic.instance imp 1", 0x6861f923e266d677ULL}, // 98 bytes; ret=0 "require parameter 3 as instance"
    {"console:magic.instance notacreature 1 fireball", 0xaaa4f7792b615e9dULL}, // 84 bytes; ret=0 "Invalid creature"
    {"console:magic.instance imp 12 fireball", 0x10033b5c89488604ULL}, // 80 bytes; ret=0 "Invalid slot"
    {"console:magic.instance imp 1 0", 0xdf26fa121932d57bULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:magic.instance imp 1 9999", 0xdf26fa121932d57bULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:map.pool", 0x67d13aba1c252df5ULL}, // 101 bytes; ret=1 "require parameter 1 as creature model"
    {"console:creature.pool troll", 0xb8a4354f5d7c6b81ULL}, // 89 bytes; ret=1 "Pool count troll: 20"
    {"console:map.pool troll 7", 0x906a2dd5c5c1f4a4ULL}, // 93 bytes; ret=1 "Set troll pool count: 7"
    {"console:map.pool troll -1", 0x4a3ee0ffe8f51aa4ULL}, // 93 bytes; ret=0 "Set troll pool count: -1"
    {"console:map.pool notacreature", 0x3df53e0dc5c964afULL}, // 95 bytes; ret=0 "Invalid creature: notacreature"
    {"console:creature.pool.add troll 3", 0x65eea7bb775b0e4dULL}, // 2 bytes; ret=1
    {"console:creature.pool.sub troll 1", 0x23172bbb775b0e4dULL}, // 2 bytes; ret=1
    {"console:creature.pool.remove troll 1", 0x23172bbb775b0e4dULL}, // 2 bytes; ret=1
    {"console:creature.pool.add troll", 0xc8a6b2d043dd4be2ULL}, // 104 bytes; ret=0 "require parameter 2 as creature amount"
    {"console:creature.pool.add 0 3", 0xbc3a991159493a91ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add any 5", 0xbc3a991159493a91ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add evil 5", 0xbc3a991159493a91ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.sub 500 1", 0xbc3a991159493a91ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:gold.create 500", 0x4f5702212d2eb34eULL}, // 48 bytes; ret=1
    {"console:create.gold", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:gold.create abc", 0x0d5f1a740db2dbaaULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:object.create barrel", 0x9f6890a1bf90710aULL}, // 44 bytes; ret=1
    {"console:create.object barrel 1", 0x7cae25a1bf90710aULL}, // 44 bytes; ret=1
    {"console:object.create 5", 0x5647d810d0ba4ab2ULL}, // 64 bytes; ret=1
    {"console:object.create", 0x89d7d41851f15f61ULL}, // 102 bytes; ret=0 "require parameter 1 as object model"
    {"console:object.create notanobject", 0x75a99774f83d7b3bULL}, // 91 bytes; ret=0 "object model is invalid"
    {"console:creature.create troll", 0xc6611150ac27d51dULL}, // 67 bytes; ret=1
    {"console:create.creature troll 5 2", 0x0bb82f3c02a3ccfdULL}, // 129 bytes; ret=1
    {"console:creature.create troll 1 1 1", 0x8854b01af59fd337ULL}, // 65 bytes; ret=1
    {"console:creature.create evil", 0xe781dd97ed846a91ULL}, // 64 bytes; ret=1
    {"console:creature.create good", 0x77e8cf76378db00eULL}, // 67 bytes; ret=1
    {"console:creature.create any", 0x8f943ba179e5f1e5ULL}, // 66 bytes; ret=1
    {"console:creature.create 999", 0x0185bdf136f36566ULL}, // 103 bytes; ret=0 "creature model [999] exceeds (0, 37)"
    {"console:creature.create notacreature", 0xba8f29f6ca20cb9cULL}, // 103 bytes; ret=0 "creature model [-1] exceeds (0, 37)"
    {"console:creature.create", 0x8341f830368e1744ULL}, // 101 bytes; ret=0 "require parameter 1 as creature model"
    {"console:thing.create object barrel", 0x465f4416d6569ee7ULL}, // 46 bytes; ret=1
    {"console:create.thing corpse troll", 0x4df6043103600aaaULL}, // 35 bytes; ret=1
    {"console:thing.create creature imp", 0xeec1acaf5416f7eaULL}, // 66 bytes; ret=1
    {"console:thing.create trap boulder", 0x1c5ff2275a5a4ac0ULL}, // 39 bytes; ret=1
    {"console:thing.create effect EFFECT_EXPLOSION_1", 0xadcdee680b7c5ce2ULL}, // 46 bytes; ret=1
    {"console:thing.create shot SHOT_FIREBALL", 0x1012dcd12d213a4aULL}, // 60 bytes; ret=1
    {"console:thing.create 1 5", 0xd9aa1b5d2fcb4a20ULL}, // 66 bytes; ret=1
    {"console:thing.create nonsense x", 0xc6aa369b89aeb484ULL}, // 83 bytes; ret=0 "class is invalid"
    {"console:thing.create object", 0x1563bf13316dd2d9ULL}, // 96 bytes; ret=0 "require parameter 2 as model"
    {"console:thing.create", 0x33360334d657ad89ULL}, // 95 bytes; ret=0 "require parameter 1 as class"
    {"console:thing.get", 0x5832adba1f1a8d12ULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get $C0", 0x5832adba1f1a8d12ULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get 0", 0x5604bff788645abaULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.get $HEART", 0x67ef6dfba8028f6fULL}, // 106 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER"
    {"console:thing.get $HERO", 0x5604bff788645abaULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.info", 0xeff7c7772f4c1ac5ULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.info", 0xa83862c83ff4f920ULL}, // 292 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "next_on_map: 0, next_of_class: 0" "health: 75" "pos: 126 131 3" "creature
    {"console:thing.get $HEART ; thing.info", 0xc1930552c9adbb60ULL}, // 294 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1 "next_on_map: 0, next_of_class: 10" "health: 30000" "pos: 127 130
    {"console:thing.show_id ; thing.show_id 0", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:thing.get $C0 ; thing.health", 0xff838a412b0b2140ULL}, // 143 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "Thing ID: 19 health: 75"
    {"console:thing.get $C0 ; thing.health 5", 0x8ce99a415fbdf559ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.health", 0xeff7c7772f4c1ac5ULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.move 40 40", 0xd3df066c9ecbd184ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 40 40 3", 0x33cd9f173601b6feULL}, // 103 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move", 0x3461b0be308e8e56ULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 999 999", 0xa3aa7187a42bd36fULL}, // 144 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "invalid location"
    {"console:thing.move", 0xeff7c7772f4c1ac5ULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.destroy", 0xeff7c7772f4c1ac5ULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get 0 ; thing.destroy", 0xa40b04e5cf9f4e0bULL}, // 151 bytes; ret=0 "thing is invalid" ; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.destroy", 0xacf99348ff6b165dULL}, // 204 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $HEART ; thing.destroy", 0x98feeb4b755eb427ULL}, // 177 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1
    {"console:creature.addhealth 10", 0xb348e9e062e3a2e7ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.addhealth 10", 0xa97e8a415fbdf559ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.health.sub 10", 0x863b86415fbdf559ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.subhealth abc", 0x7f4ac2c03a93626fULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "parameter 1 requires a number"
    {"console:creature.health.add", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:thing.get $C0 ; creature.level 5", 0xe17b4659c51b70dfULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.level", 0x6daa0dc6b906ecd2ULL}, // 102 bytes; ret=0 "require parameter 1 as creature level"
    {"console:creature.level 5", 0xb348e9e062e3a2e7ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.freeze", 0x8698444d1e07b32dULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.slow", 0x4f75c29981993d2eULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.freeze", 0xb348e9e062e3a2e7ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.chicken", 0x075589dd72bf2be9ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.chicken", 0xb348e9e062e3a2e7ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.instance.set 2", 0xa7bf64d795705558ULL}, // 101 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.instance.set", 0xf18fa78bbffcb869ULL}, // 98 bytes; ret=0 "require parameter 1 as instance"
    {"console:creature.instance.set 2", 0xb348e9e062e3a2e7ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.state.set 1", 0xe45ce4ef7f40b53bULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.state.set 0", 0x838d27ef7f40b53bULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.state.set", 0x3662d26282219c92ULL}, // 96 bytes; ret=0 "require parameter 1 as state"
    {"console:thing.get $C0 ; creature.job.set 1", 0xc12885dddb5986c9ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 1."
    {"console:thing.get $C0 ; creature.job.set 3", 0xc00459e8900c85e0ULL}, // 124 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.job.set", 0xca72831bb312eab3ULL}, // 94 bytes; ret=0 "require parameter 1 as job"
    {"console:thing.get $C0 ; creature.job.set 70", 0xfa90c668f29f94d7ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 70."
    {"console:thing.get $C0 ; creature.attackheart 1", 0x9cec4fcafd967917ULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:thing.get $C0 ; creature.attackheart 99", 0x9cec4fcafd967917ULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:creature.attackheart", 0x3dbc2995bf68fcb5ULL}, // 104 bytes; ret=0 "require parameter 1 as player number"
    {"console:creature.attackheart 1", 0xb348e9e062e3a2e7ULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:digger.sendto", 0xc200e8f3006f9fcfULL}, // 109 bytes; ret=1 "creature IMP 22 will dig to PLAYER0"
    {"console:digger.sendto 1", 0x19dd4b8c5f90e337ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:thing.get $C0 ; digger.sendto 0", 0x2d1a23f09f9e8c0bULL}, // 157 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "creature IMP 19 will dig to PLAYER0"
    {"console:digger.sendto 99", 0x19dd4b8c5f90e337ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:room.get", 0xff88408b4b1452b4ULL}, // 83 bytes; ret=1 "Got room ID 7"
    {"console:room.get $ROOM", 0x03ae468b6890aa0eULL}, // 83 bytes; ret=1 "Got room ID 1"
    {"console:room.get 0", 0xe79616c819e00b50ULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get 511", 0xe79616c819e00b50ULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get $ROOM ; room.health", 0x1097a02fe2e4c8c8ULL}, // 138 bytes; ret=1 "Got room ID 1" ; ret=1 "Room ID 1 health: 18"
    {"console:room.get $ROOM ; room.health 10", 0x215bef40a5467b60ULL}, // 84 bytes; ret=1 "Got room ID 1" ; ret=1
    {"console:room.health", 0x184df122d30ce9f9ULL}, // 97 bytes; ret=0 "no thing selected or not room"
    {"console:bug.toggle", 0x346e9cf688c013ccULL}, // 99 bytes; ret=0 "require parameter 1 as bug name"
    {"console:bug.toggle 3", 0x92f61aa261559009ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle RESURRECT_FOREVER", 0x79286c64295d6a76ULL}, // 94 bytes; ret=1 "RESURRECT_FOREVER enabled"
    {"console:bug.toggle CLAIM_ROOM_ALL_THINGS", 0x92f61aa261559009ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle ALWAYS_TUNNEL_TO_RED", 0xc6ec63b9e9519cdaULL}, // 98 bytes; ret=1 "ALWAYS_TUNNEL_TO_RED enabled"
    {"console:bug.toggle 19", 0x689df1ab3411255fULL}, // 94 bytes; ret=1 "NEUTRAL_ROOM_JOBS enabled"
    {"console:bug.toggle 20", 0x328d8588fd74f73cULL}, // 87 bytes; ret=0 "bug [20] is invalid"
    {"console:bug.toggle 0", 0xce67c4e9ade4b130ULL}, // 87 bytes; ret=0 "bug [0] is invalid"
    {"console:bug.toggle notabug", 0x5430fd81021c4886ULL}, // 92 bytes; ret=0 "bug [notabug] is invalid"
    {"console:music.set 2", 0xc2d3cb3e0dc3138eULL}, // 2 bytes; ret=0
    {"console:music.set", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:sound.test 1", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:sound.test", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:speech.test 1", 0x049d339913befa5fULL}, // 1 bytes; ret=1
    {"console:speech.test", 0x3e9ed8e0f0763f0dULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:toggle.tooltip.land.coord ; toggle.tooltip.land.coord", 0xa5f169cfc260f672ULL}, // 147 bytes; ret=1 "tooltip_land_coord is turned on" ; ret=1 "tooltip_land_coord is turned off"
    {"console:toggle.lights", 0x0c0553499fef5435ULL}, // 4 bytes; ret=1
    {"console:lua PLAYER0.MONEY = 7", 0xba5eb96119d0c3b3ULL}, // 5 bytes; ret=1
    {"console:lua not valid lua(", 0x1ae067596503efb5ULL}, // 80 bytes; ret=1 "lua error, see log"
    {"console:cheat.menu 1 ; cheat.menu 0", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 2 ; cheat.menu 0", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu instance ; cheat.menu none", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu secondary ; cheat.menu 0", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 9", 0xeb5679d9dbcc12a0ULL}, // 128 bytes; ret=0 "menu type is invalid. possible values: 0/1/2/3/4, none/main/cre"
    {"console:cheat.menu", 0x454da1fae0424022ULL}, // 100 bytes; ret=0 "require parameter 1 as menu type"
    {"console:dbc ; dbc", 0xfa3a98bfb5264523ULL}, // 1 bytes; ret=1 ; ret=1
    {"console:resync", 0x772400edc6980235ULL}, // 4 bytes; ret=1
