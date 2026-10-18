# Agent API reference (External seats)

This page is the wire reference for the engine's local agent API: the TCP JSON protocol an External seat is played over.
[architecture.md §5a](architecture.md#5a-external-seats-agents-playing-a-keeper) explains how the pieces fit together, and
[scripts/llm_bridge/README.md](../../scripts/llm_bridge/README.md) covers the MCP bridge built on top of this protocol.
The protocol is implemented in `src/kfx_script/src/api*.c`: the dispatcher is `api.c`, the view is `api_seat_view.c`, the
decisions are `api_seat_decision.c`, the diffs are `api_seat_diff.c` and the combat assistant is `api_seat_assist.c`.
Whenever this page and the code disagree, the code is right; please fix this page.

## Connection

- **Turning it on.** The server is enabled by `API_ENABLED=TRUE` in `daimonkeeper.cfg`. It listens on `API_PORT` (default 5599),
  on 127.0.0.1 only.
- **The game loop drives it.** The loop polls the server every frame. First it runs the seat machinery (the seats'
  queues, the decision ticker and the combat statistics), whether or not a client is connected. Then it accepts a
  connection, reads, and answers every complete message.
- **One client at a time.** While a client is connected, a second connection is accepted and closed at once, with no
  reply.
- **Token.** If the environment variable `DAIMON_API_TOKEN` is set when the game starts, the first message must be
  `{"action":"hello","token":"…"}`. A wrong token, or any other action first, gets `UNAUTHORIZED` and the client is
  dropped. The comparison takes constant time. If the token is longer than 127 characters, the server does not start.
  Without a token, `hello` is still accepted and just replies ok.
- **Not an HTTP port.** If the first non-blank byte a client sends is not `{`, the client is dropped silently. This turns
  away browsers' requests.
- **Disconnecting releases the game.** It ends an urgent hold, clears every subscription, resumes a game the agent had
  paused, releases the creatures held by `move_creature` (reason `agent_lost`), and, if `set_takeover` is armed, hands
  every External seat to the built-in AI.

### Framing

- **Input.** A message is one JSON object. No newline or length prefix is needed: the reader matches braces (ignoring
  braces inside strings) and skips whatever lies between objects. One read may carry several messages, and a message may
  arrive across several reads.
- **Size limits.**
  - Input past 1 MiB gets `MESSAGE_TOO_LARGE`, and the buffer is discarded.
  - A reply of 1 MiB or more becomes `RESPONSE_TOO_LARGE`.
  - Oversized events are dropped.
- **Output.** Every message is minimised JSON followed by `\n`:

  | kind | shape |
  |---|---|
  | ok | `{"ack":…,"success":true}` |
  | data | `{"ack":…,"success":true,"data":…}` |
  | error | `{"ack":…,"success":false,"error":"CODE"}`, plus `"rule":n` for a `set_assist` rule error |
  | event | `{"event":"NAME","data":{…}}` (no `data` when the event has no fields) |
  | variable | `{"event":"VAR_UPDATE","var":{"player":"PLAYER0","name":…,"value":…}}` |

- **`ack`.** Put any JSON value in `ack` and the reply echoes it back. Integers come back as int64, and object keys are
  sorted. Errors raised before the action is read (`NO_JSON`, `INVALID_JSON`, `INVALID_JSON_OBJECT`) cannot echo it;
  `MISSING_ACTION` does.
- **A client that stops reading is dropped.** If a send makes no progress for 2 s, the server stops writing and drops the
  client once the current messages are done. A client must keep reading, events included, while it thinks.

### Who an action is for

Action names are case-insensitive. Each action needs one of three things:

- **Nothing:** works at any time.
- **LocalGame:** needs a local game in progress. Otherwise the reply is `NOT_IN_LOCAL_GAME`, which is also the reply to an
  unknown action outside a local game.
- **Seat:** needs a local game and a `player` (a number, or a name such as `"PLAYER1"`) that is an External seat. The
  errors are:
  - `MISSING_PLAYER` when there is no `player`;
  - `INVALID_PLAYER` when the player is out of range or does not exist;
  - `NOT_A_VALID_SEAT` when it is not an External seat.

  A Seat action also counts as activity for the pause watchdog. Any Seat action may carry `watchdog_ms`: the default is
  60000, and 0 or less turns the watchdog off. The watchdog acts only in a game a human plays: when the agent goes quiet
  it resumes the game, and hands the seats to the built-in AI if takeover is armed.

Seat actions work while the game is paused. `map_command` and `console_command` refuse a paused game.

## Actions

The errors under each action leave out the common ones above.

### Session and seats

| action | needs | request | reply / errors |
|---|---|---|---|
| `hello` | — | `token` | ok; `UNAUTHORIZED` |
| `get_kfx_info` | Nothing | | `{kfx_version, product, product_version}`. `kfx_version` is the KeeperFX release whose content this plays |
| `get_seats` | LocalGame | | `{seats:[{player, user}]}` |
| `claim_seat` | LocalGame | `player` | `{player, user}`. Claiming a seat that is already claimed returns it unchanged. `CANNOT_CLAIM_SEAT` in a network game, for another human's keeper, or when no External user slot is free. The local human's own keeper can be claimed |
| `release_seat` | LocalGame | `player` | ok. The keeper goes back to its earlier AI (or to the local human), held creatures are released (`seat_released`) and its name is cleared |
| `set_takeover` | LocalGame | `enabled` | ok. While armed, the built-in AI takes every External seat when the client is lost or the watchdog fires. It fires once, and a game start or load disarms it; `MISSING_ENABLED` |
| `set_player_name` | Seat | `name` (≤19 bytes, printable) | ok; `MISSING_NAME`, `NAME_TOO_LONG`, `BAD_NAME` |
| `set_agent_memory` | Seat | `data` (string ≤64 KiB; `""` clears it) | ok. Saved with the game; `MISSING_DATA`, `MEMORY_TOO_LARGE`, `MEMORY_NOT_STORED` |
| `get_agent_memory` | Seat | | `{data: string \| null}` |

### Reading the game

| action | needs | request | reply / errors |
|---|---|---|---|
| `get_player_view` | Seat | `since?` | the view; see [get_player_view](#get_player_view) |
| `find_drop_spot` | Seat | `pos`, `radius?` (default 15, 0-45), `thing_id?` | `{pos, distance}`: the nearest ordinary drop spot, searched ring by ring and measured as Chebyshev distance. A drop spot is never a pool, portal, prison or torture chamber. `MISSING_POSITION`, `NO_CREATURE`, `NO_DROP_SPOT` |
| `get_combat_stats` | LocalGame | `player` | see [get_combat_stats](#get_combat_stats); `NOT_TRACKED` |
| `get_assist_presets` | LocalGame | | `{presets:{name:{…, error?, rule?}}}` from `fxdata/combat_assist_presets.toml`; `error` marks a preset the game would refuse |
| `get_level_info` (also `get_map_info`) | LocalGame | | `{level_name, level_number, players, mapsize_x, mapsize_y, is_multiplayer, campaign{campaign_name, campaign_display_name, campaign_fname, is_map_pack}}` |
| `get_current_game_info` | LocalGame | | `{game_turn}` |
| `get_log_tail` | LocalGame | `lines?` (default 100, 1-500) | `{lines[], log_level}`. Reads at most the last 256 KiB; `LOG_UNAVAILABLE` |

### Time

| action | needs | request | reply / errors |
|---|---|---|---|
| `set_pause` | Seat | `paused`, `force?` | ok. The pause is the agent's. Pausing gives `HUMAN_PLAYING` in a game a human plays, unless its setup allowed it or `force` is given; unpausing is always allowed. `MISSING_PAUSED` |
| `advance_turns` | Seat | `turns` (1-100000), `force?` | ok at once. The game runs that many turns, then pauses again; `MISSING_TURNS`, `BAD_TURNS`, `HUMAN_PLAYING` |
| `set_game_speed` | LocalGame | `turns_per_second` (1-100; 0 = the start-up rate) | `{turns_per_second}`; `MISSING_TURNS_PER_SECOND`, `BAD_TURNS_PER_SECOND` |
| `set_decision_policy` | LocalGame | `min_interval_turns?`, `on_urgent?`, `hold_seconds?`, `slow_turns_per_second?` | ok; see [Urgent holds](#urgent-holds). `MISSING_MIN_INTERVAL` (neither field given), `BAD_ON_URGENT`, `HUMAN_PLAYING` |
| `ack_decision` | Seat | `seq?` (absent = all) | ok. Answers an urgent hold without an order |

`set_pause`, `advance_turns` and `set_game_speed` end an urgent hold without restoring anything: the agent has taken
time into its own hands.

### Orders: `submit_action`

`{"action":"submit_action","player":…,"verb":…, …}` is one keeper gesture. It goes into the seat's queue, which writes
one packet per turn (see [architecture §5a.2](architecture.md#5a2-one-turn-for-a-seat)). There are 21 verbs, and each
uses the fields it needs.

| verb | fields | notes |
|---|---|---|
| `mark_dig`, `unmark_dig` | `slab_rect` | each up to 256 slabs. `mark_dig` warns `UNREACHABLE` when imps cannot reach the marks |
| `build_room` | `kind`, `slab_rect` | up to 256 slabs. Warns `PARTLY_UNBUILDABLE`. A bridge must be a line |
| `sell` | `slab_rect` or `pos`, `lose_spells?` | up to 64 slabs |
| `place_trap`, `place_door` | `kind`, `pos` | |
| `set_door_lock` | `pos`, `enabled` | |
| `cast_power` | `power`, `pos` or `thing_id`, `overcharge_turns?` (0-32; level = turns/4) | |
| `power_off` | `power` | CALL_TO_ARMS, SIGHT and OBEY only |
| `slap` | `thing_id`, `direction?` | the direction aims a slapped trap |
| `pick_up` | `thing_id` | |
| `drop` | `pos` or `to_room`, intent flags | |
| `pick_up_and_drop` | `thing_ids` (≤16), `pos` or `to_room`, intent flags | one hand trip. If only some of the things are carried, the result is `PARTIAL: n of m` |
| `move_creature`, `attack` | `thing_id`, `pos`, `hold_turns?` | holds the creature at the spot. It is released automatically on pay day, when hungry, when attacked, or after `hold_turns` (default pay_day_gap/4, at least 200) |
| `release_creature` | `thing_id` | |
| `use_special` | `thing_id`, `kind`/`level` (resurrect), `target_thing` (transfer) | |
| `set_tendency` | `kind` (`imprison`/`flee`), `enabled` | |
| `set_alliance` | `ally_player`, `enabled` | |
| `send_message` | `message` (1-63 printable characters) | one every 100 turns |
| `cancel` | `id`, or `lane`, or neither | neither cancels everything, including the running gesture |

Coordinates: `pos` is `[x,y]` in subtiles, and `slab_rect` is `[x0,y0,x1,y1]` in slabs, inclusive, with its corners in
any order. The intent flags `release`, `sacrifice`, `imprison`, `torture` and `dismiss` confirm a drop that would do that
thing. Without the flag, the drop is refused with `WOULD_SACRIFICE`, `WOULD_IMPRISON`, … or `NOT_A_POOL`, `NOT_A_PRISON`,
…, whichever applies.

**Queueing and timing fields** (any verb):

| field | meaning |
|---|---|
| `priority` | `"now"` (or `true`) puts the verb in the urgent lane. It runs before the routine lane, and it cuts a running cuttable verb (`build_room`, `mark_dig`, `unmark_dig`, `sell`, `set_assist`), which resumes afterwards. `"routine"` is the default; any other value gives `BAD_PRIORITY` |
| `queue` | when the seat is busy, wait behind the running gesture instead of failing with `ACTION_ALREADY_QUEUED`. A `now` verb always queues. Each lane holds 16 verbs (`QUEUE_FULL`) |
| `view_turn`, `max_age_turns` | the turn of the view the order was based on. The reply carries `age_turns`. A verb that has not started by `view_turn + max_age_turns` fails: `STALE_VIEW` at submit, `EXPIRED` later |
| `decision_seq` | the `DECISION_DUE` `seq` this order answers (see [Urgent holds](#urgent-holds)). Absent answers everything; 0 marks an order that answers nothing, such as a trigger's |
| `dry_run` | validate only. The reply is `{would_succeed:true, steps, warnings…}`, and the busy state is ignored. `assume_dig_rects` (up to 16 rectangles) counts planned digs for the reachability warning |

**Replies.** A real submit replies `{steps, id, queued_behind, age_turns?}`, plus a verb's warning
(`warning`, `unreachable_count`/`unreachable_slabs` or `unbuildable_count`/`unbuildable_slabs`, listing at most 32
slabs). A `cancel` replies with `steps` set to the number of verbs it dropped.

**Errors at submit:** `MISSING_VERB`, `UNKNOWN_VERB`, `STALE_VIEW`, `ACTION_ALREADY_QUEUED`, `QUEUE_FULL`, then the verb's
own check.

| check | errors |
|---|---|
| position | `MISSING_POSITION`, `POSITION_OFF_MAP` |
| things | `MISSING_THING`, `NO_SUCH_THING`, `NOT_YOURS`, `CANNOT_PICK_UP`, `TOO_MANY`, `DUPLICATE_THING`, `HAND_NOT_EMPTY`, `HAND_EMPTY`, `BAD_THING_IDS` |
| kind | `UNKNOWN_KIND`, `NOT_AVAILABLE`, `UNSUPPORTED_POWER`, `BAD_OVERCHARGE` |
| place | `CANNOT_PLACE_HERE`, `CANNOT_BUILD_HERE`, `CANNOT_CAST_HERE`, `CANNOT_DROP_HERE`, `CANNOT_REACH`, `ROOM_FULL`, `NO_ROOM`, `NOT_YOUR_ROOM`, `NO_POOL` |
| area | `MISSING_RECT`, `AREA_TOO_LARGE`, `BRIDGE_NOT_A_LINE`, `NOTHING_MARKED`, `NOTHING_TO_SELL`, `WOULD_LOSE_SPELLS` |
| creatures | `NOT_ORDERABLE`, `NOT_HELD`, `BAD_HOLD`, `HUNGRY`, `OWED_PAY`, `PAYDAY_TOO_CLOSE`, `ALREADY_THERE`, `NOTHING_TO_ATTACK`, `NOT_SLAPPABLE`, `SLAP_NOT_READY`, `MISSING_DIRECTION`, `BAD_DIRECTION` |
| settings | `MISSING_ENABLED`, `ALREADY_SET`, `NO_PRISON`, `NO_DOOR`, `NOT_ACTIVE`, `MISSING_TARGET_PLAYER`, `ALLIANCE_LOCKED` |
| specials | `MISSING_CHOICE`, `BAD_LEVEL`, `NO_SUCH_DEAD` |
| messages | `BAD_MESSAGE`, `TOO_SOON` |
| cancel | `BAD_LANE`, `NO_SUCH_ORDER` |

A queued verb is checked again when it starts. A failure then shows only in `seat.results[].error`, which can also carry
`EXPIRED`, `CANCELLED`, `BUILD_BUSY` (waited 600 turns for the previous room drag), `HAND_BUSY` (waited 30 turns for
the hand), `PICK_UP_FAILED`, `HAND_CHANGED` and `PARTIAL: n of m`.

### Combat assistant: `set_assist`, `use_assist_bank`

`set_assist` sets the seat's assistant rules. The rules run inside the game, in the simulation (cast rules) or the seat
(hand rules), so they act within a turn of a fight starting, without waiting for the agent. A change travels as packets
through the seat's routine lane. The request is
`{"action":"set_assist","player":…, <globals>, "rules"?:[…], "bank"?, "dry_run"?, "queue"?, "decision_seq"?}`:

- **Globals.** Each is optional; a field left out keeps its current value.

  | field | range |
  |---|---|
  | `enabled` | 0/1 |
  | `interval` (turns between evaluations) | 1-20 |
  | `charge_time` | 0/1 |
  | `min_gap` | 1-200 |
  | `reserve` (gold never spent) | 0-1e9 |
  | `max_spend` (per pay day) | 0-1e9 |

- **`rules`.** Up to 16 rules. Leaving it out keeps the current rules; `[]` clears them.
  - **A cast rule** is `{power | power_off, target, level 0-8, pick?, cooldown, target_cooldown, budget?, max_allies_hit?,
    when}`.
    - `target` is one of `ally_in_fight`, `ally_any` or `enemy_in_fight`. `pick` is one of `lowest_health`, `strongest`,
      `highest_level` or `most_valuable`.
    - Allied powers are HEALCRTR, SPEEDCRTR, PROTECT and CONCEAL. Enemy powers are CHICKEN, DISEASE, LIGHTNING and
      CAVEIN. `power_off` takes CALL2ARMS or OBEY (the gold guard).
  - **A hand rule** is `{hand, to?, count 1-8, radius 1-15, min_match 0-200, cooldown, when}`. Hand rules are for agents
    only: a human's assistant config drops them, and the game refuses them in multiplayer. Each firing is an urgent
    `pick_up_and_drop`, which the seat carries out with its hand. `to` is in subtiles.
    - `evacuate_diggers` lifts the diggers within `radius` slabs of an enemy fighter to `to`. Make `to` a sealed pocket:
      imps run back to defend an attacked heart.
    - `lift_wounded` lifts fighters below the rule's health out of a fight, to `to` or else to a lair. It does this only
      when no heal rule could heal them now.
    - `reinforce` sends up to `count` healthy fighters to a fight that is being lost or only matched. Each must have at
      least `min_match`% of the strongest enemy's force there, and they are sent only if together they would turn the
      fight.
  - **`when`** holds up to 4 `[fact, op, value]` entries. The ops are `< <= > >= == !=`.
    - Facts about the creature are `health_pct`, `health`, `level`, `force`, `is_digger`, `on_own_ground`, `fleeing` and
      `kind` (`==`/`!=`, with a creature name).
    - Facts about the battle are `battle_ally_force`, `battle_enemy_force`, `force_ratio`, `battle_size` and
      `allies_near`.
    - Facts about the keeper are `gold`, `gold_after_wages`, `payday_turns`, `fights`, `fighters`, `cta_active`,
      `cta_paying`, `obey_active` and `heart_health_pct`.
- **`bank`.** 0 (the default) is the live set. A bank of 1-3 stores the set for later, and `use_assist_bank` switches to it
  in one packet.
- **`queue`** defaults to true here, unlike `submit_action`.

The reply is `{steps, rules_changed}` plus `unchanged:true` (nothing was sent), `would_succeed:true` (a dry run), or
`{id, queued_behind}` (a real submit). A rule error carries the index of the bad rule in `rule`:

| errors | when |
|---|---|
| `BAD_VALUE`, `VALUE_OUT_OF_RANGE` | a field's value |
| `BAD_RULES`, `TOO_MANY_RULES`, `BAD_RULE` | the rule list or a rule |
| `UNKNOWN_HAND`, `UNKNOWN_ACTION`, `MISSING_DESTINATION`, `BAD_POSITION`, `POSITION_OFF_MAP` | a hand rule's action or destination |
| `MISSING_POWER`, `UNKNOWN_POWER`, `POWER_NOT_SUPPORTED`, `BAD_TARGET_FOR_POWER`, `UNKNOWN_TARGET`, `UNKNOWN_PICK`, `BAD_LEVEL` | a cast rule's power and target |
| `BAD_CONDITION`, `TOO_MANY_CONDITIONS`, `UNKNOWN_FACT`, `BAD_OP`, `UNKNOWN_KIND`, `FACT_NEEDS_TARGET` | a `when` condition |

The game can also refuse the submit itself, with `ACTION_ALREADY_QUEUED`, `QUEUE_FULL`, `NOT_IN_MULTIPLAYER`, `BAD_BANK`
or `TOO_MANY_STEPS`.

`use_assist_bank` takes `{bank 1-3, priority?, decision_seq?}` and switches the live set to that bank. It uses the urgent
lane unless `priority:"routine"`, and replies `{id, steps, queued_behind}`. Errors: `MISSING_BANK`, `BAD_BANK`,
`EMPTY_BANK`, `QUEUE_FULL` and `NOT_IN_MULTIPLAYER`.

### Scripting and events

| action | needs | request | reply / errors |
|---|---|---|---|
| `subscribe_event`, `unsubscribe_event` | Nothing | `event` (exact name, case-sensitive, <64 characters) | ok. Any name is accepted. `MISSING_EVENT`, `STRING_TOO_LONG`, `SUB_FAILED` (256 subscriptions in all, events and variables together) |
| `subscribe_var`, `unsubscribe_var` | Nothing | `var`, `player?` | ok. A `VAR_UPDATE` is pushed whenever the variable changes. `MISSING_VAR`, `UNKNOWN_VAR`, `SUB_FAILED` |
| `unsubscribe_all` | Nothing | | ok |
| `read_var` | LocalGame | `var`, `player?` | the value as bare `data` (int64). `MISSING_VAR`, `UNKNOWN_VAR` |
| `set_var` | LocalGame | `var`, `value` (int), `player?` | ok; `UNABLE_TO_SET_VAR`, `VALUE_MUST_BE_INT` |
| `get_all_player_flags` | LocalGame | | `{PLAYER: {FLAG: n}}` |
| `map_command` | LocalGame | `command` (a level-script line) | ok; `GAME_IS_PAUSED`, `MISSING_COMMAND`, `FAILED_TO_EXECUTE_MAP_COMMAND` |
| `console_command` | LocalGame | `command`, `player?` | ok. The command runs as that player; `lua` is refused (`COMMAND_NOT_ALLOWED`). `GAME_IS_PAUSED`, `MISSING_COMMAND`, `FAILED_TO_EXECUTE_CONSOLE_COMMAND` |

For `read_var`, `set_var`, the `*_var` actions and `console_command`, `player` defaults to the local player.

## Events

An event is pushed only to a connected client that has subscribed to its exact name.

| event | data |
|---|---|
| `DECISION_DUE` | see below |
| `GAME_STARTED`, `GAME_ENDED`, `CAMPAIGN_LOADED` | none |
| `GAME_SAVED` | `turn`, `slot` |
| `GAME_LOADED` | `turn` |
| `WIN_GAME`, `LOSE_GAME` | none (the local player's result only; a seat's own result is `seat.victory_state`) |
| `ACTION_POINT` | `player`, `action_point` |
| `SPECIAL_ACTIVATED` | `player`, `special`, `special_name`, `special_value`, `thing`, `model`, `x`, `y`, `z` (subtiles), `level_number`, `game_turn` |
| `MANUFACTURE_COMPLETED` | `player`, `class`, `class_description`, `kind`, `kind_description`, `level_number` |
| `RESEARCH_COMPLETED` | `player`, `category`, `kind`, `kind_description`, `level_number` |

## Decisions

The game tells each seat when something needs its attention. It does this whether or not anybody subscribed:
`seat.decision` in the view always holds the latest decision, so a client can poll instead of subscribing.

`DECISION_DUE` carries:

| field | meaning |
|---|---|
| `player` | the seat |
| `seq` | the decision's number, counting up per seat |
| `turn` | the turn it was sent |
| `reasons` | the reasons, comma-joined (at most 8) |
| `raised` | the turn each reason was first raised, comma-joined in the same order |
| `severity` | that of its most severe reason |
| `where_x`, `where_y` | the subtile of the most severe reason that has a place, or -1 |
| `quarter` | the quarter of the pay-day cycle, 0-3 |
| `victory_state` | `won` / `lost` / `undecided` |

`seat.decision` in the view holds the same decision in more detail: `seq`, `turn`, `reasons[]`, `raised[]`, `severity`,
`where`, `losses[]` (what was lost or captured, and where), the reasons pending for the next decision, the `hold` and
`on_urgent`.

### Reasons and severity

| severity | reasons |
|---|---|
| 0 critical | `heart_attacked`, `defeat` |
| 1 combat | `enemy_fight`, `room_under_attack`, `breach`, `intruder`, `alarm`, `room_captured`, `room_destroyed`, `ground_lost`, `room_lost` |
| 2 problem | `creature_hungry`, `treasure_room_full`, `need_treasure_room`, `creature_annoyed`, `no_more_living_set`, `work_room_unreachable`, `storage_room_unreachable`, `enemy_door`, `creature_scavenged`, `assist_budget`, `assist_reserve`, `assist_gold_guard`, `sight_open`, `prisoner_hungry`, `torture_victim_hurt`, `chat` |
| 3 routine | `secret_door_spotted`, `new_room_research`, `new_spell_research`, `new_trap`, `new_door`, `information`, `special_found`, `payday`, `quarter_1`-`quarter_3`, `objective`, `new_prisoner`, `prisoner_interrogated`, `prisoner_converted`, `prisoner_gone`, `sacrifice_result`, `vampire_raised`, `wall_cue`, `victory`, `new_kind_<CREATURE>` |

What some of these mean:

- `intruder`: an enemy is on the seat's ground that has not been seen there in the last 1200 turns.
- `ground_lost`: covers ground taken by another keeper. Its loss record says `ground_captured`.
- `sight_open`: Sight of Evil has stopped widening for 8 turns. It is raised once per cast.
- `assist_gold_guard`: the assistant switched Call to Arms or Obey off to save gold.
- `wall_cue`: one of the seat's walls has open ground behind it.

### When a decision is sent

- **The usual rule.** A decision goes out when reasons are pending and either it is urgent or `min_interval_turns`
  (default 100) has passed since the last one.
- **Urgent at once:** `alarm`, `victory`, `defeat`, `sight_open`.
- **Fights and losses:** `heart_attacked`, `enemy_fight`, `room_under_attack`, `breach`, `intruder`, `room_captured`,
  `room_destroyed` and `ground_lost` are urgent once 60 turns have passed since the last such decision. A reason that
  wasn't in that decision is urgent at once.
- **`room_lost`** is combat severity but waits for the interval.
- **Baselines.** A seat's first tick, and every load, takes a baseline and raises nothing.

### Urgent holds

`set_decision_policy` can give the agent time to think about a decision of severity 0 or 1:

| field | default | meaning |
|---|---|---|
| `on_urgent` | `none` | `pause` holds the game paused; `slow` runs it at `slow_turns_per_second` |
| `slow_turns_per_second` | 4 | 1-100 |
| `hold_seconds` | 60 | 1-600: the wall-clock cap |

Giving `on_urgent` resets the other two fields to whatever this message gives, or to their defaults. A hold is refused
with `HUMAN_PLAYING` unless the agent may pause the game: no human plays a keeper, or the game's setup allowed it (the
Skirmish page's LLM Pause). A hold does not start on a game the agent has already paused, or one already slower than the
slow rate.

The hold ends in one of these ways:

- **The seat answers.** An answer is an accepted `submit_action`, `set_assist` or `use_assist_bank` whose `decision_seq`
  is the held decision's or later (or is absent), or an `ack_decision`. The game then resumes at its old speed, or
  finishes an `advance_turns` the hold interrupted.
- **Something releases it.** These also restore the game: the `hold_seconds` cap passing, and the client disconnecting.
- **Something cancels it.** These leave the pause and speed as they are: `set_pause`, `advance_turns`, `set_game_speed`
  and `on_urgent:"none"`.
- **A load forgets it.**

While a hold lasts, the view shows `seat.decision.hold{mode, ms_left}`.


## get_player_view

`{"action":"get_player_view","player":<number or name>,"since"?:<view_id>}`

The seat named by `player` must be an External seat. Otherwise the call fails with `MISSING_PLAYER`, `INVALID_PLAYER` or
`NOT_A_VALID_SEAT`. The call works while the game is paused. The reply is `{"ack":…,"success":true,"data":<view>}`.

While Sight of Evil is cast, the area it has explored is uncovered for as long as the view is being built. The map,
`visible` and `POWER_SIGHT` therefore show what the sight shows. If the seat's dungeon is invalid, `own` is `{}`, and
`chat`, `map` and `visible` are absent.

### Top level

| key | meaning |
|---|---|
| `turn` | the current game turn |
| `paused`, `agent_pause` | the game is paused; the pause is the agent's (set_pause, advance_turns or an urgent hold) |
| `advancing` | an `advance_turns` is running |
| `agent_only` | no human plays a keeper |
| `can_pause` | `set_pause` and `advance_turns` are accepted (agent-only games, or a setup that allowed it) |
| `turns_per_second` | the game's pace |
| `seat` | queue, results and decisions; see [seat](#seat) |
| `level` | `number`, plus `campaign`, `campaign_name`, `name` and `description` when known |
| `players[]` | every player except neutral: `{id, player, kind:"human"\|"heroes"\|"external"\|"computer", name?, ai_type?, alive?}`. `alive` means the player has a heart; it is omitted for heroes |
| `rules` | `custody` (`prison_skeleton_chance`, `torture_convert_chance`, `torture_death_chance`, `ghost_convert_chance`, `turns_per_torture_health_loss`, `max_things_in_hand`), plus `sacrifices[]` recipe strings (`"<ACTION> <param> <- VICTIM …"`) once the seat owns a temple |
| `own` | the seat's own dungeon, unfiltered; see [own](#own) |
| `chat[]` | the last 16 chat messages from anyone, as `{player, text, turn}` |
| `map` | the slab map, fog-filtered; see [map](#map) |
| `visible` | other players' things, fog-filtered; see [visible](#visible) |
| `view_id` | this view's id, to pass as `since` next time (one counter across all seats) |
| `mode` | `"full"` or `"diff"` |
| `reason` | full views only: why a requested diff was refused (`NO_BASE`, `BASE_MISMATCH`, `TURN_WENT_BACKWARDS`) |
| `base` | diffs only: the `since` value the diff is relative to |

### seat

- `player`, `user` (the net user id), `victory_state` (`won`/`lost`/`undecided`: the seat's own fate).
- `queued_steps`: the steps of the running gesture that are still to be written. `queued_verbs`: the verbs waiting
  behind it.
- `results[]` (the last 16): `{id, status:"done"|"rejected", error?, turn, submitted, started, lane?, suspended?}`.
  - `submitted` is the turn the verb was accepted; `started` is the turn it began, -1 if it never did.
  - `lane` is present only for `"now"`; when it is absent, the verb ran in the routine lane.
  - `suspended` counts how many times an urgent verb cut this one.
  - Errors include `EXPIRED`, `NOT_AVAILABLE`, `CANCELLED`, `PARTIAL: n of m` and the per-verb refusals.
- `queue`: `running?` `{id, lane, steps_left}`, then `now[]` and `routine[]` as `{id, suspended?:true}` in the order
  they will run.
- `decision`: see [Decisions](#decisions).
- `ordered_creatures`: the creatures that `move_creature` still holds. `auto_released[]` (the last 8): `{id, reason,
  turn}`. `reason` is one of `max_hold`, `payday`, `owed_pay`, `hungry`, `attacked`, `agent_lost` or `released`.
- `payday`: `{progress, gap, quarter:0-3, turns_to_payday?, turns_to_next_quarter?}`.

### own

The keys appear in this order.

- **Economy and limits:**
  - `gold`, `territory` (slabs owned), `creatures_active`, `creature_limit`.
  - `objective` and `objective_history[]` (`{text, turn}`, the last 5).
- **`intruders`:**
  - `{count, creatures[{id, kind, owner, level, pos}]}` lists up to 16 enemies standing on the seat's ground.
  - Allies, own prisoners, unconscious creatures and creatures in a hand are left out.
  - It is taken from the decision tick's scan once a turn, not when the view is built.
- **`assist`** is the combat assistant:
  - Settings: `{enabled, interval, charge_time, min_gap, reserve, max_spend, spent_this_payday, pending?}`.
  - `rules[]`: each live rule has its `slot` and its fields, then `fired`, `spent`, `spent_this_payday`, `last_turn?` and `state`.
    - Cast rules have `power` or `power_off`, `level`, `target`, `pick?`, `cooldown`, `target_cooldown`, `budget?`, `max_allies_hit?` and `when`.
    - Hand rules have `hand`, `to?`, `count`, `radius`, `min_match` (reinforce), `cooldown` and `when`.
    - A cast rule's `state` is `ok`, `unavailable`, `budget_spent`, `max_spend`, `reserve` or `cannot_afford`.
    - A hand rule's `state` is `ok`, or why it last held back although something matched: `HEAL_READY`, `NO_MATCH`, `WOULD_NOT_TURN_IT`, `NO_DROP_SPOT`, `NO_LAIR`, or the queue's error code.
  - `reflexes[]` lists the last 16 hand-rule firings: `{turn, rule, hand, verb_id, things[], pos | to_room}`.
  - `active_bank`, and `banks[]` for the banks in use: `{bank (1-based), enabled, reserve, max_spend, rules[]}`.
  - `casts[]` (the last 16): `{turn, rule, power, level, target, cost}`. `target` is 0 for a gold-guard power-off.
- **`creatures[]`:**
  - Fighters come first, then diggers.
  - Each is `{id, born, kind, level, health, health_pct, digger, pos, state, angry, angry_reason?, fleeing}`.
  - `angry_reason` is one of `not_paid`, `hungry`, `no_lair` or `other`.
- **`rooms[]`:**
  - Each is `{id, kind, slabs, pos, efficiency, health, max_health, capacity?{used,total}, gold?, occupants{KIND:n}, open_sides{slabs[[x,y,"N"|"E"|"S"|"W"]], count}}`.
  - `open_sides` lists up to 8 of the room's slabs with a side open to the outside, as slab coordinates.
- **What the seat can build or cast:**
  - `stock` (`{traps{}, doors{}}`, placeable amounts) and `powers[]` (what is available now).
  - `unlocked{rooms, traps, doors}`, `room_costs{ROOM: gold per slab}`, `tendencies{imprison, flee}`.
- **Optional sections:**
  - `custody?` appears while the seat has a prison or torture room, holds prisoners, or has creatures held by an enemy.
    It has `prisons[]`, `torture[]`, `held_by_enemy[]`, `prisoners[]` (with `where`: `hand`/`torture`/`prison`/`moving`) and
    `food{room id: {chickens, ids}}`.
  - `graveyard?` appears with a graveyard: `{rooms[{id, bodies, capacity}], toward_vampire, per_vampire, bodies_lying}`.
  - `temples?` appears with a temple: `[{id, efficiency, pool?, praying}]`.
- **History, research and forces:**
  - `sacrifices{offered{KIND:n}, outcomes[{recipe, turn}]}`.
  - `research{current?{category, name, progress_pct}, queue[], status, library, researchers, books}`. `status` is one of
    `done`, `no_library`, `no_researchers` or `researching`.
  - `alliance{declared[], mutual[]}`.
  - `creature_summary{KIND:{count, max_level, avg_level_x10, avg_health, digger, by_level{}}}`.
  - `creature_info{KIND:{…stats, abilities[{name, from_level}], primary_jobs[], secondary_jobs[], combat}}`. This covers the
    kinds the seat owns. `combat` is a one-line rating, such as `"heavy melee fighter"`.
  - `force{fighters, workers, fighter_score}`.
  - `power_costs{POWER:[cost at charge level 0..8]}`.
- **`events[]`:** `{id, kind, pos, target, lifespan?, text?}`. The engine's event list, with names such as `heart_attacked`, `enemy_fight` and `room_under_attack`.
- **Digging:**
  - `dig_marks[]` and `dig_marks_limit` (300).
  - `unreachable_dig_marks[]` (up to 64) and `unreachable_dig_marks_count`.
  - `open_behind_walls[]` (up to 64): the seat's own walls with open ground behind them. Its count is `open_behind_walls_count`.
- **`traps[]` and `doors[]`** (the seat's own):
  - Traps are `{id, kind, charges, max_charges, rearming, slappable, aimed?, pos}`. -1 charges means unlimited.
  - Doors are `{id, kind, locked, pos}`.
- **`active_powers`:**
  - `POWER_CALL_TO_ARMS{pos?, range}` and `POWER_OBEY{}`.
  - `POWER_SIGHT{pos?, until_turn, open_subtiles, …}`. While it shows anything, it adds `slab_rect`, `rooms[]`,
    `creatures[]` (up to 24, then `creatures_not_listed`), `traps[]`, `doors[]`, `hearts[]`, `loose_gold`, `stored_gold`
    and `gold_slabs`.
- **Optional pickups:**
  - `specials?` is `[{id, kind, reachable, pos}]`.
  - `loose_gold?` is `{total, count, piles[16, largest first], ids[], reachable_gold, reachable_count}`. It excludes
    gold already in treasure rooms.
  - `dead_creatures?` is `[{kind, level}]`, shown while a resurrect box is visible.

### visible

Other players' things, as far as the seat's own vision (and its allies') shows them:

- `creatures[]`: `{id, owner, ally, kind, level, health, digger, pos}`. Invisible enemies are left out.
- `force_by_owner{"<player>": {fighters, workers, fighter_score}}`, counting what is visible.
- `rooms[]`: `{id, owner, ally, kind, slabs_seen, pos}`.
- `traps[]`: `{id, kind, charges, max_charges, rearming, owner, ally, pos}`. A hidden trap appears only once revealed.
- `doors[]`: `{id, kind, locked, owner, ally, pos}`. Hidden doors are left out.
- `hero_gates[]`: `{id, number, pos}`.

### map

- `width` and `height` are in slabs. `cell` is a one-line explanation of the cell format.
- `rows[]` holds one string per slab row, with y = 0 first. Each slab takes 2 characters, so slab x starts at index 2x.
  - The first character is the slab kind. Kinds are numbered from `!` and skip `.`.
  - The second character is the owner digit, or `#` when no digit fits.
  - `..` is a slab that has not been revealed.
  - `.o` is a slab that has not been revealed but faces a revealed wall that has been built against it. Something open
    (ground, a room, a door or another keeper's wall) lies there.
- `legend[]` lists `{c, kind}`, the code name of each slab kind that appears among the revealed slabs. Read the slab kinds
  from the legend, not from the character arithmetic: a kind past the alphabet falls back to `#`.

### Diffs

Every `get_player_view` stores the view it built as that seat's baseline, whether or not a diff was asked for. There is
one baseline per seat, not per client. Pass the previous `view_id` as an integer `since`:

| situation | reply |
|---|---|
| no `since`, or a non-integer one | full view, no `reason` |
| no baseline yet | full view, `reason:"NO_BASE"` |
| `since` is not the baseline's `view_id` | full view, `reason:"BASE_MISMATCH"` |
| the turn is earlier than the baseline's (a load) | full view, `reason:"TURN_WENT_BACKWARDS"` |
| otherwise | `mode:"diff"`, `base:<since>`, with a new `view_id` |

A diff has the shape of the view but holds only what changed:

- **Dicts:** a dict lists only its new and changed keys, and recurses into dicts below it. A key that is gone is named in
  `_removed:[…]` at its level. A dict with no changes is omitted. `turn` always changed.
- **Short integer arrays** (1-4 entries: `pos`, `where`, `slab_rect`, `to`, …) are replaced whole.
- **Other arrays** become `{added?, removed?, changed?}`.
  - Elements with an integer `id` are matched by id. `removed` lists their ids, and `changed` holds
    `{id, <changed fields, diffed>}`.
  - Other elements are matched by content. `removed` holds the old elements, and such an element never appears in `changed`.
  - Duplicate values (as in `power_costs`) are matched one for one by content, so a repeated value can be lost.
- **`map.rows`:** when the row count and lengths are unchanged, `map` holds `changes[{y, x, cells}]` and `revealed[{y, x, len}]` in place of
  `rows`. `changes` lists runs of changed cells, with the new 2-character pairs in `cells`. `revealed` lists runs whose
  kind went from unknown to known.

If a diff can't be applied (an `id` that is missing, or the client lost the base), ask for a full view.

### Coordinates

The API uses two units:

- **Subtiles** are the unit for every position: all `pos`, `decision.where`, `losses[].pos`, `temples[].pool`,
  `reflexes[].pos` and a hand rule's `to`. There are 3 subtiles per slab; slab s has its centre at subtile 3s+1.
  `losses[].pos` is the centre of the first slab lost.
- **Slabs** are the unit of the map: `width`, `height`, `rows` and diff runs, `dig_marks`,
  `unreachable_dig_marks`, `open_behind_walls`, `rooms[].open_sides.slabs` and `POWER_SIGHT.slab_rect`.

Orders follow the same rule: a verb's `pos` is in subtiles, and its `slab_rect` is in slabs.

## get_combat_stats

This action reports what the seat's fights cost and how quickly its keeper answered them. It covers a human keeper or
an External seat from the turn the game first saw it; a load or a new level starts the statistics again. A median is -1
when there is nothing to take it over. A fight's `*_after` fields count turns from the start of the fight.

- **Top level:** `player`, `since_turn`, `turn`, `low_pct` (35) and `critical_pct` (20).
- **`fights`:** `{count, turns, with_cast, median_first_cast_after, median_decision_after, median_orders_after, open?,
  recent[]}`. The last 32 are kept. Each fight is `{start, end, involved, deaths, digger_deaths, casts, cast_gold,
  first_cast_after, first_heal_after, decision_after, orders_after}`.
- **`deaths`:** `{total, combat, combat_diggers, seen_low, healed, median_window_low, median_window_critical, recent[]}`.
  The last 64 are kept. Each death is `{turn, kind, level, digger, window_low, window_critical, heal_after_low,
  fight_age}`.
- **`casts`:** `{count, gold, by_power{POWER:{count, gold}}}`.
- **`upkeep`:** `{cta_gold, cta_paying_turns, obey_gold, obey_turns}`.
- **`paydays`:** `{count, short, recent[{turn, gold, wages}]}`.
- **`decisions`:** `{count, answered, median_latency, combat_answered, median_latency_combat,
  recent[{turn, combat, latency}]}`.

`scripts/llm_bridge/latency.py` splits a decision's latency into its parts: hold, delivery, think, queue and gesture.
