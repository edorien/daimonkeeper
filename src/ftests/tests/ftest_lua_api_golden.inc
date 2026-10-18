    {"lua:SetGenerateSpeed(200)", 0xd1e39964378d1090ULL}, // 36 bytes; ok:
    {"lua:SetGenerateSpeed(200, PLAYER0)", 0x10e9c74a2f12cfe8ULL}, // 4 bytes; ok:
    {"lua:ComputerPlayer(PLAYER1, 0)", 0x3742f0229ba86dfeULL}, // 37975 bytes; ok:
    {"lua:ComputerPlayer(PLAYER1, 'ROAMING')", 0x16477d427a21af8cULL}, // 2 bytes; ok:
    {"lua:AllyPlayers(PLAYER0, PLAYER_GOOD, 1)", 0xd8f53659f1c002dcULL}, // 2530 bytes; ok:
    {"lua:AllyPlayers(PLAYER0, PLAYER_GOOD)", 0x5d48ae1859796ce0ULL}, // 0 bytes; error: [string "AllyPlayers(PLAYER0, PLAYER_GOOD)"]:1: bad argument #3 to 'AllyPlayers' (number expected, got no value)
    {"lua:StartMoney(PLAYER0, 5000)", 0xc5738c7274006ef4ULL}, // 4 bytes; ok:
    {"lua:StartMoney(ALL_PLAYERS, 0)", 0xf547145dc27533d1ULL}, // 0 bytes; error: [string "StartMoney(ALL_PLAYERS, 0)"]:1: bad argument #1 to 'StartMoney' (string expected, got nil)
    {"lua:MaxCreatures(PLAYER0, 3)", 0xe3b5c3cf16aaddf7ULL}, // 1 bytes; ok:
    {"lua:AddCreatureToPool('TROLL', 4)", 0x31fe0b8af3a61d86ULL}, // 1 bytes; ok:
    {"lua:AddCreatureToPool('NOT_A_CREATURE', 4)", 0x7294eaebbdc57f4eULL}, // 0 bytes; error: [string "AddCreatureToPool('NOT_A_CREATURE', 4)"]:1: bad argument #1 to 'AddCreatureToPool' (unrecognized command
    {"lua:CreatureAvailable(PLAYER0, 'TROLL', true, 2)", 0xaa1a694efa2d8747ULL}, // 1 bytes; ok:
    {"lua:DeadCreaturesReturnToPool(false)", 0x9917b891b1a236aeULL}, // 1 bytes; ok:
    {"lua:RoomAvailable(PLAYER0, 'TEMPLE', 1, true)", 0xcf6767a97df3921dULL}, // 1 bytes; ok:
    {"lua:RoomAvailable(PLAYER0, 'NOT_A_ROOM', 1, true)", 0x2d25461400b29527ULL}, // 0 bytes; error: [string "RoomAvailable(PLAYER0, 'NOT_A_ROOM', 1, true)"]:1: bad argument #2 to 'RoomAvailable' (unrecognized comm
    {"lua:MagicAvailable(PLAYER0, 'POWER_LIGHTNING', true, true)", 0xd0fa6b08f802cd07ULL}, // 1 bytes; ok:
    {"lua:DoorAvailable(PLAYER0, 'BRACED', true, 3)", 0x2ee8e90755fa4c77ULL}, // 3 bytes; ok:
    {"lua:TrapAvailable(PLAYER0, 'BOULDER', true, 2)", 0x869b515db45f6640ULL}, // 3 bytes; ok:
    {"lua:WinGame(PLAYER0)", 0x55a57b853d0622f6ULL}, // 19 bytes; ok:
    {"lua:WinGame()", 0x4c893a35a906e0b6ULL}, // 51 bytes; ok:
    {"lua:LoseGame(PLAYER0)", 0xb6fba97825c7727bULL}, // 18 bytes; ok:
    {"lua:return CountCreaturesAtActionPoint(1, PLAYER0, 'ANY_CREATURE')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return CountCreaturesAtActionPoint(1, ALL_PLAYERS, 'IMP')", 0x1792fc2ddccfa36eULL}, // 0 bytes; error: bad argument #2 to '?' (string expected, got nil)
    {"lua:SetTimer(PLAYER0, 'TIMER0')", 0xd747107aa2aeb3aeULL}, // 2 bytes; ok:
    {"lua:AddToTimer(PLAYER0, 'TIMER0', 100)", 0xf5b8a31b4bcbd7f0ULL}, // 8 bytes; ok:
    {"lua:DisplayTimer(PLAYER0, 'TIMER0', 1000, true)", 0xb84aab25093a02bdULL}, // 2 bytes; ok:
    {"lua:HideTimer()", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:BonusLevelTime(1000)", 0xbe47aff48b321797ULL}, // 3 bytes; ok:
    {"lua:BonusLevelTime(1000, 1)", 0x8233ecef230d4b78ULL}, // 4 bytes; ok:
    {"lua:BonusLevelTime(1000, 0)", 0xbe47aff48b321797ULL}, // 3 bytes; ok:
    {"lua:BonusLevelTime(1000, true)", 0x8233ecef230d4b78ULL}, // 4 bytes; ok:
    {"lua:AddBonusTime(500)", 0x409b5d559537a21dULL}, // 2 bytes; ok:
    {"lua:ResetActionPoint(1, PLAYER0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetNextLevel(12)", 0x22d6e7c4e468deadULL}, // 1 bytes; ok:
    {"lua:TriggerActionPoint(1, PLAYER0)", 0x35083e7aabec8f85ULL}, // 1 bytes; ok:
    {"lua:return IsActionpointActivatedByPlayer(PLAYER0, 1)", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:return AddCreatureToLevel(PLAYER0, 'TROLL', 1, 3, 100)", 0x304c8b899286b223ULL}, // 73 bytes; ok: Thing#126
    {"lua:return AddCreatureToLevel(PLAYER_GOOD, 'KNIGHT', PLAYER0, 5, 0)", 0x4183ae6b33db874cULL}, // 73 bytes; ok: Thing#126
    {"lua:return AddCreatureToLevel(PLAYER0, 'IMP', {stl_x = 40, stl_y = 40}, 1, 0)", 0x0783f0230a8b2f9aULL}, // 63 bytes; ok: Thing#126
    {"lua:return AddCreatureToLevel(PLAYER0, 'NOT_A_CREATURE', 1, 1, 0)", 0xe51c2d4eebe25b10ULL}, // 0 bytes; error: bad argument #2 to '?' (unrecognized command)
    {"lua:return AddTunnellerToLevel(PLAYER_GOOD, 1, 'DUNGEON_HEART', 0, 2, 100)", 0x3c0a020bc0a55413ULL}, // 108 bytes; ok: Thing#126
    {"lua:CreateParty('GOLDEN')", 0xf221bc41006abbc9ULL}, // 7 bytes; ok:
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'KNIGHT', 3, 100, 'ATTACK_DUNGEON_HEART', 0)", 0xbf415fe94f77638cULL}, // 14 bytes; ok:
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'KNIGHT', 3, 100, 'ATTACK_DUNGEON_HEART', 0) DeleteFromParty('GOLDEN', 'KNIGHT', 3)", 0xe0be0eca80d394feULL}, // 13 bytes; ok:
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'ARCHER', 2, 0, 'ATTACK_DUNGEON_HEART', 0) return AddPartyToLevel(PLAYER_GOOD, 'GOLDEN', 1)", 0x1eb16250655e95acULL}, // 119 bytes; ok: {1=Thing#126}
    {"lua:CreateParty('GOLDEN') AddToParty('GOLDEN', 'ARCHER', 2, 0, 'ATTACK_DUNGEON_HEART', 0) AddTunnellerPartyToLevel(PLAYER_GOOD, 'GOLDEN', 1, 'DUNGEON_HEART', 0, 1, 0)", 0x69522608bea18268ULL}, // 226 bytes; ok:
    {"lua:return AddPartyToLevel(PLAYER_GOOD, 'NO_SUCH_PARTY', 1)", 0xc6ac750cf662089aULL}, // 0 bytes; error: bad argument #2 to '?' (Party of requested name, 'NO_SUCH_PARTY', is not defined)
    {"lua:DisplayObjective(1)", 0x1fe1603942af07a6ULL}, // 412 bytes; ok:
    {"lua:DisplayObjective(1, 1)", 0xd9cec37ad9b6088eULL}, // 388 bytes; ok:
    {"lua:DisplayObjectiveWithPos(1, 40, 40)", 0x59f19ae96561a26eULL}, // 396 bytes; ok:
    {"lua:DisplayInformation(2)", 0x16543e5ae50bf341ULL}, // 183 bytes; ok:
    {"lua:DisplayInformationWithPos(2, 40, 40)", 0x09f8f9a5215040e1ULL}, // 215 bytes; ok:
    {"lua:QuickObjective('golden objective')", 0x15cfe68b09a86c84ULL}, // 187 bytes; ok:
    {"lua:QuickObjectiveWithPos('golden', 40, 40)", 0xa80fbc3505fa7df7ULL}, // 81 bytes; ok:
    {"lua:QuickInformation(1, 'golden info')", 0x50b47aee1ebd8be4ULL}, // 194 bytes; ok:
    {"lua:QuickInformationWithPos(1, 'golden', 40, 40)", 0xe415e5375b029324ULL}, // 221 bytes; ok:
    {"lua:DisplayPlayerObjective(1, PLAYER0)", 0xbc06bea4d2ef032aULL}, // 48 bytes; ok:
    {"lua:DisplayPlayerObjectiveWithPos(1, PLAYER0, 40, 40)", 0xb0a1d710b7506832ULL}, // 46 bytes; ok:
    {"lua:DisplayPlayerInformation(2, PLAYER0)", 0xbd33c46ae6f74e6dULL}, // 22 bytes; ok:
    {"lua:DisplayPlayerInformationWithPos(2, PLAYER0, 40, 40)", 0xb0c161398e7c4c4dULL}, // 26 bytes; ok:
    {"lua:QuickPlayerObjective('golden', PLAYER0)", 0x4cc9ee9ebeb86e13ULL}, // 13 bytes; ok:
    {"lua:QuickPlayerObjectiveWithPos('golden', PLAYER0, 40, 40)", 0xed35b0dfa1e3a25bULL}, // 11 bytes; ok:
    {"lua:QuickPlayerInformation(1, PLAYER0, 'golden')", 0x2f56f189256a7140ULL}, // 28 bytes; ok:
    {"lua:QuickPlayerInformationWithPos(1, PLAYER0, 'golden', 40, 40)", 0xc5b3f2f7bfa276a0ULL}, // 32 bytes; ok:
    {"lua:DisplayMessage(1)", 0x97de0b95277c827eULL}, // 99 bytes; ok:
    {"lua:DisplayMessage(1, PLAYER0)", 0x848035981bec7520ULL}, // 98 bytes; ok:
    {"lua:DisplayMessage(1, 'IMP')", 0xd548da2d38bbb9e0ULL}, // 100 bytes; ok:
    {"lua:QuickMessage('golden')", 0x8be810ab00c51edbULL}, // 63 bytes; ok:
    {"lua:QuickMessage('golden', PLAYER0)", 0x5e9385d6ad910c17ULL}, // 62 bytes; ok:
    {"lua:QuickMessage('golden') ClearMessage()", 0x15382f984ae86137ULL}, // 63 bytes; ok:
    {"lua:ClearMessage(1)", 0xd90874b2c4dfd0ddULL}, // 2 bytes; ok:
    {"lua:HeartLostObjective(1, PLAYER0)", 0xef1dbbd231955d8dULL}, // 3 bytes; ok:
    {"lua:HeartLostQuickObjective('golden', PLAYER0)", 0xb54c35592250ccbdULL}, // 0 bytes; error: [string "HeartLostQuickObjective('golden', PLAYER0)"]:1: bad argument #1 to 'HeartLostQuickObjective' (number exp
    {"lua:HeartLostQuickObjective(1, 'golden', PLAYER0)", 0x4db5c5029fa04f85ULL}, // 10 bytes; ok:
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
    {"lua:SetBoxTooltip(1, 'golden')", 0x67c518b9802ddd31ULL}, // 6 bytes; ok:
    {"lua:SetBoxTooltipId(1, 5)", 0x003f8ab34b9c3260ULL}, // 136 bytes; ok:
    {"lua:RevealMapLocation(PLAYER0, 1, 5)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:RevealMapRect(PLAYER0, 40, 40, 10, 10)", 0x3e20e588919db0c5ULL}, // 225 bytes; ok:
    {"lua:ConcealMapRect(PLAYER0, 40, 40, 10, 10)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:ConcealMapRect(PLAYER0, 40, 40, 10, 10, true)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:PlaceDoor(PLAYER0, 'WOOD', 33, 37, true, true)", 0x51813064238a906eULL}, // 633 bytes; ok:
    {"lua:PlaceDoor(PLAYER0, 'WOOD', 33, 37, true, true) SetDoor('UNLOCKED', 33, 37)", 0xca77019b9d151dd6ULL}, // 599 bytes; ok:
    {"lua:SetDoor('LOCKED', 33, 37)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true)", 0x46cd3687e2141aa7ULL}, // 41 bytes; ok:
    {"lua:ChangeSlabOwner(33, 37, PLAYER_GOOD)", 0xcd028b4494ee4a2aULL}, // 70 bytes; ok:
    {"lua:ChangeSlabOwner(33, 37, PLAYER_GOOD, 'MATCH')", 0x6492f8ea9438f1b2ULL}, // 270 bytes; ok:
    {"lua:ChangeSlabType(33, 37, 'LAVA')", 0x5df9f8217e153855ULL}, // 570 bytes; ok:
    {"lua:ChangeSlabType(33, 37, 'GOLD', 'FLOOR')", 0x4162a6a6be44775cULL}, // 6062 bytes; ok:
    {"lua:ChangeSlabTexture(33, 37, 'ANCIENT')", 0x5e33bd96d63ff4ecULL}, // 2 bytes; ok:
    {"lua:HideHeroGate(1, true)", 0x9e5d987df23947f6ULL}, // 25 bytes; ok:
    {"lua:AddHeartHealth(PLAYER0, -100, true)", 0xc0b1c30b14b0a321ULL}, // 21 bytes; ok:
    {"lua:AddHeartHealth(PLAYER0, 100, false)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:MakeSafe(PLAYER0)", 0x0495602884e6e91bULL}, // 87 bytes; ok:
    {"lua:MakeUnsafe(PLAYER0)", 0xab27f74f85767102ULL}, // 5234 bytes; ok:
    {"lua:LocateHiddenWorld()", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return AddObjectToLevel('GOLD_CHEST', 1, 500)", 0x6bb460271ec2ba3aULL}, // 45 bytes; ok: Thing#126
    {"lua:return AddObjectToLevel('GOLD_CHEST', 1, 500, PLAYER0, 0)", 0x0b4681271ec2ba3aULL}, // 45 bytes; ok: Thing#126
    {"lua:return AddObjectToLevelAtPos('BARREL', 100, 112, 0)", 0x7e790f202963a040ULL}, // 43 bytes; ok: Thing#126
    {"lua:return AddObjectToLevel('NOT_AN_OBJECT', 1, 0)", 0x93f67e3d178af56fULL}, // 0 bytes; error: bad argument #1 to '?' (unrecognized command)
    {"lua:return AddShotToLevel('SHOT_FIREBALL', 1, PLAYER0, 'CreaturesOnly')", 0xad876978e147a9e3ULL}, // 60 bytes; ok: Thing#126
    {"lua:AddEffectGeneratorToLevel('EFFECTGENERATOR_LAVA', 1, 5)", 0x42e61a99b85f2011ULL}, // 30 bytes; ok:
    {"lua:return AddCorpseToLevel('TROLL', 1, 2)", 0xaa4d56e210db79adULL}, // 33 bytes; ok: Thing#126
    {"lua:return AddCorpseToLevel('TROLL', 1, 2, true, PLAYER0)", 0xf0f216804a2f7420ULL}, // 31 bytes; ok: Thing#126
    {"lua:return CreateEffect('EFFECT_EXPLOSION_1', 1, 0)", 0x1fb8a6207088cb2bULL}, // 47 bytes; ok: Thing#8283
    {"lua:return CreateEffectAtPos('EFFECT_EXPLOSION_1', 40, 40, 0)", 0xe414e0901a4405dbULL}, // 47 bytes; ok: Thing#8283
    {"lua:CreateEffectsLine(1, 2, 0, 1, 1, 'EFFECT_EXPLOSION_1')", 0x461bc7afe1255800ULL}, // 23 bytes; ok:
    {"lua:NewCreatureType('GOLDEN')", 0x3083d96753f061a1ULL}, // 16 bytes; ok:
    {"lua:CopyCreatureType('TROLL', 'GOLDEN')", 0xe286ee80abc31d3cULL}, // 379 bytes; ok:
    {"lua:SetDoorConfiguration('WOOD', 'Health', 999)", 0x1a3549758715b96cULL}, // 4 bytes; ok:
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
    {"lua:SetHandRule(PLAYER0, 'IMP', 0, 'DENY', 'ALWAYS', 0)", 0x762b5cbebc3b03b9ULL}, // 2 bytes; ok:
    {"lua:SwapCreature('TROLL', 'DRAGON')", 0x4f62154f84332723ULL}, // 124 bytes; ok:
    {"lua:SetSacrificeRecipe('MKCREATURE', 'TROLL', 'FLY', 'BUG')", 0xda16f7282de6a700ULL}, // 4 bytes; ok:
    {"lua:RemoveSacrificeRecipe('FLY', 'BUG')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetCreatureInstance('IMP', 1, 'FIREBALL', 1)", 0x362be362bd4ee3c0ULL}, // 9 bytes; ok:
    {"lua:SetCreatureMaxLevel(PLAYER0, 'IMP', 5)", 0x6515d2466caf81daULL}, // 1 bytes; ok:
    {"lua:SetCreatureTendencies(PLAYER0, 'IMPRISON', true)", 0x9c40eb0c0c783c24ULL}, // 0 bytes; error: [string "SetCreatureTendencies(PLAYER0, 'IMPRISON', true)"]:1: bad argument #3 to 'SetCreatureTendencies' (number
    {"lua:SetCreatureTendencies(PLAYER0, 'IMPRISON', 1)", 0x4d7053a9ec8d8637ULL}, // 1 bytes; ok:
    {"lua:SetCreatureTendencies(PLAYER0, 'FLEE', 0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:CreatureEntranceLevel(PLAYER0, 3)", 0xafaff729ab97e277ULL}, // 1 bytes; ok:
    {"lua:ChangeCreaturesAnnoyance(PLAYER0, 'IMP', 'SET', 100)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:Research(PLAYER0, 'MAGIC', 'POWER_LIGHTNING', 100)", 0x361f36af409d77f9ULL}, // 2 bytes; ok:
    {"lua:Research(PLAYER0, 'ROOM', 'TEMPLE', 100)", 0x6c28102347d56be9ULL}, // 2 bytes; ok:
    {"lua:Research_order(PLAYER0, 'ROOM', 'TEMPLE', 100)", 0xfd1dd695b441b09bULL}, // 5 bytes; ok:
    {"lua:SetDigger(PLAYER0, 'TROLL')", 0xcc0e51039919e7acULL}, // 1 bytes; ok:
    {"lua:SetTexture(PLAYER0, 'ANCIENT')", 0x607fe8e42a9780d3ULL}, // 433 bytes; ok:
    {"lua:SetHandGraphic(PLAYER0, 'Default')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:ComputerDigToLocation(PLAYER1, 1, 2)", 0xe17e24cdd1ea91b7ULL}, // 34 bytes; ok:
    {"lua:SetComputerProcess(PLAYER1, 'BUILD ALL ROOM 3x3', 0, 1, 2, 3, 4)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetComputerChecks(PLAYER1, 'CHECK MONEY', 100, 1, 2, 3, 4)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:SetComputerGlobals(PLAYER1, 1, 2, 3, 4, 5, 6, 7)", 0xa3d86aa29c89cad6ULL}, // 7 bytes; ok:
    {"lua:SetComputerEvent(PLAYER1, 'EVENT DUNGEON BREACH', 100, 1, 2, 3, 0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:UseSpecialIncreaseLevel(PLAYER0)", 0x4370ba96acf865b1ULL}, // 11 bytes; ok:
    {"lua:UseSpecialIncreaseLevel(PLAYER0, -1)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:UseSpecialMultiplyCreatures(PLAYER0, 1)", 0x080d7a88c3ffa9c9ULL}, // 702 bytes; ok:
    {"lua:UseSpecialTransferCreature(PLAYER0)", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:UseSpecialStealHero(PLAYER0, 1)", 0x6dcf0c7644114782ULL}, // 68 bytes; ok:
    {"lua:UsePower(PLAYER0, 'POWER_OBEY', true)", 0x02beacee7d1dcba3ULL}, // 12 bytes; ok:
    {"lua:UsePowerAtLocation(PLAYER0, 1, 'POWER_LIGHTNING', 1, true)", 0x2f4c72e3b06e2083ULL}, // 101 bytes; ok:
    {"lua:UsePowerAtPos(PLAYER0, 40, 40, 'POWER_CAVE_IN', 1, true)", 0xe27e572f8d779da2ULL}, // 22 bytes; ok:
    {"lua:UsePowerOnCreature(C0(), 'POWER_SPEED', 1, PLAYER0, true)", 0x45d9e00475c51c78ULL}, // 6 bytes; ok:
    {"lua:UseSpellOnCreature(C0(), 'SPELL_ARMOUR', 1)", 0xd5cf204512684839ULL}, // 109 bytes; ok:
    {"lua:return PayForPower(PLAYER0, 'POWER_LIGHTNING', 1, false)", 0x446286d47fb29db0ULL}, // 4 bytes; ok: true
    {"lua:SetMusic(2)", 0xcd7c9f7ee37b52e0ULL}, // 1 bytes; ok:
    {"lua:ZoomToLocation(PLAYER0, 1)", 0xc334a94a14c9f4f8ULL}, // 18 bytes; ok:
    {"lua:LockPossession(PLAYER0, true)", 0x9cd745d0cbcb5308ULL}, // 1 bytes; ok:
    {"lua:print('golden')", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:RunDKScriptCommand('SET_FLAG(PLAYER0,FLAG0,7)')", 0xd6df8b3c94db3d7fULL}, // 1 bytes; ok:
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
    {"lua:PLAYER0.MONEY = 1234 return PLAYER0.MONEY", 0xb675c59630eb3fecULL}, // 4 bytes; ok: 1234
    {"lua:PLAYER0.MONEY = 25000 return PLAYER0.MONEY", 0xf1da30aadc32ef7bULL}, // 4 bytes; ok: 25000
    {"lua:PLAYER0.HEART_HEALTH = 1000 return PLAYER0.HEART_HEALTH, PLAYER0.heart.health", 0x7f0276ba44e97e19ULL}, // 2 bytes; ok: 1000, 1000
    {"lua:PLAYER0.max_creatures = 5 return PLAYER0.max_creatures", 0xf0270ae3838eadceULL}, // 1 bytes; ok: 5
    {"lua:PLAYER0.GAME_TURN = 5 return PLAYER0.GAME_TURN", 0xdfba0ca6fc3fac39ULL}, // 0 bytes; ok: 30
    {"lua:PLAYER0.FLAG3 = 9 return PLAYER0.FLAG3", 0x0a899eeb84f1e32aULL}, // 1 bytes; ok: 9
    {"lua:PLAYER0.NOT_A_FIELD = 1", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return PLAYER0.type", 0x65fdfc98c7cb4b0fULL}, // 0 bytes; ok: "Human"
    {"lua:return PLAYER1.type", 0x1b8ac8c745de9f71ULL}, // 0 bytes; ok: "Inactive"
    {"lua:return PLAYER_GOOD.type", 0xf346ae77d8a95209ULL}, // 0 bytes; ok: "Roaming"
    {"lua:return PLAYER0.max_creatures", 0xdfba0ca6fc3fac39ULL}, // 0 bytes; ok: 30
    {"lua:return PLAYER0.player_name", 0x71e3c5a74eb98200ULL}, // 0 bytes; ok: ""
    {"lua:PLAYER0.player_name = 'golden' return PLAYER0.player_name", 0x0c2223f4187fed90ULL}, // 6 bytes; ok: "golden"
    {"lua:return PLAYER0.colour", 0x77ef15ee1b657341ULL}, // 0 bytes; ok: "RED"
    {"lua:PLAYER0.colour = 'BLUE' return PLAYER0.colour", 0xbc221a731d0e808cULL}, // 2012 bytes; ok: "BLUE"
    {"lua:return PLAYER0.heart", 0xb6ed2a19564c29d2ULL}, // 0 bytes; ok: Thing#12
    {"lua:return PLAYER0.available('IMP')", 0xe51c2d4eebe25b10ULL}, // 0 bytes; error: bad argument #2 to '?' (unrecognized command)
    {"lua:return PLAYER0:available('IMP')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return PLAYER0:available('TOTAL_CREATURES')", 0xe71335a7000a0869ULL}, // 0 bytes; ok: 23
    {"lua:return PLAYER0:controls('IMP')", 0x7d7fcf55b7992f6aULL}, // 0 bytes; ok: 4
    {"lua:return PLAYER0.camera", 0x3f99cc654f7e5e1eULL}, // 0 bytes; ok: Player#0
    {"lua:PLAYER0:add_gold(500) return PLAYER0.MONEY", 0x320226794cb0ac96ULL}, // 4 bytes; ok: 10500
    {"lua:PLAYER0:set_texture('ANCIENT')", 0x607fe8e42a9780d3ULL}, // 433 bytes; ok:
    {"lua:return tostring(PLAYER0)", 0x83c8a41769a5d60dULL}, // 0 bytes; ok: "PLAYER0"
    {"lua:return PLAYER0 == PLAYER0", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return PLAYER0 == PLAYER1", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:return PLAYER0.NOT_A_FIELD", 0x663e3a9e1fe048c5ULL}, // 0 bytes; error: [string "return PLAYER0.NOT_A_FIELD"]:1: Unknown field or method 'NOT_A_FIELD' for Player
    {"lua:return Map.width, Map.height, Map.map_number, Map.map_name, Map.map_type, Map.campaign, Map.default_texture", 0x1aaf55dad08febfdULL}, // 0 bytes; ok: 85, 85, 11, "Hearth", "Campaign", "Dungeon Keeper original campaign", "NONE"
    {"lua:return Map.creature_pool", 0xa25801881e69f1fbULL}, // 0 bytes; ok: {BILE_DEMON=10,DARK_MISTRESS=20,DEMONSPAWN=20,DRAGON=15,HELL_HOUND=30,ORC=15,SORCEROR=30,TROLL=20}
    {"lua:Map.width = 10 return Map.width", 0x67e4e21b40a1f58dULL}, // 0 bytes; error: [string "Map.width = 10 return Map.width"]:1: MAP field 'width' is read-only
    {"lua:local s = GetSlab(13, 13) return s.kind, s.owner, s.revealed, s.style, s.room, s.centerpos", 0x7b9d3ec81ec06e22ULL}, // 0 bytes; ok: "WATER", Player#5, false, "NONE", nil, {val_x=10368,val_y=10368,val_z=0}
    {"lua:local s = GetSlab(13, 13) s.kind = 'LAVA' return s.kind", 0x68ca4c273f2e86b3ULL}, // 634 bytes; ok: "LAVA"
    {"lua:local s = GetSlab(13, 13) s.owner = PLAYER_GOOD return s.owner", 0x00f25d387d36c653ULL}, // 1 bytes; ok: Player#4
    {"lua:return GetSlab(13, 13):GET_CREATURES()", 0xc2f504a80d6ecc14ULL}, // 0 bytes; ok: {}
    {"lua:return GetSlab(500, 500)", 0x5c7137c99d4698e4ULL}, // 0 bytes; error: bad argument #1 to '?' (x slab coord out of range)
    {"lua:local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] return r.type, r.owner, r.slabs_count, r.health, r.max_health, r.efficiency", 0x800207bf4097edb3ULL}, // 0 bytes; ok: "ENTRANCE", Player#0, 9, 18, 18, 219
    {"lua:local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] return r.used_capacity, r.max_capacity, r.capacity_used_for_storage, r.centerpos, r.workers, r.slabs", 0x2c9a5e726bd2193eULL}, // 0 bytes; ok: 0, 9, 0, {val_x=32640,val_y=25728,val_z=0}, {}, {1={__class="Slab",slb_x=41,slb_y=32},2={__class="Slab",slb_x=42,slb
    {"lua:local r = GetRoomsOfPlayerAndType(PLAYER0, 'ANY_ROOM')[1] r.health = 10 return r.health", 0x21b3ca4eb4341975ULL}, // 1 bytes; ok: 10
    {"lua:local c = PLAYER0.camera return c.pos, c.yaw, c.pitch, c.roll, c.zoom, c.horizontal_fov, c.view_mode", 0x826ce16faf6be3a3ULL}, // 0 bytes; ok: {val_x=32611,val_y=21251,val_z=0}, 256, -266, 0, 8192, 94, 2
    {"lua:PLAYER0.camera.zoom = 2000 return PLAYER0.camera.zoom", 0x3474f10b88f46d97ULL}, // 2 bytes; ok: 2000
    {"lua:return C0():isValid()", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return tostring(C0())", 0xf5b72f85c1bf0886ULL}, // 0 bytes; ok: "Thing (id: 19 turn: 0 creature IMP)"
    {"lua:return C0() == C0()", 0xe7ddae4e62ee81a4ULL}, // 0 bytes; ok: true
    {"lua:return C0().NOT_A_FIELD", 0x68db543597519e46ULL}, // 0 bytes; error: [string "return C0().NOT_A_FIELD"]:1: Unknown field or method 'NOT_A_FIELD' for Creature thing
    {"lua:C0():walk_to(40, 40)", 0x2537684a1886d713ULL}, // 25 bytes; ok:
    {"lua:C0():kill()", 0x3b0c89169cac933eULL}, // 97 bytes; ok:
    {"lua:C0():stun(100)", 0x62ef45c32cedf9d4ULL}, // 3 bytes; ok:
    {"lua:C0():remove_from_play(100)", 0x2c6b0342be5488afULL}, // 8 bytes; ok:
    {"lua:C0():destroy()", 0x7f4f99c03d688b18ULL}, // 0 bytes; error: [string "C0():destroy()"]:1: calling 'destroy' on bad self (Expected an object)
    {"lua:C0():delete()", 0x3a31e6ff859d95aaULL}, // 109 bytes; ok:
    {"lua:C0():transform('TROLL', 2)", 0x836a439e9d96ba1dULL}, // 177 bytes; ok:
    {"lua:C0():transfer(PLAYER_GOOD)", 0x579ea3b5017d8ebcULL}, // 140 bytes; ok:
    {"lua:C0():level_up(2)", 0x7cfa7b99a3fa2b49ULL}, // 6 bytes; ok:
    {"lua:C0():teleport(1, 'EFFECT_EXPLOSION_1')", 0x31cd26b9e63f8c33ULL}, // 112 bytes; ok:
    {"lua:C0():change_owner(PLAYER_GOOD)", 0xe19910c80c347422ULL}, // 13 bytes; ok:
    {"lua:C0():set_velocity(10, 0, 0)", 0x9c99a2f3abc2f17cULL}, // 9 bytes; ok:
    {"lua:return C0():get_annoyance('NOT_PAID')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:C0():set_annoyance('HUNGRY', 100) return C0():get_annoyance('HUNGRY')", 0x7d8bcf55b7a206c6ULL}, // 0 bytes; ok: 0
    {"lua:return C0():in_enemy_custody()", 0x5cda8f81987f7453ULL}, // 0 bytes; ok: false
    {"lua:C0():make_thing_zombie()", 0x44979a4fd370cd5bULL}, // 1 bytes; ok:
    {"lua:C0():set_start_state('CREATURE_STATE_IDLE')", 0xbce626aa0a82bdbdULL}, // 2 bytes; ok:
    {"lua:return HERO()", 0xdcf298ae650faca0ULL}, // 0 bytes; ok:
    {"lua:return PLAYER0.heart.health, PLAYER0.heart.max_health, PLAYER0.heart.thing_class, PLAYER0.heart.model", 0x270e36f3287950feULL}, // 0 bytes; ok: 30000, 30000, "OBJECT", "SOUL_CONTAINER"
    {"lua:PLAYER0.heart.health = 100 return PLAYER0.heart.health", 0xf59ca984f20d7744ULL}, // 2 bytes; ok: 100
    {"lua:PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true) local t = GetThingsOnSubtile(100, 112, 'Trap')[1] return t, t.shots, t.revealed, t.rearm_turn, t.shooting_finished_turn", 0x557067dd4b84c8c1ULL}, // 41 bytes; ok: Thing#126, 1, 0, 30, 0
    {"lua:PlaceTrap(PLAYER0, 'BOULDER', 100, 112, true) local t = GetThingsOnSubtile(100, 112, 'Trap')[1] t.shots = 5 t.revealed = 1 t.rearm_turn = 10 return t.shots, t.revealed, t.rearm_turn", 0x38ad65db5f95842fULL}, // 42 bytes; ok: 5, 1, 10
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
    {"lua:local c = C0() c.pitch = 100 return c.pitch", 0xeffd894c76f7e82dULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.owner = PLAYER_GOOD return c.owner", 0x53e05c34942c9564ULL}, // 13 bytes; ok: Player#4
    {"lua:local c = C0() c.health = 50 return c.health", 0xc8a81dc80c6e9d79ULL}, // 1 bytes; ok: 50
    {"lua:local c = C0() c.health = 5000 return c.health", 0xc529d5bb7da3e08eULL}, // 2 bytes; ok: 5000
    {"lua:local c = C0() c.health = -1 return c.health", 0xd60c4de376e19d6eULL}, // 8 bytes; ok: -1
    {"lua:local c = C0() c.pos = {val_x = c.pos.val_x + 256, val_y = c.pos.val_y, val_z = c.pos.val_z} return c.pos", 0xd3868c08e0c2c3adULL}, // 5 bytes; ok: {val_x=32640,val_y=33664,val_z=768}
    {"lua:local c = C0() c.anim_sprite = 1 return c.anim_sprite", 0xfc3391854756be98ULL}, // 2 bytes; ok: 1
    {"lua:local c = C0() c.anim_speed = 64 return c.anim_speed", 0x83a610c01ef62545ULL}, // 1 bytes; ok: 64
    {"lua:local c = C0() c.sprite_size = 200 return c.sprite_size", 0xe293d8720a74840fULL}, // 2 bytes; ok: 200
    {"lua:local c = C0() c.sprite_size_min = 100 return c.sprite_size_min", 0x6c4ec8977a4c5829ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.sprite_size_max = 300 return c.sprite_size_max", 0x2d594e45a59045d2ULL}, // 2 bytes; ok: 300
    {"lua:local c = C0() c.transformation_speed = 10 return c.transformation_speed", 0x39fecc0c2ce27872ULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.clipbox_size_xy = 100 return c.clipbox_size_xy", 0x7b425786aa610c05ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.clipbox_size_z = 100 return c.clipbox_size_z", 0x499ab5dff029121cULL}, // 2 bytes; ok: 100
    {"lua:local c = C0() c.solid_size_xy = 100 return c.solid_size_xy", 0x3543974c956b9015ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.solid_size_z = 100 return c.solid_size_z", 0x79ee31f319da5a5cULL}, // 2 bytes; ok: 100
    {"lua:local c = C0() c.name = 'Golden' return c.name", 0xf26325ca44072624ULL}, // 6 bytes; ok: "Golden"
    {"lua:local c = C0() c.gold_held = 500 return c.gold_held", 0x3de2594d7b26dda0ULL}, // 2 bytes; ok: 500
    {"lua:local c = C0() c.exp_points = 1000 return c.exp_points", 0x0ffc470792538be2ULL}, // 2 bytes; ok: 1000
    {"lua:local c = C0() c.moveto_pos = c.pos return c.moveto_pos", 0x4bcc1059d25c96abULL}, // 0 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:local c = C0() c.flee_pos = c.pos return c.flee_pos", 0xa0fe8f44c9b86c82ULL}, // 3 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:local c = C0() c.max_speed = 100 return c.max_speed", 0x53d4b0f72412108bULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.patrol_pos = c.pos return c.patrol_pos", 0x6c2c2d5565621261ULL}, // 5 bytes; ok: {val_x=32384,val_y=33664,val_z=768}
    {"lua:local c = C0() c.patrol_countdown = 10 return c.patrol_countdown", 0x91cc1fb030688372ULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.party_objective = 'ATTACK_DUNGEON_HEART' return c.party_objective", 0x1d3dd3f0c1a0da67ULL}, // 1 bytes; ok: "ATTACK_DUNGEON_HEART"
    {"lua:local c = C0() c.party_original_objective = 'ATTACK_DUNGEON_HEART' return c.party_original_objective", 0xc8f4d27cd9a67b94ULL}, // 1 bytes; ok: "ATTACK_DUNGEON_HEART"
    {"lua:local c = C0() c.party_target_player = PLAYER0 return c.party_target_player", 0x3f99cc654f7e5e1eULL}, // 0 bytes; ok: Player#0
    {"lua:local c = C0() c.countdown = 100 return c.countdown", 0x442a7ef51bc52aacULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.state = 1 return c.state", 0xd72fd687f6770203ULL}, // 2 bytes; ok: "ImpDoingNothing"
    {"lua:local c = C0() c.continue_state = 1 return c.continue_state", 0xf69b13b5b17315a8ULL}, // 0 bytes; ok: "ImpDoingNothing"
    {"lua:local c = C0() c.instance = 1 return c.instance", 0x55900a1e04a29fdfULL}, // 5 bytes; ok: "SWING_WEAPON_SWORD"
    {"lua:local c = C0() c.hunger_amount = 5 return c.hunger_amount", 0x60fff584292cfd8dULL}, // 1 bytes; ok: 5
    {"lua:local c = C0() c.hunger_level = 5 return c.hunger_level", 0x118966a931fedd3dULL}, // 1 bytes; ok: 5
    {"lua:local c = C0() c.creature_kills = 3 return c.creature_kills", 0x69d570141b258d85ULL}, // 1 bytes; ok: 3
    {"lua:local c = C0() c.creature_kills_allies = 1 return c.creature_kills_allies", 0x6f0c004a157bc20bULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.creature_kills_enemies = 2 return c.creature_kills_enemies", 0x483c8b4d074e5facULL}, // 1 bytes; ok: 2
    {"lua:local c = C0() c.hunger_loss = 1 return c.hunger_loss", 0x3cb8d75d252bb9deULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.hand_blocked_turns = 10 return c.hand_blocked_turns", 0x9bb8828e2c75c3f6ULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.force_health_flower_displayed = true return c.force_health_flower_displayed", 0xe7844181659343e3ULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.force_health_flower_hidden = true return c.force_health_flower_hidden", 0x09ff003916365b10ULL}, // 1 bytes; ok: 1
    {"lua:local c = C0() c.conscious_back_turns = 10 return c.conscious_back_turns", 0x84f5d26ff4401cbaULL}, // 1 bytes; ok: 10
    {"lua:local c = C0() c.unsummon_duration = 100 return c.unsummon_duration", 0x45bf12dc9eeffa00ULL}, // 1 bytes; ok: 100
    {"lua:local c = C0() c.shots = 3 return c.shots", 0x153051fa350edf4fULL}, // 0 bytes; error: [string "local c = C0() c.shots = 3 return c.shots"]:1: Field 'shots' is not writable on Creature thing
    {"lua:local c = C0() c.revealed = true return c.revealed", 0x209a3d8421e522d0ULL}, // 0 bytes; error: [string "local c = C0() c.revealed = true return c.rev..."]:1: Field 'revealed' is not writable on Creature thing
    {"lua:local c = C0() c.rearm_turn = 10 return c.rearm_turn", 0x97bae7c098ff1f02ULL}, // 0 bytes; error: [string "local c = C0() c.rearm_turn = 10 return c.rea..."]:1: Field 'rearm_turn' is not writable on Creature thi
    {"lua:local c = C0() c.shooting_finished_turn = 10 return c.shooting_finished_turn", 0xcbdccd97ae54509fULL}, // 0 bytes; error: [string "local c = C0() c.shooting_finished_turn = 10 ..."]:1: Field 'shooting_finished_turn' is not writable on
    {"lua:local c = C0() c.box_kind = 1 return c.box_kind", 0xd66348cb19f98e02ULL}, // 0 bytes; error: [string "local c = C0() c.box_kind = 1 return c.box_kind"]:1: Field 'box_kind' is not writable on Creature thing
    {"lua:local c = C0() c.target = c return c.target", 0xc2ee4407acb10dc5ULL}, // 0 bytes; error: [string "local c = C0() c.target = c return c.target"]:1: Field 'target' is not writable on Creature thing
    {"lua:local c = C0() c.damage = 5 return c.damage", 0xe712492e03997b6fULL}, // 0 bytes; error: [string "local c = C0() c.damage = 5 return c.damage"]:1: Field 'damage' is not writable on Creature thing
