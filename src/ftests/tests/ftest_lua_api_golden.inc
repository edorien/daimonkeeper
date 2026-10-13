    {"lua:SetGenerateSpeed(200)", 0xd30b400585c71090ULL}, // 36 bytes; ok:
    {"lua:SetGenerateSpeed(200, PLAYER0)", 0x1f8f010cddd88fe8ULL}, // 4 bytes; ok:
    {"lua:ComputerPlayer(PLAYER1, 0)", 0x541e51c275fecdfeULL}, // 37975 bytes; ok:
    {"lua:ComputerPlayer(PLAYER1, 'ROAMING')", 0x99912bacdb802f8cULL}, // 2 bytes; ok:
    {"lua:AllyPlayers(PLAYER0, PLAYER_GOOD, 1)", 0x048c064730a942dcULL}, // 2530 bytes; ok:
    {"lua:AllyPlayers(PLAYER0, PLAYER_GOOD)", 0x5d48ae1859796ce0ULL}, // 0 bytes; error: [string "AllyPlayers(PLAYER0, PLAYER_GOOD)"]:1: bad argument #3 to 'AllyPlayers' (number expected, got no value)
    {"lua:StartMoney(PLAYER0, 5000)", 0xa0b2c6b00a396ef4ULL}, // 4 bytes; ok:
    {"lua:StartMoney(ALL_PLAYERS, 0)", 0xf547145dc27533d1ULL}, // 0 bytes; error: [string "StartMoney(ALL_PLAYERS, 0)"]:1: bad argument #1 to 'StartMoney' (string expected, got nil)
    {"lua:MaxCreatures(PLAYER0, 3)", 0x172f6610dde3bdf7ULL}, // 1 bytes; ok:
    {"lua:AddCreatureToPool('TROLL', 4)", 0x6577adccbadefd86ULL}, // 1 bytes; ok:
    {"lua:AddCreatureToPool('NOT_A_CREATURE', 4)", 0x7294eaebbdc57f4eULL}, // 0 bytes; error: [string "AddCreatureToPool('NOT_A_CREATURE', 4)"]:1: bad argument #1 to 'AddCreatureToPool' (unrecognized command
    {"lua:CreatureAvailable(PLAYER0, 'TROLL', true, 2)", 0xa5f9e7744dd86747ULL}, // 1 bytes; ok:
    {"lua:DeadCreaturesReturnToPool(false)", 0xcc915ad378db16aeULL}, // 1 bytes; ok:
    {"lua:RoomAvailable(PLAYER0, 'TEMPLE', 1, true)", 0x5c129d95ea82721dULL}, // 1 bytes; ok:
    {"lua:RoomAvailable(PLAYER0, 'NOT_A_ROOM', 1, true)", 0x2d25461400b29527ULL}, // 0 bytes; error: [string "RoomAvailable(PLAYER0, 'NOT_A_ROOM', 1, true)"]:1: bad argument #2 to 'RoomAvailable' (unrecognized comm
    {"lua:MagicAvailable(PLAYER0, 'POWER_LIGHTNING', true, true)", 0xccd9e92e4badad07ULL}, // 1 bytes; ok:
    {"lua:DoorAvailable(PLAYER0, 'BRACED', true, 3)", 0x1cf509608e7cec77ULL}, // 3 bytes; ok:
    {"lua:TrapAvailable(PLAYER0, 'BOULDER', true, 2)", 0x1018b3ae1fc20640ULL}, // 3 bytes; ok:
    {"lua:WinGame(PLAYER0)", 0x7c7ba7d9c83a42f6ULL}, // 19 bytes; ok:
    {"lua:WinGame()", 0xa80858014f3c40b6ULL}, // 51 bytes; ok:
    {"lua:LoseGame(PLAYER0)", 0x6fa2250f5454b27bULL}, // 18 bytes; ok:
    {"lua:return CountCreaturesAtActionPoint(1, PLAYER0, 'ANY_CREATURE')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return CountCreaturesAtActionPoint(1, ALL_PLAYERS, 'IMP')", 0x1792fc2ddccfa36eULL}, // 0 bytes; error: bad argument #2 to '?' (string expected, got nil)
    {"lua:SetTimer(PLAYER0, 'TIMER0')", 0x93108a5e8f14b3aeULL}, // 2 bytes; ok:
    {"lua:AddToTimer(PLAYER0, 'TIMER0', 100)", 0x206cccf5117f17f0ULL}, // 8 bytes; ok:
    {"lua:DisplayTimer(PLAYER0, 'TIMER0', 1000, true)", 0xb84aab25093a02bdULL}, // 2 bytes; ok:
    {"lua:HideTimer()", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:BonusLevelTime(1000)", 0xbe47aff48b321797ULL}, // 3 bytes; ok:
    {"lua:BonusLevelTime(1000, 1)", 0x8233ecef230d4b78ULL}, // 4 bytes; ok:
    {"lua:BonusLevelTime(1000, 0)", 0xbe47aff48b321797ULL}, // 3 bytes; ok:
    {"lua:BonusLevelTime(1000, true)", 0x8233ecef230d4b78ULL}, // 4 bytes; ok:
    {"lua:AddBonusTime(500)", 0x409b5d559537a21dULL}, // 2 bytes; ok:
    {"lua:ResetActionPoint(1, PLAYER0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetNextLevel(12)", 0x22d6e7c4e468deadULL}, // 1 bytes; ok:
    {"lua:TriggerActionPoint(1, PLAYER0)", 0x6881e0bc73256f85ULL}, // 1 bytes; ok:
    {"lua:return IsActionpointActivatedByPlayer(PLAYER0, 1)", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:return AddCreatureToLevel(PLAYER0, 'TROLL', 1, 3, 100)", 0x32a89d0f869a1223ULL}, // 73 bytes; ok: Thing#126
    {"lua:return AddCreatureToLevel(PLAYER_GOOD, 'KNIGHT', PLAYER0, 5, 0)", 0xfa149d47e93a674cULL}, // 73 bytes; ok: Thing#126
    {"lua:return AddCreatureToLevel(PLAYER0, 'IMP', {stl_x = 40, stl_y = 40}, 1, 0)", 0xcccebfe844b60f9aULL}, // 63 bytes; ok: Thing#126
    {"lua:return AddCreatureToLevel(PLAYER0, 'NOT_A_CREATURE', 1, 1, 0)", 0xe51c2d4eebe25b10ULL}, // 0 bytes; error: bad argument #2 to '?' (unrecognized command)
    {"lua:return AddTunnellerToLevel(PLAYER_GOOD, 1, 'DUNGEON_HEART', 0, 2, 100)", 0xb79d57ab921a9413ULL}, // 108 bytes; ok: Thing#126
    {"lua:CreateParty('GOLDEN')", 0xf221bc41006abbc9ULL}, // 7 bytes; ok:
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'KNIGHT', 3, 100, 'ATTACK_DUNGEON_HEART', 0)", 0xbf415fe94f77638cULL}, // 14 bytes; ok:
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'KNIGHT', 3, 100, 'ATTACK_DUNGEON_HEART', 0) DeleteFromParty('GOLDEN', 'KNIGHT', 3)", 0xe0be0eca80d394feULL}, // 13 bytes; ok:
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'ARCHER', 2, 0, 'ATTACK_DUNGEON_HEART', 0) return AddPartyToLevel(PLAYER_GOOD, 'GOLDEN', 1)", 0x7964a586609f15acULL}, // 119 bytes; ok: {1=Thing#126}
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'ARCHER', 2, 0, 'ATTACK_DUNGEON_HEART', 0) AddTunnellerPartyToLevel(PLAYER_GOOD, 'GOLDEN', 1, 'DUNGEON_HEART', 0, 1, 0)", 0x7ba38ae8dbf22268ULL}, // 226 bytes; ok:
    {"lua:return AddPartyToLevel(PLAYER_GOOD, 'NO_SUCH_PARTY', 1)", 0xc6ac750cf662089aULL}, // 0 bytes; error: bad argument #2 to '?' (Party of requested name, 'NO_SUCH_PARTY', is not defined)
    {"lua:DisplayObjective(1)", 0x86dd6536130587a6ULL}, // 412 bytes; ok:
    {"lua:DisplayObjective(1, 1)", 0x1a3185000677488eULL}, // 388 bytes; ok:
    {"lua:DisplayObjectiveWithPos(1, 40, 40)", 0xbbff82faa7d6e26eULL}, // 396 bytes; ok:
    {"lua:DisplayInformation(2)", 0xfa255eef0adc5341ULL}, // 183 bytes; ok:
    {"lua:DisplayInformationWithPos(2, 40, 40)", 0x5ce30763745260e1ULL}, // 215 bytes; ok:
    {"lua:QuickObjective('golden objective')", 0xf5765bf84c19cc84ULL}, // 187 bytes; ok:
    {"lua:QuickObjectiveWithPos('golden', 40, 40)", 0x8c033f3da3a45df7ULL}, // 81 bytes; ok:
    {"lua:QuickInformation(1, 'golden info')", 0x99147e3186d78be4ULL}, // 194 bytes; ok:
    {"lua:QuickInformationWithPos(1, 'golden', 40, 40)", 0x617735c20e8ff324ULL}, // 221 bytes; ok:
    {"lua:DisplayPlayerObjective(1, PLAYER0)", 0x2ff4e2348810c32aULL}, // 48 bytes; ok:
    {"lua:DisplayPlayerObjectiveWithPos(1, PLAYER0, 40, 40)", 0x04937acbcb192832ULL}, // 46 bytes; ok:
    {"lua:DisplayPlayerInformation(2, PLAYER0)", 0xf4fc260d2eb60e6dULL}, // 22 bytes; ok:
    {"lua:DisplayPlayerInformationWithPos(2, PLAYER0, 40, 40)", 0xa528db4dbd81cc4dULL}, // 26 bytes; ok:
    {"lua:QuickPlayerObjective('golden', PLAYER0)", 0x921ac89c6b09ce13ULL}, // 13 bytes; ok:
    {"lua:QuickPlayerObjectiveWithPos('golden', PLAYER0, 40, 40)", 0x3d09da033ee6025bULL}, // 11 bytes; ok:
    {"lua:QuickPlayerInformation(1, PLAYER0, 'golden')", 0x861163dd07ec3140ULL}, // 28 bytes; ok:
    {"lua:QuickPlayerInformationWithPos(1, PLAYER0, 'golden', 40, 40)", 0xca3c53e5bb76b6a0ULL}, // 32 bytes; ok:
    {"lua:DisplayMessage(1)", 0x8b73d4897b56a27eULL}, // 99 bytes; ok:
    {"lua:DisplayMessage(1, PLAYER0)", 0x85c902aaeae7b520ULL}, // 98 bytes; ok:
    {"lua:DisplayMessage(1, 'IMP')", 0x7b3e400ce44ab9e0ULL}, // 100 bytes; ok:
    {"lua:QuickMessage('golden')", 0xe227f8809f58fedbULL}, // 63 bytes; ok:
    {"lua:QuickMessage('golden', PLAYER0)", 0x704df882005c0c17ULL}, // 62 bytes; ok:
    {"lua:QuickMessage('golden') ClearMessage()", 0x2e6c710cddcf0137ULL}, // 63 bytes; ok:
    {"lua:ClearMessage(1)", 0xcaf30a9d3e3b50ddULL}, // 2 bytes; ok:
    {"lua:HeartLostObjective(1, PLAYER0)", 0xb4d579af487fbd8dULL}, // 3 bytes; ok:
    {"lua:HeartLostQuickObjective('golden', PLAYER0)", 0xb54c35592250ccbdULL}, // 0 bytes; error: [string "HeartLostQuickObjective('golden', PLAYER0)"]:1: bad argument #1 to 'HeartLostQuickObjective' (number exp
    {"lua:HeartLostQuickObjective(1, 'golden', PLAYER0)", 0x32ca5a9991246f85ULL}, // 10 bytes; ok:
    {"lua:PlayMessage(PLAYER0, 'SOUND', 1)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:PlayMessage(PLAYER0, 'SPEECH', 1)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:TutorialFlashButton('TREASURE', 100)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:TutorialFlashButton(5, 100)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:DisplayCountdown(PLAYER0, 'TIMER0', 500, false)", 0x6f5f9da830b7ce8dULL}, // 3 bytes; ok:
    {"lua:DisplayVariable(PLAYER0, 'MONEY')", 0x6ec741b4027f063fULL}, // 12 bytes; ok:
    {"lua:DisplayVariable(PLAYER0, 'MONEY', 10000, 0)", 0xc85598b014333140ULL}, // 14 bytes; ok:
    {"lua:DisplayVariableWithLabel(PLAYER0, 'TOTAL_CREATURES', 'IMP')", 0xfd9c7e26b2fd66d6ULL}, // 7 bytes; ok:
    {"lua:DisplayVariable(PLAYER0, 'MONEY') HideVariable(PLAYER0, 'MONEY')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:DisplayVariable(PLAYER0, 'NOT_A_VARIABLE')", 0xc336f62fe482b173ULL}, // 0 bytes; error: [string "DisplayVariable(PLAYER0, 'NOT_A_VARIABLE')"]:1: bad argument #2 to 'DisplayVariable' (Unknown variable,
    {"lua:SetBoxTooltip(1, 'golden')", 0x1f0d516e45ce5d31ULL}, // 6 bytes; ok:
    {"lua:SetBoxTooltipId(1, 5)", 0x2ad94615dabe3260ULL}, // 136 bytes; ok:
    {"lua:RevealMapLocation(PLAYER0, 1, 5)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:RevealMapRect(PLAYER0, 40, 40, 10, 10)", 0x832f8d37dfa010c5ULL}, // 225 bytes; ok:
    {"lua:ConcealMapRect(PLAYER0, 40, 40, 10, 10)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:ConcealMapRect(PLAYER0, 40, 40, 10, 10, true)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:PlaceDoor(PLAYER0, 'WOOD', 33, 37, true, true)", 0x6d419d160de2f06eULL}, // 633 bytes; ok:
    {"lua:PlaceDoor(PLAYER0, 'WOOD', 33, 37, true, true) SetDoor('UNLOCKED', 33, 37)", 0x8ae21c05bac9ddd6ULL}, // 599 bytes; ok:
    {"lua:SetDoor('LOCKED', 33, 37)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true)", 0x63fa82656b503aa7ULL}, // 41 bytes; ok:
    {"lua:ChangeSlabOwner(33, 37, PLAYER_GOOD)", 0x8b87dff615cf0a2aULL}, // 70 bytes; ok:
    {"lua:ChangeSlabOwner(33, 37, PLAYER_GOOD, 'MATCH')", 0x1cd8e619f78bb1b2ULL}, // 270 bytes; ok:
    {"lua:ChangeSlabType(33, 37, 'LAVA')", 0x5575b8e4ed213855ULL}, // 570 bytes; ok:
    {"lua:ChangeSlabType(33, 37, 'GOLD', 'FLOOR')", 0xa0d27a528247f75cULL}, // 6062 bytes; ok:
    {"lua:ChangeSlabTexture(33, 37, 'ANCIENT')", 0x5e33bd96d63ff4ecULL}, // 2 bytes; ok:
    {"lua:HideHeroGate(1, true)", 0x3bbe94371d2a67f6ULL}, // 25 bytes; ok:
    {"lua:AddHeartHealth(PLAYER0, -100, true)", 0xda49967e10c24321ULL}, // 21 bytes; ok:
    {"lua:AddHeartHealth(PLAYER0, 100, false)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:MakeSafe(PLAYER0)", 0x822c23b44eee091bULL}, // 87 bytes; ok:
    {"lua:MakeUnsafe(PLAYER0)", 0x409fb7a04b9a7102ULL}, // 5234 bytes; ok:
    {"lua:LocateHiddenWorld()", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return AddObjectToLevel('GOLD_CHEST', 1, 500)", 0xa005a1a178a6da3aULL}, // 45 bytes; ok: Thing#126
    {"lua:return AddObjectToLevel('GOLD_CHEST', 1, 500, PLAYER0, 0)", 0xd64eeca178a6da3aULL}, // 45 bytes; ok: Thing#126
    {"lua:return AddObjectToLevelAtPos('BARREL', 100, 112, 0)", 0x470367e7582f4040ULL}, // 43 bytes; ok: Thing#126
    {"lua:return AddObjectToLevel('NOT_AN_OBJECT', 1, 0)", 0x93f67e3d178af56fULL}, // 0 bytes; error: bad argument #1 to '?' (unrecognized command)
    {"lua:return AddShotToLevel('SHOT_FIREBALL', 1, PLAYER0, 'CreaturesOnly')", 0xf138e6de844029e3ULL}, // 60 bytes; ok: Thing#126
    {"lua:AddEffectGeneratorToLevel('EFFECTGENERATOR_LAVA', 1, 5)", 0x8a18d141fbb32011ULL}, // 30 bytes; ok:
    {"lua:return AddCorpseToLevel('TROLL', 1, 2)", 0xb18f08ee2edd19adULL}, // 33 bytes; ok: Thing#126
    {"lua:return AddCorpseToLevel('TROLL', 1, 2, true, PLAYER0)", 0xba23c60cc7d75420ULL}, // 31 bytes; ok: Thing#126
    {"lua:return CreateEffect('EFFECT_EXPLOSION_1', 1, 0)", 0xf97b7018c8712b2bULL}, // 47 bytes; ok: Thing#8283
    {"lua:return CreateEffectAtPos('EFFECT_EXPLOSION_1', 40, 40, 0)", 0x1f4705a7366da5dbULL}, // 47 bytes; ok: Thing#8283
    {"lua:CreateEffectsLine(1, 2, 0, 1, 1, 'EFFECT_EXPLOSION_1')", 0x5f1b7aac5d68b800ULL}, // 23 bytes; ok:
    {"lua:NewCreatureType('GOLDEN')", 0x02ec5b92d8e041a1ULL}, // 16 bytes; ok:
    {"lua:CopyCreatureType('TROLL', 'GOLDEN')", 0xe286ee80abc31d3cULL}, // 379 bytes; ok:
    {"lua:SetDoorConfiguration('WOOD', 'Health', 999)", 0x6d7e24993c3d396cULL}, // 4 bytes; ok:
    {"lua:SetObjectConfiguration('BARREL', 'Health', 999)", 0xe067fc324af69575ULL}, // 2 bytes; ok:
    {"lua:SetTrapConfiguration('BOULDER', 'ManufactureLevel', 3)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetEffectGeneratorConfiguration('EFFECTGENERATOR_LAVA', 'GenerationDelayMin', 5)", 0x10895018810c13e0ULL}, // 1 bytes; ok:
    {"lua:SetRoomConfiguration('TREASURE', 'Cost', 100)", 0xb000793ad71483e8ULL}, // 1 bytes; ok:
    {"lua:Set_creature_configuration('IMP', 'Health', 999)", 0x19ffd830902d55b3ULL}, // 0 bytes; error: [string "Set_creature_configuration('IMP', 'Health', 999)"]:1: attempt to call global 'Set_creature_configuration
    {"lua:SetCreatureProperty('IMP', 'FLYING', true)", 0x8592a95cf868cae7ULL}, // 0 bytes; error: [string "SetCreatureProperty('IMP', 'FLYING', true)"]:1: attempt to call global 'SetCreatureProperty' (a nil valu
    {"lua:ResearchOrder(PLAYER0, 'ROOM', 'TEMPLE', 100)", 0x3e417cd5250f3c22ULL}, // 0 bytes; error: [string "ResearchOrder(PLAYER0, 'ROOM', 'TEMPLE', 100)"]:1: attempt to call global 'ResearchOrder' (a nil value)
    {"lua:ChangeCreatureOwner(C0(), PLAYER_GOOD)", 0xe020b31a90c94261ULL}, // 0 bytes; error: [string "ChangeCreatureOwner(C0(), PLAYER_GOOD)"]:1: attempt to call global 'ChangeCreatureOwner' (a nil value)
    {"lua:SetGameRule('GoldPerGoldBlock', 2000)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetGameRule('NotARule', 1)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetHandRule(PLAYER0, 'IMP', 0, 'DENY', 'ALWAYS', 0)", 0xac78f21ce83cc3b9ULL}, // 2 bytes; ok:
    {"lua:SwapCreature('TROLL', 'DRAGON')", 0x4f62154f84332723ULL}, // 124 bytes; ok:
    {"lua:SetSacrificeRecipe('MKCREATURE', 'TROLL', 'FLY', 'BUG')", 0xf6d140fbfd47c6e8ULL}, // 2 bytes; ok:
    {"lua:RemoveSacrificeRecipe('FLY', 'BUG')", 0x01ad3593a3edabc0ULL}, // 63 bytes; ok:
    {"lua:SetCreatureInstance('IMP', 1, 'FIREBALL', 1)", 0x8b29031d7ac523c0ULL}, // 9 bytes; ok:
    {"lua:SetCreatureMaxLevel(PLAYER0, 'IMP', 5)", 0x988f748833e861daULL}, // 1 bytes; ok:
    {"lua:SetCreatureTendencies(PLAYER0, 'IMPRISON', true)", 0x9c40eb0c0c783c24ULL}, // 0 bytes; error: [string "SetCreatureTendencies(PLAYER0, 'IMPRISON', true)"]:1: bad argument #3 to 'SetCreatureTendencies' (number
    {"lua:SetCreatureTendencies(PLAYER0, 'IMPRISON', 1)", 0x80e9f5ebb3c66637ULL}, // 1 bytes; ok:
    {"lua:SetCreatureTendencies(PLAYER0, 'FLEE', 0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:CreatureEntranceLevel(PLAYER0, 3)", 0xe329996b72d0c277ULL}, // 1 bytes; ok:
    {"lua:ChangeCreaturesAnnoyance(PLAYER0, 'IMP', 'SET', 100)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:Research(PLAYER0, 'MAGIC', 'POWER_LIGHTNING', 100)", 0x78466ecbf42377f9ULL}, // 2 bytes; ok:
    {"lua:Research(PLAYER0, 'ROOM', 'TEMPLE', 100)", 0x0d01890e3122ebe9ULL}, // 2 bytes; ok:
    {"lua:Research_order(PLAYER0, 'ROOM', 'TEMPLE', 100)", 0x7a459f30c6d0109bULL}, // 5 bytes; ok:
    {"lua:SetDigger(PLAYER0, 'TROLL')", 0xff87f3456052c7acULL}, // 1 bytes; ok:
    {"lua:SetTexture(PLAYER0, 'ANCIENT')", 0x93cb4310e54060d3ULL}, // 433 bytes; ok:
    {"lua:SetHandGraphic(PLAYER0, 'Default')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:ComputerDigToLocation(PLAYER1, 1, 2)", 0x1698df8e4bfa51b7ULL}, // 34 bytes; ok:
    {"lua:SetComputerProcess(PLAYER1, 'BUILD ALL ROOM 3x3', 0, 1, 2, 3, 4)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetComputerChecks(PLAYER1, 'CHECK MONEY', 100, 1, 2, 3, 4)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetComputerGlobals(PLAYER1, 1, 2, 3, 4, 5, 6, 7)", 0x52c2091f06e5ead6ULL}, // 7 bytes; ok:
    {"lua:SetComputerEvent(PLAYER1, 'EVENT DUNGEON BREACH', 100, 1, 2, 3, 0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:UseSpecialIncreaseLevel(PLAYER0)", 0x0787d447794485b1ULL}, // 11 bytes; ok:
    {"lua:UseSpecialIncreaseLevel(PLAYER0, -1)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:UseSpecialMultiplyCreatures(PLAYER0, 1)", 0xfcedd90db67629c9ULL}, // 702 bytes; ok:
    {"lua:UseSpecialTransferCreature(PLAYER0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:UsePower(PLAYER0, 'POWER_OBEY', true)", 0xe348ed92ea698ba3ULL}, // 12 bytes; ok:
    {"lua:UsePowerAtLocation(PLAYER0, 1, 'POWER_LIGHTNING', 1, true)", 0xdf9b0acd93120083ULL}, // 101 bytes; ok:
    {"lua:UsePowerAtPos(PLAYER0, 40, 40, 'POWER_CAVE_IN', 1, true)", 0x1331e64c9ef55da2ULL}, // 22 bytes; ok:
    {"lua:UsePowerOnCreature(C0(), 'POWER_SPEED', 1, PLAYER0, true)", 0x5916f71d9b511c78ULL}, // 6 bytes; ok:
    {"lua:UseSpellOnCreature(C0(), 'SPELL_ARMOUR', 1)", 0xa60a16cf21a3e839ULL}, // 109 bytes; ok:
    {"lua:return PayForPower(PLAYER0, 'POWER_LIGHTNING', 1, false)", 0xfeb26bd571fb9db0ULL}, // 4 bytes; ok: true
    {"lua:SetMusic(2)", 0xcd7c9f7ee37b52e0ULL}, // 1 bytes; ok:
    {"lua:ZoomToLocation(PLAYER0, 1)", 0x6bb03b5a4c9f74f8ULL}, // 18 bytes; ok:
    {"lua:LockPossession(PLAYER0, true)", 0xd050e81293043308ULL}, // 1 bytes; ok:
    {"lua:print('golden')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:RunDKScriptCommand('SET_FLAG(PLAYER0,FLAG0,7)')", 0x638ac129016a1d7fULL}, // 1 bytes; ok:
    {"lua:RunDKScriptCommand('NOT_A_COMMAND(1)')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return GetCreatureNear(40, 40)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return GetCreatureByCriterion(PLAYER0, 'IMP', 'MOST_EXPERIENCED')", 0x9dc9af19484c0d93ULL}, // 0 bytes; ok: Thing#22
    {"lua:return GetThingByIdx(1)", 0x51cb988b88bcaa0aULL}, // 0 bytes; ok: Thing#1
    {"lua:return #GetThingsOfClass('Creature')", 0xf0365ea70558d7faULL}, // 0 bytes; ok: 11
    {"lua:return GetThingsOnSubtile(40, 40)", 0xc2f504a80d6ecc14ULL}, // 0 bytes; ok: {}
    {"lua:return GetThingsOnSlab(13, 13)", 0xc2f504a80d6ecc14ULL}, // 0 bytes; ok: {}
    {"lua:return GetString(1)", 0xac3653cfc3cf4b45ULL}, // 0 bytes; ok: "This is a message. Right click to delete it."
    {"lua:return GetFloorHeight(40, 40)", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')", 0xe74cbcf072c37102ULL}, // 0 bytes; ok: {1={__class="Room",creation_turn=0,room_idx=1},10={__class="Room",creation_turn=0,room_idx=6},2={__class="Room",crea
    {"lua:return LoadCustomSound('golden', 'no_such_file.wav')", 0x127102a7189eec6eULL}, // 0 bytes; ok: -1
    {"lua:return GetCustomSoundId('golden')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return IsCustomSoundLoaded('golden')", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:SetCreatureSound('IMP', 'HURT', 'golden')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return PlaySound(1)", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:StopSound(1)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:PlayMusic(1)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:StopMusic()", 0xb109117ee37b52e0ULL}, // 1 bytes; ok:
    {"lua:return CreateLens('golden')", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return GetActiveLens()", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return SetActiveLens(0)", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return IsLensEnabled('MIST')", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:return BuildDarkeningLUT(0.5)", 0xa41a9a494c57371cULL}, // 0 bytes; ok: {1=0,10=82,100=64,101=97,102=97,103=98,104=98,105=99,106=101,107=84,108=85,109=86,...(256)}
    {"lua:return PLAYER0.MONEY", 0xae01e71261706a0fULL}, // 0 bytes; ok: 10000
    {"lua:PLAYER0.MONEY = 1234 return PLAYER0.MONEY", 0x70c5aa9723343fecULL}, // 4 bytes; ok: 1234
    {"lua:PLAYER0.MONEY = 25000 return PLAYER0.MONEY", 0x7c183b468e3def7bULL}, // 4 bytes; ok: 25000
    {"lua:PLAYER0.HEART_HEALTH = 1000 return PLAYER0.HEART_HEALTH, PLAYER0.heart.health", 0x0993589ff98afe19ULL}, // 2 bytes; ok: 1000, 1000
    {"lua:PLAYER0.max_creatures = 5 return PLAYER0.max_creatures", 0xa0b5c0a909334dceULL}, // 1 bytes; ok: 5
    {"lua:PLAYER0.GAME_TURN = 5 return PLAYER0.GAME_TURN", 0xdfba0ca6fc3fac39ULL}, // 0 bytes; ok: 30
    {"lua:PLAYER0.FLAG3 = 9 return PLAYER0.FLAG3", 0xa0563fa7fbb8832aULL}, // 1 bytes; ok: 9
    {"lua:PLAYER0.NOT_A_FIELD = 1", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return PLAYER0.type", 0x65fdfc98c7cb4b0fULL}, // 0 bytes; ok: "Human"
    {"lua:return PLAYER1.type", 0x1b8ac8c745de9f71ULL}, // 0 bytes; ok: "Inactive"
    {"lua:return PLAYER_GOOD.type", 0xf346ae77d8a95209ULL}, // 0 bytes; ok: "Roaming"
    {"lua:return PLAYER0.max_creatures", 0xdfba0ca6fc3fac39ULL}, // 0 bytes; ok: 30
    {"lua:return PLAYER0.player_name", 0x71e3c5a74eb98200ULL}, // 0 bytes; ok: ""
    {"lua:PLAYER0.player_name = 'golden' return PLAYER0.player_name", 0xfc8a897bbc116d90ULL}, // 6 bytes; ok: "golden"
    {"lua:return PLAYER0.colour", 0x77ef15ee1b657341ULL}, // 0 bytes; ok: "RED"
    {"lua:PLAYER0.colour = 'BLUE' return PLAYER0.colour", 0x49e25b9ebccf808cULL}, // 2012 bytes; ok: "BLUE"
    {"lua:return PLAYER0.heart", 0xb6ed2a19564c29d2ULL}, // 0 bytes; ok: Thing#12
    {"lua:return PLAYER0.available('IMP')", 0xe51c2d4eebe25b10ULL}, // 0 bytes; error: bad argument #2 to '?' (unrecognized command)
    {"lua:return PLAYER0:available('IMP')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return PLAYER0:available('TOTAL_CREATURES')", 0xe71335a7000a0869ULL}, // 0 bytes; ok: 23
    {"lua:return PLAYER0:controls('IMP')", 0x7d7fcf55b7992f6aULL}, // 0 bytes; ok: 4
    {"lua:return PLAYER0.camera", 0x3f99cc654f7e5e1eULL}, // 0 bytes; ok: Player#0
    {"lua:PLAYER0:add_gold(500) return PLAYER0.MONEY", 0xbc403114febbac96ULL}, // 4 bytes; ok: 10500
    {"lua:PLAYER0:set_texture('ANCIENT')", 0x93cb4310e54060d3ULL}, // 433 bytes; ok:
    {"lua:return tostring(PLAYER0)", 0x83c8a41769a5d60dULL}, // 0 bytes; ok: "PLAYER0"
    {"lua:return PLAYER0 == PLAYER0", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return PLAYER0 == PLAYER1", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:return PLAYER0.NOT_A_FIELD", 0x663e3a9e1fe048c5ULL}, // 0 bytes; error: [string "return PLAYER0.NOT_A_FIELD"]:1: Unknown field or method 'NOT_A_FIELD' for Player
    {"lua:return Map.width, Map.height, Map.map_number, Map.map_name, Map.map_type, Map.campaign, Map.default_texture", 0x1aaf55dad08febfdULL}, // 0 bytes; ok: 85, 85, 11, "Hearth", "Campaign", "Dungeon Keeper original campaign", "NONE"
    {"lua:return Map.creature_pool", 0xa25801881e69f1fbULL}, // 0 bytes; ok: {BILE_DEMON=10,DARK_MISTRESS=20,DEMONSPAWN=20,DRAGON=15,HELL_HOUND=30,ORC=15,SORCEROR=30,TROLL=20}
    {"lua:Map.width = 10 return Map.width", 0x67e4e21b40a1f58dULL}, // 0 bytes; error: [string "Map.width = 10 return Map.width"]:1: MAP field 'width' is read-only
    {"lua:local s = GetSlab(13, 13) return s.kind, s.owner, s.revealed, s.style, s.room, s.centerpos", 0x7b9d3ec81ec06e22ULL}, // 0 bytes; ok: "WATER", Player#5, false, "NONE", nil, {val_x=10368,val_y=10368,val_z=0}
    {"lua:local s = GetSlab(13, 13) s.kind = 'LAVA' return s.kind", 0xcd230e229cf966b3ULL}, // 634 bytes; ok: "LAVA"
    {"lua:local s = GetSlab(13, 13) s.owner = PLAYER_GOOD return s.owner", 0xd5b77636e861a653ULL}, // 1 bytes; ok: Player#4
    {"lua:return GetSlab(13, 13):GET_CREATURES()", 0xc2f504a80d6ecc14ULL}, // 0 bytes; ok: {}
    {"lua:return GetSlab(500, 500)", 0x5c7137c99d4698e4ULL}, // 0 bytes; error: bad argument #1 to '?' (x slab coord out of range)
    {"lua:local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] return r.type, r.owner, r.slabs_count, r.health, r.max_health, r.efficiency", 0x800207bf4097edb3ULL}, // 0 bytes; ok: "ENTRANCE", Player#0, 9, 18, 18, 219
    {"lua:local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] return r.used_capacity, r.max_capacity, r.capacity_used_for_storage, r.centerpos, r.workers, r.slabs", 0x2c9a5e726bd2193eULL}, // 0 bytes; ok: 0, 9, 0, {val_x=32640,val_y=25728,val_z=0}, {}, {1={__class="Slab",slb_x=41,slb_y=32},2={__class="Slab",slb_x=42,slb
    {"lua:local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] r.health = 10 return r.health", 0xc8d2a8f0caeff975ULL}, // 1 bytes; ok: 10
    {"lua:local c = PLAYER0.camera return c.pos, c.yaw, c.pitch, c.roll, c.zoom, c.horizontal_fov, c.view_mode", 0x826ce16faf6be3a3ULL}, // 0 bytes; ok: {val_x=32611,val_y=21251,val_z=0}, 256, -266, 0, 8192, 94, 2
    {"lua:PLAYER0.camera.zoom = 2000 return PLAYER0.camera.zoom", 0x64157c7c3c69ed97ULL}, // 2 bytes; ok: 2000
    {"lua:return C0():isValid()", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return tostring(C0())", 0xf5b72f85c1bf0886ULL}, // 0 bytes; ok: "Thing (id: 19 turn: 0 creature IMP)"
    {"lua:return C0() == C0()", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return C0().NOT_A_FIELD", 0x68db543597519e46ULL}, // 0 bytes; error: [string "return C0().NOT_A_FIELD"]:1: Unknown field or method 'NOT_A_FIELD' for Creature thing
    {"lua:C0():walk_to(40, 40)", 0x09f9c65f64f53713ULL}, // 25 bytes; ok:
    {"lua:C0():kill()", 0x68698627baad733eULL}, // 97 bytes; ok:
    {"lua:C0():stun(100)", 0xeed1ee291fda19d4ULL}, // 3 bytes; ok:
    {"lua:C0():remove_from_play(100)", 0x5567a3e361d2c8afULL}, // 8 bytes; ok:
    {"lua:C0():destroy()", 0x7f4f99c03d688b18ULL}, // 0 bytes; error: [string "C0():destroy()"]:1: calling 'destroy' on bad self (Expected an object)
    {"lua:C0():delete()", 0x5dec7ab4a7a6f5aaULL}, // 109 bytes; ok:
    {"lua:C0():transform('TROLL', 2)", 0xf81aadad2f591a1dULL}, // 177 bytes; ok:
    {"lua:C0():transfer(PLAYER_GOOD)", 0x457a8d3f41c76ebcULL}, // 140 bytes; ok:
    {"lua:C0():level_up(2)", 0xca06210f3fa96b49ULL}, // 6 bytes; ok:
    {"lua:C0():teleport(1, 'EFFECT_EXPLOSION_1')", 0x3e50883399750c33ULL}, // 112 bytes; ok:
    {"lua:C0():change_owner(PLAYER_GOOD)", 0x3eb95d6b8e3dd422ULL}, // 13 bytes; ok:
    {"lua:C0():set_velocity(10, 0, 0)", 0xd3a01708e616917cULL}, // 9 bytes; ok:
    {"lua:return C0():get_annoyance('NOT_PAID')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:C0():set_annoyance('HUNGRY', 100) return C0():get_annoyance('HUNGRY')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0():in_enemy_custody()", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:C0():make_thing_zombie()", 0x40771875271bad5bULL}, // 1 bytes; ok:
    {"lua:C0():set_start_state('CREATURE_STATE_IDLE')", 0xc46bfea98a92bdbdULL}, // 2 bytes; ok:
    {"lua:return HERO()", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return PLAYER0.heart.health, PLAYER0.heart.max_health, PLAYER0.heart.thing_class, PLAYER0.heart.model", 0x270e36f3287950feULL}, // 0 bytes; ok: 30000, 30000, "OBJECT", "SOUL_CONTAINER"
    {"lua:PLAYER0.heart.health = 100 return PLAYER0.heart.health", 0x0aae2f2ae7e9f744ULL}, // 2 bytes; ok: 100
    {"lua:PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true) local t = GetThingsOnSubtile(100, 112, 'Trap')[1] return t, t.shots, t.revealed, t.rearm_turn, t.shooting_finished_turn", 0x5a89d9c82e6fe8c1ULL}, // 41 bytes; ok: Thing#126, 1, 0, 30, 0
    {"lua:PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true) local t = GetThingsOnSubtile(100, 112, 'Trap')[1] t.shots = 5 t.revealed = 1 t.rearm_turn = 10 return t.shots, t.revealed, t.rearm_turn", 0x8afeabadc23d842fULL}, // 42 bytes; ok: 5, 1, 10
    {"lua:return C0().ThingIndex", 0xf01c5ea70543c342ULL}, // 0 bytes; ok: 19
    {"lua:return C0().creation_turn", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().model", 0x0b80add5bc0cb7b8ULL}, // 0 bytes; ok: "IMP"
    {"lua:return C0().owner", 0x3f99cc654f7e5e1eULL}, // 0 bytes; ok: Player#0
    {"lua:return C0().pos", 0x4bcc1059d25c96abULL}, // 0 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:return C0().orientation", 0xb7484f2a6103faadULL}, // 0 bytes; ok: 1024
    {"lua:return C0().pitch", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().health", 0xbbdf68a6e799dc78ULL}, // 0 bytes; ok: 75
    {"lua:return C0().anim_sprite", 0xfab0fead48ea0cc9ULL}, // 0 bytes; ok: 555
    {"lua:return C0().anim_speed", 0x9e746ec50752c4f0ULL}, // 0 bytes; ok: 253
    {"lua:return C0().sprite_size", 0x69c845bea10535c1ULL}, // 0 bytes; ok: 300
    {"lua:return C0().sprite_size_min", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().sprite_size_max", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().transformation_speed", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().clipbox_size_xy", 0xc9c83bc51fdf5470ULL}, // 0 bytes; ok: 200
    {"lua:return C0().clipbox_size_z", 0x9e646ec5074604bdULL}, // 0 bytes; ok: 256
    {"lua:return C0().solid_size_xy", 0xa78d97c50c993ffcULL}, // 0 bytes; ok: 240
    {"lua:return C0().solid_size_z", 0xb49dcfb5adceefb2ULL}, // 0 bytes; ok: 400
    {"lua:return C0().max_health", 0xbbdf68a6e799dc78ULL}, // 0 bytes; ok: 75
    {"lua:return C0().picked_up", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:return C0().thing_class", 0xf383cc4140d418b9ULL}, // 0 bytes; ok: "CREATURE"
    {"lua:return C0().parent", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return C0().name", 0x959b437ff40cde08ULL}, // 0 bytes; ok: "Pepe"
    {"lua:return C0().gold_held", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().party", 0xc2f504a80d6ecc14ULL}, // 0 bytes; ok: {}
    {"lua:return C0().level", 0x7d89cf55b7a183efULL}, // 0 bytes; ok: 1
    {"lua:return C0().max_speed", 0xab5b16a6de78df09ULL}, // 0 bytes; ok: 96
    {"lua:return C0().exp_points", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().creature_kills", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().creature_kills_enemies", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().creature_kills_allies", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().hunger_amount", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().hunger_level", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().hunger_loss", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().lair", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return C0().opponents_melee_count", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().opponents_ranged_count", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().opponents_count", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().battle_enemy", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return C0().combat_type", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return C0().battle_id", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().force_health_flower_displayed", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().force_health_flower_hidden", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().hand_blocked_turns", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().countdown", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().state", 0xb5a2dbf8ac1cacabULL}, // 0 bytes; ok: "MoveToPosition"
    {"lua:return C0().state_besides_interruptions", 0xf69b13b5b17315a8ULL}, // 0 bytes; ok: "ImpDoingNothing"
    {"lua:return C0().continue_state", 0xf69b13b5b17315a8ULL}, // 0 bytes; ok: "ImpDoingNothing"
    {"lua:return C0().instance", 0x4523912f945ab885ULL}, // 0 bytes; ok: "NULL"
    {"lua:return C0().workroom", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return C0().moveto_pos", 0x4bcc1059d25c96abULL}, // 0 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:return C0().flee_pos", 0x5295f2a7f624f7bdULL}, // 0 bytes; ok: {val_x=32640,val_y=31872,val_z=256}
    {"lua:return C0().patrol_pos", 0xf10272f3a7a89d3cULL}, // 0 bytes; ok: {val_x=0,val_y=0,val_z=0}
    {"lua:return C0().patrol_countdown", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().party_objective", 0x71e3c5a74eb98200ULL}, // 0 bytes; ok: ""
    {"lua:return C0().party_original_objective", 0x71e3c5a74eb98200ULL}, // 0 bytes; ok: ""
    {"lua:return C0().party_target_player", 0x3f99cc654f7e5e1eULL}, // 0 bytes; ok: Player#0
    {"lua:return C0().conscious_back_turns", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0().unsummon_duration", 0xe095c0eec9a4e366ULL}, // 0 bytes; ok: -30
    {"lua:return C0().familiars", 0xc2f504a80d6ecc14ULL}, // 0 bytes; ok: {}
    {"lua:return C0().shots", 0xa35e0157a06489eaULL}, // 0 bytes; error: [string "return C0().shots"]:1: Unknown field or method 'shots' for Creature thing
    {"lua:return C0().revealed", 0x858ca873f6e74d48ULL}, // 0 bytes; error: [string "return C0().revealed"]:1: Unknown field or method 'revealed' for Creature thing
    {"lua:return C0().rearm_turn", 0x6eb6d5475c1ac2c6ULL}, // 0 bytes; error: [string "return C0().rearm_turn"]:1: Unknown field or method 'rearm_turn' for Creature thing
    {"lua:return C0().shooting_finished_turn", 0x51e5caf0066080f0ULL}, // 0 bytes; error: [string "return C0().shooting_finished_turn"]:1: Unknown field or method 'shooting_finished_turn' for Creature th
    {"lua:return C0().box_kind", 0xc51496873a504ac4ULL}, // 0 bytes; error: [string "return C0().box_kind"]:1: Unknown field or method 'box_kind' for Creature thing
    {"lua:return C0().target", 0x62381b2e4b1f4f62ULL}, // 0 bytes; error: [string "return C0().target"]:1: Unknown field or method 'target' for Creature thing
    {"lua:return C0().damage", 0xd45b58ba4a52a6f6ULL}, // 0 bytes; error: [string "return C0().damage"]:1: Unknown field or method 'damage' for Creature thing
    {"lua:return C0().originpos", 0xa45d4785ba4d5356ULL}, // 0 bytes; error: [string "return C0().originpos"]:1: Unknown field or method 'originpos' for Creature thing
    {"lua:local c = C0() c.orientation = 1024 return c.orientation", 0xb7484f2a6103faadULL}, // 0 bytes; ok: 1024
    {"lua:local c = C0() c.pitch = 100 return c.pitch", 0xa551d6b91835882dULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.owner = PLAYER_GOOD return c.owner", 0x36e0a55fad21f564ULL}, // 13 bytes; ok: Player#4
    {"lua:local c = C0() c.health = 50 return c.health", 0x6fc6fc6a232a7d79ULL}, // 1 bytes; ok: 50
    {"lua:local c = C0() c.health = 5000 return c.health", 0x14149898c7e7608eULL}, // 2 bytes; ok: 5000
    {"lua:local c = C0() c.health = -1 return c.health", 0xcccd6de140159d6eULL}, // 8 bytes; ok: -1
    {"lua:local c = C0() c.pos = {val_x = c.pos.val_x + 256, val_y = c.pos.val_y, val_z = c.pos.val_z} return c.pos", 0x4eadf677e788e3adULL}, // 5 bytes; ok: {val_x=32640,val_y=33664,val_z=768}
    {"lua:local c = C0() c.anim_sprite = 1 return c.anim_sprite", 0x7b2707a232653e98ULL}, // 2 bytes; ok: 1
    {"lua:local c = C0() c.anim_speed = 64 return c.anim_speed", 0x2ac4ef6235b20545ULL}, // 1 bytes; ok: 64
    {"lua:local c = C0() c.sprite_size = 200 return c.sprite_size", 0x3723798f5ca5c40fULL}, // 2 bytes; ok: 200
    {"lua:local c = C0() c.sprite_size_min = 100 return c.sprite_size_min", 0x21a316041b89f829ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.sprite_size_max = 300 return c.sprite_size_max", 0x41a4e56489a4c5d2ULL}, // 2 bytes; ok: 300
    {"lua:local c = C0() c.transformation_speed = 10 return c.transformation_speed", 0xe11daaae439e5872ULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.clipbox_size_xy = 100 return c.clipbox_size_xy", 0x3096a4f34b9eac05ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.clipbox_size_z = 100 return c.clipbox_size_z", 0xb32b23c8e817921cULL}, // 2 bytes; ok: 100
    {"lua:local c = C0() c.solid_size_xy = 100 return c.solid_size_xy", 0xea97e4b936a93015ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.solid_size_z = 100 return c.solid_size_z", 0x78d60502cf4d9a5cULL}, // 2 bytes; ok: 100
    {"lua:local c = C0() c.name = 'Golden' return c.name", 0xfb092a6b73856624ULL}, // 6 bytes; ok: "Golden"
    {"lua:local c = C0() c.gold_held = 500 return c.gold_held", 0xfd8b17356a96dda0ULL}, // 2 bytes; ok: 500
    {"lua:local c = C0() c.exp_points = 1000 return c.exp_points", 0x6c80f0e416e98be2ULL}, // 2 bytes; ok: 1000
    {"lua:local c = C0() c.moveto_pos = c.pos return c.moveto_pos", 0x4bcc1059d25c96abULL}, // 0 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:local c = C0() c.flee_pos = c.pos return c.flee_pos", 0x6cb80c20d7428c82ULL}, // 3 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:local c = C0() c.max_speed = 100 return c.max_speed", 0x0928fe63c54fb08bULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.patrol_pos = c.pos return c.patrol_pos", 0x87dcae62a6213261ULL}, // 5 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:local c = C0() c.patrol_countdown = 10 return c.patrol_countdown", 0x38eafe5247246372ULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.party_objective = 'ATTACK_DUNGEON_HEART' return c.party_objective", 0xfca98e4a8202ba67ULL}, // 1 bytes; ok: "ATTACK_DUNGEON_HEART"
    {"lua:local c = C0() c.party_original_objective = 'ATTACK_DUNGEON_HEART' return c.party_original_objective", 0xa8608cd69a085b94ULL}, // 1 bytes; ok: "ATTACK_DUNGEON_HEART"
    {"lua:local c = C0() c.party_target_player = PLAYER0 return c.party_target_player", 0x3f99cc654f7e5e1eULL}, // 0 bytes; ok: Player#0
    {"lua:local c = C0() c.countdown = 100 return c.countdown", 0xf97ecc61bd02caacULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.state = 1 return c.state", 0xbcfa90d1dda70203ULL}, // 2 bytes; ok: "ImpDoingNothing"
    {"lua:local c = C0() c.continue_state = 1 return c.continue_state", 0xf69b13b5b17315a8ULL}, // 0 bytes; ok: "ImpDoingNothing"
    {"lua:local c = C0() c.instance = 1 return c.instance", 0xaddb4be2d0063fdfULL}, // 5 bytes; ok: "SWING_WEAPON_SWORD"
    {"lua:local c = C0() c.hunger_amount = 5 return c.hunger_amount", 0x118eab49aed19d8dULL}, // 1 bytes; ok: 5
    {"lua:local c = C0() c.hunger_level = 5 return c.hunger_level", 0xc2181c6eb7a37d3dULL}, // 1 bytes; ok: 5
    {"lua:local c = C0() c.creature_kills = 3 return c.creature_kills", 0x1a6425d9a0ca2d85ULL}, // 1 bytes; ok: 3
    {"lua:local c = C0() c.creature_kills_allies = 1 return c.creature_kills_allies", 0x1f9ab60f9b20620bULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.creature_kills_enemies = 2 return c.creature_kills_enemies", 0xf8cb41128cf2ffacULL}, // 1 bytes; ok: 2
    {"lua:local c = C0() c.hunger_loss = 1 return c.hunger_loss", 0xed478d22aad059deULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.hand_blocked_turns = 10 return c.hand_blocked_turns", 0x42d761304331a3f6ULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.force_health_flower_displayed = true return c.force_health_flower_displayed", 0x9812f746eb37e3e3ULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.force_health_flower_hidden = true return c.force_health_flower_hidden", 0xba8db5fe9bdafb10ULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.conscious_back_turns = 10 return c.conscious_back_turns", 0x2c14b1120afbfcbaULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.unsummon_duration = 100 return c.unsummon_duration", 0xfb136049402d9a00ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.shots = 3 return c.shots", 0x153051fa350edf4fULL}, // 0 bytes; error: [string "local c = C0() c.shots = 3 return c.shots"]:1: Field 'shots' is not writable on Creature thing
    {"lua:local c = C0() c.revealed = true return c.revealed", 0x209a3d8421e522d0ULL}, // 0 bytes; error: [string "local c = C0() c.revealed = true return c.rev..."]:1: Field 'revealed' is not writable on Creature thing
    {"lua:local c = C0() c.rearm_turn = 10 return c.rearm_turn", 0x97bae7c098ff1f02ULL}, // 0 bytes; error: [string "local c = C0() c.rearm_turn = 10 return c.rea..."]:1: Field 'rearm_turn' is not writable on Creature thi
    {"lua:local c = C0() c.shooting_finished_turn = 10 return c.shooting_finished_turn", 0xcbdccd97ae54509fULL}, // 0 bytes; error: [string "local c = C0() c.shooting_finished_turn = 10 ..."]:1: Field 'shooting_finished_turn' is not writable on
    {"lua:local c = C0() c.box_kind = 1 return c.box_kind", 0xd66348cb19f98e02ULL}, // 0 bytes; error: [string "local c = C0() c.box_kind = 1 return c.box_kind"]:1: Field 'box_kind' is not writable on Creature thing
    {"lua:local c = C0() c.target = c return c.target", 0xc2ee4407acb10dc5ULL}, // 0 bytes; error: [string "local c = C0() c.target = c return c.target"]:1: Field 'target' is not writable on Creature thing
    {"lua:local c = C0() c.damage = 5 return c.damage", 0xe712492e03997b6fULL}, // 0 bytes; error: [string "local c = C0() c.damage = 5 return c.damage"]:1: Field 'damage' is not writable on Creature thing
