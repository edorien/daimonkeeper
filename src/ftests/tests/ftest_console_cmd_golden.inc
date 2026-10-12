    {"console:", 0x9c122a06926616aaULL}, // 85 bytes; ret=0 "command is empty"
    {"console:notacommand", 0x46325d4fed8b968dULL}, // 87 bytes; ret=0 "unsupported command"
    {"console:STATS", 0xc4ef06ca81569c76ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-stats", 0x5161a3e1acc83db0ULL}, // 95 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:-reveal", 0x7939375277c38ad1ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-notacommand", 0x8e283c3e34d25158ULL}, // 0 bytes; ret=0
    {"console:-thing.get $C0", 0x7939375277c38ad1ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:-room.get $ROOM", 0x7939375277c38ad1ULL}, // 87 bytes; ret=0 "require 'cheat mode'"
    {"console:stats", 0xc4ef06ca81569c76ULL}, // 96 bytes; ret=1 "turn fps is 20, draw fps is 0"
    {"console:fps", 0x31c712a7cd8cde79ULL}, // 93 bytes; ret=1 "Framerate/Turn is 20 fps"
    {"console:fps 30", 0xd4963e921761523eULL}, // 2 bytes; ret=1
    {"console:fps.draw 60", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:ft", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:frametime.max", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:ft.max ; ft.max", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:netstats ; netstats", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:quit", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:time", 0x3e43ef92f4ce669cULL}, // 83 bytes; ret=1 "Time: 00:00:01"
    {"console:time 1200 20", 0xf2c8e3df34edcd3eULL}, // 83 bytes; ret=1 "Time: 00:01:00"
    {"console:timer.toggle", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:timer.switch", 0xe75950d56d511226ULL}, // 2 bytes; ret=1
    {"console:turn", 0x52c8056ef7d7059bULL}, // 76 bytes; ret=1 "turn 30"
    {"console:pause", 0xb3689a08073ab4cfULL}, // 2 bytes; ret=1
    {"console:pause ; pause", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:step", 0x94fed111eae48565ULL}, // 2 bytes; ret=1
    {"console:game.save 9 golden ; game.load 9", 0x1843821abafec4adULL}, // 0 bytes; ret=1 ; ret=1
    {"console:game.save -1", 0x658ba00f4527d86eULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load -1", 0x658ba00f4527d86eULL}, // 98 bytes; ret=0 "slot_num [-1] exceeds [0,10000)"
    {"console:game.load 9999", 0xcf1e646e48af38a5ULL}, // 92 bytes; ret=0 "Unable to load game 9999"
    {"console:cls", 0x7f67bebbc8afcc43ULL}, // 59 bytes; ret=1
    {"console:ver", 0x5ea6ce6ffd4b6cb2ULL}, // 114 bytes; ret=1 "dAImon Keeper 1.0.0 - KFX 1.4 (ver <version>)"
    {"console:volume", 0xc5365aaf10871e19ULL}, // 89 bytes; ret=1 "Sound FX: 50 Music: 35"
    {"console:volume.sound", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:volume.sound abc", 0x5d6f7b5b589e3ed2ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:volume.sound 50", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:volume.sfx 40", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:volume.music 30", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:volume.soundtrack abc", 0x5d6f7b5b589e3ed2ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:compuchat", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:compuchat scarce", 0x0bd958d5674181ddULL}, // 80 bytes; ret=1 "Ai model 7"
    {"console:compuchat 2", 0x609527797d617d3bULL}, // 71 bytes; ret=1 "2"
    {"console:compuchat none", 0x52b99f1c46ea381dULL}, // 73 bytes; ret=1 "none"
    {"console:comp.procs 1 ; cheat.menu 0", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.events 1 ; cheat.menu 0", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.checks 1 ; cheat.menu 0", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:comp.procs", 0xe4c311611a852efeULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.events 9", 0x751b6223a3d740e5ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.checks -1", 0xcb0d155cdeaeceb0ULL}, // 96 bytes; ret=0 "player idx [-1] exceeds [0,8]"
    {"console:comp.kill 1", 0x9124ab0184bae135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill 2", 0x9124ab0184bae135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:comp.kill", 0xe4c311611a852efeULL}, // 101 bytes; ret=0 "require parameter 1 as player idx"
    {"console:comp.kill 99", 0x6592f9f278d99124ULL}, // 96 bytes; ret=0 "player idx [99] exceeds [0,8]"
    {"console:comp.kill 9", 0x751b6223a3d740e5ULL}, // 96 bytes; ret=0 "player idx [9] exceeds [0,8]"
    {"console:comp.me 1", 0x73817e1e03ef51e4ULL}, // 1021 bytes; ret=1 "computer assistant is 1"
    {"console:comp.me", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:spectate", 0x5ab7ff977f200cc9ULL}, // 21721 bytes; ret=1 "player 0 is now AI-controlled; map revealed"
    {"console:spectate 1", 0x1a3aa25b10bca115ULL}, // 94 bytes; ret=0 "unable to spectate player 1"
    {"console:reveal", 0x82a1d1c7d6dbed9cULL}, // 21610 bytes; ret=1
    {"console:reveal 10", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:conceal", 0xe2a05dca9ef17a4aULL}, // 7228 bytes; ret=1
    {"console:conceal 10", 0x55a9933c5fe0c1f7ULL}, // 145 bytes; ret=1
    {"console:slab.place lava", 0x41a0f9650c892b2cULL}, // 2136 bytes; ret=1
    {"console:slab.place claimed", 0xdb4c442eca75dcebULL}, // 2113 bytes; ret=1
    {"console:slab.place rock", 0x00b4e10b4262eb08ULL}, // 2169 bytes; ret=1
    {"console:slab.place gold 1", 0xbf054fd2c518aeb2ULL}, // 2170 bytes; ret=1
    {"console:slab.place temple", 0x69074008c6ae6d92ULL}, // 2122 bytes; ret=1
    {"console:slab.place 10", 0x3f8f9884904d83bbULL}, // 2110 bytes; ret=1
    {"console:slab.place 99", 0x5a28c6db2e8f8c78ULL}, // 94 bytes; ret=0 "slbkind [99] exceeds [0,62]"
    {"console:slab.place notaslab", 0x2cd0b047db2f35fcULL}, // 94 bytes; ret=0 "slbkind [-1] exceeds [0,62]"
    {"console:slab.place", 0x392e374fd6139929ULL}, // 98 bytes; ret=0 "require parameter 1 as slbkind"
    {"console:slab.place earth", 0x21c312755a3020eaULL}, // 2170 bytes; ret=1
    {"console:slab.place reinforced", 0x24895bfa73c387ccULL}, // 2171 bytes; ret=1
    {"console:slab.place fortified", 0x24895bfa73c387ccULL}, // 2171 bytes; ret=1
    {"console:slab.health", 0x7ac54fc2e2d75ea5ULL}, // 83 bytes; ret=1 "Slab health: 20"
    {"console:slab.health 100", 0xaef9b436d0e57603ULL}, // 2 bytes; ret=1
    {"console:look treasure", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:look troll", 0xb1928a8ecb1a05d0ULL}, // 2 bytes; ret=1
    {"console:look 1", 0xc4e94340bdc8a06fULL}, // 19 bytes; ret=1
    {"console:look notalocation", 0x220fc1edc3dec3a9ULL}, // 91 bytes; ret=0 "subtile coord is invalid"
    {"console:look", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:zoomto.slabcoord 10 10", 0x83a76f7145527c47ULL}, // 31 bytes; ret=1
    {"console:zoomto.slabcoord 10", 0x8c035f88e0a1da13ULL}, // 96 bytes; ret=0 "require parameter 2 as slb_y"
    {"console:zoomto.slabcoord", 0x66fedd397b42fbc7ULL}, // 96 bytes; ret=0 "require parameter 1 as slb_x"
    {"console:zoomto.slabcoord 999 999", 0x654470a2051e761bULL}, // 107 bytes; ret=0 "Slab coordinates specified are invalid"
    {"console:zoomto.subtilecoord 40 40", 0x61a4007145527c47ULL}, // 31 bytes; ret=1
    {"console:zoomto.subtilecoord 40", 0x7e1b94a59e92cad1ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:zoomto.subtilecoord 999 1", 0x2ed6245a5818fdc8ULL}, // 108 bytes; ret=0 "Subtile coordinates specified are invalid"
    {"console:actionpoint.pos 1", 0x8ba6ae6cada50795ULL}, // 105 bytes; ret=1 "Action Point idx: 1 num: 1 X: 225 Y: 130"
    {"console:actionpoint.pos 99", 0x37d833cb8073a460ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.pos", 0xb35893a95c2a97dfULL}, // 108 bytes; ret=0 "require parameter 1 as actionpoint number"
    {"console:actionpoint.zoomto 1", 0xc4e94340bdc8a06fULL}, // 19 bytes; ret=1
    {"console:zoomto.actionpoint 99", 0x37d833cb8073a460ULL}, // 89 bytes; ret=0 "actionpoint no exist"
    {"console:actionpoint.reset 1", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:actionpoint.reset 1 0", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:herogate.zoomto 1", 0xff81433692575dc8ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:zoomto.herogate 9", 0xff81433692575dc8ULL}, // 86 bytes; ret=0 "thing is not object"
    {"console:herogate.zoomto", 0x0162f4e80fc51d9dULL}, // 107 bytes; ret=0 "require parameter 1 as hero gate number"
    {"console:mapwho.info", 0xe5d1a8e7aacff716ULL}, // 247 bytes; ret=1 "first_thing:19 creature IMP" "flags: 02,filled: 9, wib: 1, col: 0532" "mapwho: 0019, rev: 1" "stl_x: 126, stl_y:1
    {"console:mapwho.info 40 40", 0x20026c96f8fee076ULL}, // 251 bytes; ret=1 "first_thing:0 EMPTY model 0" "flags: 00,filled: 12, wib: 2, col: 0393" "mapwho: 0000, rev: 0" "stl_x: 40, stl_y:4
    {"console:mapwho.info 40", 0x7e1b94a59e92cad1ULL}, // 96 bytes; ret=0 "require parameter 2 as stl_y"
    {"console:mapwho.info 999 999", 0xe3ab59da5601778cULL}, // 83 bytes; ret=0 "invalid location"
    {"console:cursor.pos", 0x41dc22701a019eacULL}, // 90 bytes; ret=1 "Cursor at 126, 131, 3"
    {"console:player.score", 0x17094fdef23b9943ULL}, // 88 bytes; ret=1 "Player 0 score: 436"
    {"console:player.score 1", 0xcdb7bb748c50e372ULL}, // 86 bytes; ret=1 "Player 1 score: 0"
    {"console:player.score 99", 0x8042b239f4e5ee5aULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.flag 0 3", 0x9f5d113a3f08a39aULL}, // 93 bytes; ret=1 "Player 0 flag 3 value: 0"
    {"console:player.flag 0 3 9 ; player.flag 0 3", 0xd1afdce7d6341850ULL}, // 94 bytes; ret=1 ; ret=1 "Player 0 flag 3 value: 9"
    {"console:player.flag 0 99", 0xd1fac3bf7254d376ULL}, // 93 bytes; ret=0 "script flag [99] exceeds"
    {"console:player.flag 99", 0x8042b239f4e5ee5aULL}, // 87 bytes; ret=0 "dungeon is invalid"
    {"console:player.heart.health", 0xe8a3259d2a1e473fULL}, // 114 bytes; ret=1 "Player 0 heart health: 30000 (100.00 per cent)"
    {"console:player.heart.health 0 1000", 0x79f4bb47a93eebdeULL}, // 3 bytes; ret=1
    {"console:player.heart.health 1", 0x9124ab0184bae135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.heart.health 0 0", 0x75fefb0f4997ac10ULL}, // 5 bytes; ret=1
    {"console:player.heart.health 99", 0x9124ab0184bae135ULL}, // 91 bytes; ret=0 "thing is not dungeon heart"
    {"console:player.gold.add 0 1000", 0x221eb8b84881b42bULL}, // 5 bytes; ret=1
    {"console:player.addgold 1 500", 0x3e93f57ccfdde0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.gold.add 0", 0x4917da8d319e4997ULL}, // 101 bytes; ret=0 "require parameter 2 as gold amount"
    {"console:player.gold.add 99 1", 0x3e93f57ccfdde0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:player.colour 0 blue", 0xefde8bdfafa7f9afULL}, // 2013 bytes; ret=1
    {"console:player.color 1 green", 0x795a411e3b11d407ULL}, // 2 bytes; ret=1
    {"console:player.colour 0", 0xac9ab48c7f0ed337ULL}, // 96 bytes; ret=0 "require parameter 2 as color"
    {"console:player.colour", 0xac3549c0a46674ddULL}, // 97 bytes; ret=0 "require parameter 1 as player"
    {"console:player.colour 0 notacolour", 0x8338d6400a7fa019ULL}, // 85 bytes; ret=0 "colour is invalid"
    {"console:possession.lock", 0x72263e08828796dfULL}, // 2 bytes; ret=1
    {"console:possession.unlock", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:string.show 5", 0x599f78383fa5c286ULL}, // 23 bytes; ret=1
    {"console:string.show", 0x9edb6a594af20e36ULL}, // 15 bytes; ret=1
    {"console:quick.show 5", 0x8e496e383fa5c286ULL}, // 23 bytes; ret=1
    {"console:quick.show -1", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:give.trap boulder 2", 0x543dd1f28c005ae7ULL}, // 77 bytes; ret=1 "done!"
    {"console:trap.give gas", 0x91aa31674225f55dULL}, // 77 bytes; ret=1 "done!"
    {"console:give.trap 99", 0x132f476ba4dcb6edULL}, // 100 bytes; ret=0 "trap number [99] exceeds (0,10]"
    {"console:give.trap", 0x276aef5853dd3181ULL}, // 100 bytes; ret=0 "trap number [-1] exceeds (0,10]"
    {"console:give.door wooden", 0x2b3e11bd241d7aa7ULL}, // 77 bytes; ret=1 "done!"
    {"console:door.give braced 3", 0xc0eff785af05219eULL}, // 77 bytes; ret=1 "done!"
    {"console:give.door 99", 0x474805685adaa229ULL}, // 100 bytes; ret=0 "door number [99] exceeds (0, 7])"
    {"console:give.door", 0xf55806231ef4b055ULL}, // 100 bytes; ret=0 "door number [-1] exceeds (0, 7])"
    {"console:room.available temple", 0xf1d13a815c757416ULL}, // 2 bytes; ret=1
    {"console:room.available all", 0xcd5b655b974ead25ULL}, // 6 bytes; ret=1
    {"console:room.available temple 0", 0x3f44b4d79e551516ULL}, // 2 bytes; ret=1
    {"console:room.available hatchery", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:room.available guard 1 1", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:room.available", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give all", 0x9d0f4be6c2ca4cc4ULL}, // 118 bytes; ret=1
    {"console:spell.give POWER_LIGHTNING", 0x4ffbd504504cf6d0ULL}, // 2 bytes; ret=1
    {"console:power.give", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:power.give 999", 0x947603d113e0e2d3ULL}, // 83 bytes; ret=0 "power is invalid"
    {"console:creature.available troll", 0x1fb48e28a1722d90ULL}, // 2 bytes; ret=1
    {"console:creature.available troll 0", 0x93d8089793a96a10ULL}, // 2 bytes; ret=1
    {"console:creature.available notacreature", 0xfb2b2d3d824b9afeULL}, // 1 bytes; ret=0
    {"console:magic.instance imp 1 fireball", 0x6e18e2d9e0464287ULL}, // 6 bytes; ret=1
    {"console:magic.instance imp 1 2", 0x83fd878abed78e1bULL}, // 6 bytes; ret=1
    {"console:magic.instance", 0x27858145191fc4f6ULL}, // 97 bytes; ret=0 "require parameter 1 as creature"
    {"console:magic.instance imp", 0x58fd95060a84c89cULL}, // 95 bytes; ret=0 "require parameter 2 as slot"
    {"console:magic.instance imp 1", 0x3f60a635c00fd187ULL}, // 98 bytes; ret=0 "require parameter 3 as instance"
    {"console:magic.instance notacreature 1 fireball", 0x543bc35b2742a39dULL}, // 84 bytes; ret=0 "Invalid creature"
    {"console:magic.instance imp 12 fireball", 0x304d9aad05c14184ULL}, // 80 bytes; ret=0 "Invalid slot"
    {"console:magic.instance imp 1 0", 0x06778ff415141a7bULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:magic.instance imp 1 9999", 0x06778ff415141a7bULL}, // 84 bytes; ret=0 "Invalid instance"
    {"console:map.pool", 0xedb36ff80c4cb92dULL}, // 101 bytes; ret=1 "require parameter 1 as creature model"
    {"console:creature.pool troll", 0x5da009357ee068c9ULL}, // 89 bytes; ret=1 "Pool count troll: 20"
    {"console:map.pool troll 7", 0x873d681c9f50e404ULL}, // 93 bytes; ret=1 "Set troll pool count: 7"
    {"console:map.pool troll -1", 0x19020a87a9e32aacULL}, // 93 bytes; ret=0 "Set troll pool count: -1"
    {"console:map.pool notacreature", 0x01fce858ac8a9e07ULL}, // 95 bytes; ret=0 "Invalid creature: notacreature"
    {"console:creature.pool.add troll 3", 0xfc53e7c580737fd5ULL}, // 2 bytes; ret=1
    {"console:creature.pool.sub troll 1", 0xb97c6bc580737fd5ULL}, // 2 bytes; ret=1
    {"console:creature.pool.remove troll 1", 0xb97c6bc580737fd5ULL}, // 2 bytes; ret=1
    {"console:creature.pool.add troll", 0x0e319d635b709272ULL}, // 104 bytes; ret=0 "require parameter 2 as creature amount"
    {"console:creature.pool.add 0 3", 0xac258ffe033298c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add any 5", 0xac258ffe033298c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.add evil 5", 0xac258ffe033298c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:creature.pool.sub 500 1", 0xac258ffe033298c1ULL}, // 90 bytes; ret=0 "invalid creature model"
    {"console:gold.create 500", 0x5b687f18917ab3fcULL}, // 48 bytes; ret=1
    {"console:create.gold", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:gold.create abc", 0x5d6f7b5b589e3ed2ULL}, // 97 bytes; ret=0 "parameter 1 requires a number"
    {"console:object.create barrel", 0xdd4ad4a78cdf8617ULL}, // 44 bytes; ret=1
    {"console:create.object barrel 1", 0x65b6f9a78cdf8617ULL}, // 44 bytes; ret=1
    {"console:object.create 5", 0xba12a50988bb84eeULL}, // 64 bytes; ret=1
    {"console:object.create", 0xeeaf4fb398247461ULL}, // 102 bytes; ret=0 "require parameter 1 as object model"
    {"console:object.create notanobject", 0x4d0114f31afe2e13ULL}, // 91 bytes; ret=0 "object model is invalid"
    {"console:creature.create troll", 0x22836f6116fae0bbULL}, // 67 bytes; ret=1
    {"console:create.creature troll 5 2", 0x9d6c5a7b34518f09ULL}, // 129 bytes; ret=1
    {"console:creature.create troll 1 1 1", 0xdfb086998b83d121ULL}, // 65 bytes; ret=1
    {"console:creature.create evil", 0x289b8ef42ef011abULL}, // 64 bytes; ret=1
    {"console:creature.create good", 0x6014c5147d5c02c4ULL}, // 67 bytes; ret=1
    {"console:creature.create any", 0xc2b43ad080a80187ULL}, // 66 bytes; ret=1
    {"console:creature.create 999", 0x0ef5e1d434dc26feULL}, // 103 bytes; ret=0 "creature model [999] exceeds (0, 37)"
    {"console:creature.create notacreature", 0x7295976b6212e304ULL}, // 103 bytes; ret=0 "creature model [-1] exceeds (0, 37)"
    {"console:creature.create", 0x0a2d6484e93c491cULL}, // 101 bytes; ret=0 "require parameter 1 as creature model"
    {"console:thing.create object barrel", 0x81cc5f42fbc21063ULL}, // 46 bytes; ret=1
    {"console:create.thing corpse troll", 0xbd83f084c2c442a2ULL}, // 35 bytes; ret=1
    {"console:thing.create creature imp", 0xba5964741c0b578dULL}, // 66 bytes; ret=1
    {"console:thing.create trap boulder", 0xb221ceac4ad5e9d3ULL}, // 39 bytes; ret=1
    {"console:thing.create effect EFFECT_EXPLOSION_1", 0xb98740e063c152b3ULL}, // 46 bytes; ret=1
    {"console:thing.create shot SHOT_FIREBALL", 0x9c9a9653de8e40f6ULL}, // 60 bytes; ret=1
    {"console:thing.create 1 5", 0x156630c981a00689ULL}, // 66 bytes; ret=1
    {"console:thing.create nonsense x", 0xc70e4a06bbba96bcULL}, // 83 bytes; ret=0 "class is invalid"
    {"console:thing.create object", 0x8a2033cb4fe3ce39ULL}, // 96 bytes; ret=0 "require parameter 2 as model"
    {"console:thing.create", 0x287edea261df2481ULL}, // 95 bytes; ret=0 "require parameter 1 as class"
    {"console:thing.get", 0xe12e0b6eeff9c5daULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get $C0", 0xe12e0b6eeff9c5daULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP"
    {"console:thing.get 0", 0x21c50fc555e754faULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.get $HEART", 0x4297d4220219bb17ULL}, // 106 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER"
    {"console:thing.get $HERO", 0x21c50fc555e754faULL}, // 82 bytes; ret=0 "thing is invalid"
    {"console:thing.info", 0x77ce1ca039a7c61dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.info", 0x5fb71aef4762c188ULL}, // 292 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "next_on_map: 0, next_of_class: 0" "health: 75" "pos: 126 131 3" "creature
    {"console:thing.get $HEART ; thing.info", 0xd97d380863264928ULL}, // 294 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1 "next_on_map: 0, next_of_class: 10" "health: 30000" "pos: 127 130
    {"console:thing.show_id ; thing.show_id 0", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:thing.get $C0 ; thing.health", 0xab9de70c0d8060d0ULL}, // 143 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "Thing ID: 19 health: 75"
    {"console:thing.get $C0 ; thing.health 5", 0xe5b0c9746942b594ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.health", 0x77ce1ca039a7c61dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.move 40 40", 0x9cb4a4ee6e94a976ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 40 40 3", 0xd2c623c4a9bc8935ULL}, // 103 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move", 0x2d2526ebcc6d26feULL}, // 96 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; thing.move 999 999", 0x1cf7a27b09b79b97ULL}, // 144 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "invalid location"
    {"console:thing.move", 0x77ce1ca039a7c61dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.destroy", 0x77ce1ca039a7c61dULL}, // 103 bytes; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get 0 ; thing.destroy", 0x444907f076306ea3ULL}, // 151 bytes; ret=0 "thing is invalid" ; ret=0 "no thing selected or thing is invalid"
    {"console:thing.get $C0 ; thing.destroy", 0xd1f1734fe03b1272ULL}, // 204 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $HEART ; thing.destroy", 0x7086edcdb79f0e3eULL}, // 177 bytes; ret=1 "Got thing ID 12 object SOUL_CONTAINER" ; ret=1
    {"console:creature.addhealth 10", 0x321481d3c1da0f7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.addhealth 10", 0xb53179746942b594ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.health.sub 10", 0x6c91e5746942b594ULL}, // 97 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.subhealth abc", 0x5d6c3832fa3d8aa7ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "parameter 1 requires a number"
    {"console:creature.health.add", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:thing.get $C0 ; creature.level 5", 0x4ace9ca1a1e60693ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.level", 0x3e2f2e4950059d52ULL}, // 102 bytes; ret=0 "require parameter 1 as creature level"
    {"console:creature.level 5", 0x321481d3c1da0f7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.freeze", 0x0ac9344d59f833a5ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.slow", 0xf0e5d28841023de6ULL}, // 102 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.freeze", 0x321481d3c1da0f7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.chicken", 0x9ea5ff3f374b88e9ULL}, // 104 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.chicken", 0x321481d3c1da0f7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.instance.set 2", 0x6b0ba410f884cc00ULL}, // 101 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.instance.set", 0x8f676d25acfc2a79ULL}, // 98 bytes; ret=0 "require parameter 1 as instance"
    {"console:creature.instance.set 2", 0x321481d3c1da0f7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:thing.get $C0 ; creature.state.set 1", 0x9500ab617958d0ffULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:thing.get $C0 ; creature.state.set 0", 0x018642617958d0ffULL}, // 98 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.state.set", 0x0d1e85dc303fa772ULL}, // 96 bytes; ret=0 "require parameter 1 as state"
    {"console:thing.get $C0 ; creature.job.set 1", 0xfc1db9c17b708d01ULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 1."
    {"console:thing.get $C0 ; creature.job.set 3", 0x6e7a4b7acaf02a97ULL}, // 124 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1
    {"console:creature.job.set", 0xdbfee4f9992ccfe3ULL}, // 94 bytes; ret=0 "require parameter 1 as job"
    {"console:thing.get $C0 ; creature.job.set 70", 0x03868dac486fa1bfULL}, // 146 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "Cannot do job 70."
    {"console:thing.get $C0 ; creature.attackheart 1", 0x09c3532af959a3ffULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:thing.get $C0 ; creature.attackheart 99", 0x09c3532af959a3ffULL}, // 142 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=0 "thing is not dungeon heart"
    {"console:creature.attackheart", 0xccfbe61b47f23ce5ULL}, // 104 bytes; ret=0 "require parameter 1 as player number"
    {"console:creature.attackheart 1", 0x321481d3c1da0f7fULL}, // 101 bytes; ret=0 "no thing selected or not creature"
    {"console:digger.sendto", 0xa87e3bc1440fd063ULL}, // 109 bytes; ret=1 "creature IMP 22 will dig to PLAYER0"
    {"console:digger.sendto 1", 0x3e93f57ccfdde0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:thing.get $C0 ; digger.sendto 0", 0x0a36ad3e26f222abULL}, // 157 bytes; ret=1 "Got thing ID 19 creature IMP" ; ret=1 "creature IMP 19 will dig to PLAYER0"
    {"console:digger.sendto 99", 0x3e93f57ccfdde0f7ULL}, // 84 bytes; ret=0 "player no exist"
    {"console:room.get", 0xdc629efe149e4f14ULL}, // 83 bytes; ret=1 "Got room ID 7"
    {"console:room.get $ROOM", 0x55b688fe321aa66eULL}, // 83 bytes; ret=1 "Got room ID 1"
    {"console:room.get 0", 0xbc9751531d8e8920ULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get 511", 0xbc9751531d8e8920ULL}, // 82 bytes; ret=0 "room no exist"
    {"console:room.get $ROOM ; room.health", 0xee329bb77abb71d0ULL}, // 138 bytes; ret=1 "Got room ID 1" ; ret=1 "Room ID 1 health: 18"
    {"console:room.get $ROOM ; room.health 10", 0x4481bd36459e4ae0ULL}, // 84 bytes; ret=1 "Got room ID 1" ; ret=1
    {"console:room.health", 0x2594abbfbd111281ULL}, // 97 bytes; ret=0 "no thing selected or not room"
    {"console:bug.toggle", 0x84c61f62e96bcac4ULL}, // 99 bytes; ret=0 "require parameter 1 as bug name"
    {"console:bug.toggle 3", 0xbf17d9efc7fd2759ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle RESURRECT_FOREVER", 0xadaa192323d2f2ceULL}, // 94 bytes; ret=1 "RESURRECT_FOREVER enabled"
    {"console:bug.toggle CLAIM_ROOM_ALL_THINGS", 0xbf17d9efc7fd2759ULL}, // 99 bytes; ret=1 "CLAIM_ROOM_ALL_THINGS enabled"
    {"console:bug.toggle ALWAYS_TUNNEL_TO_RED", 0xd0e90b7b03fba602ULL}, // 98 bytes; ret=1 "ALWAYS_TUNNEL_TO_RED enabled"
    {"console:bug.toggle 19", 0xa8faa9533d289067ULL}, // 94 bytes; ret=1 "NEUTRAL_ROOM_JOBS enabled"
    {"console:bug.toggle 20", 0x515135b0a1833174ULL}, // 87 bytes; ret=0 "bug [20] is invalid"
    {"console:bug.toggle 0", 0x7aebcebec49aa2d8ULL}, // 87 bytes; ret=0 "bug [0] is invalid"
    {"console:bug.toggle notabug", 0x6f842809486804e6ULL}, // 92 bytes; ret=0 "bug [notabug] is invalid"
    {"console:music.set 2", 0xba3ce4af29737dc6ULL}, // 2 bytes; ret=0
    {"console:music.set", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:sound.test 1", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:sound.test", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:speech.test 1", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
    {"console:speech.test", 0x5e7d0d9b3feddae5ULL}, // 87 bytes; ret=0 "require parameter 1"
    {"console:toggle.tooltip.land.coord ; toggle.tooltip.land.coord", 0x02d68256f554a64aULL}, // 147 bytes; ret=1 "tooltip_land_coord is turned on" ; ret=1 "tooltip_land_coord is turned off"
    {"console:toggle.lights", 0xd1168a8400e76b75ULL}, // 4 bytes; ret=1
    {"console:lua PLAYER0.MONEY = 7", 0x1ba92eb84881b42bULL}, // 5 bytes; ret=1
    {"console:lua not valid lua(", 0x47d7f398f43490d5ULL}, // 80 bytes; ret=1 "lua error, see log"
    {"console:cheat.menu 1 ; cheat.menu 0", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 2 ; cheat.menu 0", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu instance ; cheat.menu none", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu secondary ; cheat.menu 0", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:cheat.menu 9", 0x626ece7c31fa58c0ULL}, // 128 bytes; ret=0 "menu type is invalid. possible values: 0/1/2/3/4, none/main/cre"
    {"console:cheat.menu", 0x3edeb1e7d3e4eff2ULL}, // 100 bytes; ret=0 "require parameter 1 as menu type"
    {"console:dbc ; dbc", 0x81548e8d94971a2bULL}, // 1 bytes; ret=1 ; ret=1
    {"console:resync", 0xfb292d3d824b1827ULL}, // 1 bytes; ret=1
