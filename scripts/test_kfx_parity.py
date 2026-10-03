#!/usr/bin/env python3
"""Tests for kfx_parity.py's source extraction and comparison (no git needed).

    python3 -m unittest scripts/test_kfx_parity.py
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import kfx_parity as kp  # noqa: E402

RULES_C = r'''
// {"COMMENTED_OUT", 0, ...} must not count
static const struct NamedField rules_game_named_fields[] = {
  {"POTOFGOLDHOLDS", 0, field_t(struct RulesConfig, gameplay.pot_of_gold_holds), 1000, 0, 1, NULL, value_default, assign_default},
  /* {"ALSO_COMMENTED", 0} */
  {"Nested", 0, {1, 2}, 0},
  {NULL, 0},
};
const struct NamedCommand rules_research_commands[] = {
  {"RESEARCH", 1},
  {NULL, 0},
};
const struct NamedCommand research_desc[] = {
  {"MAGIC", 1},
  {"ROOM", 2},
  {NULL, 0},
};
static void parse(void) {
    int64_t cmd_num = recognize_conf_command(buf, &pos, len, rules_research_commands);
    CONDITIONAL_ASSIGN_INT(section, "TomlKey", x);
}
'''

SCRIPT_C = r'''
const struct CommandDesc command_desc[] = {
  {"LEVEL_VERSION", "N       ", Cmd_LEVEL_VERSION, NULL, NULL},
  {"quick_message", "NAA     ", Cmd_QUICK_MESSAGE, NULL, NULL},
  {NULL, "        ", Cmd_NONE, NULL, NULL},
};
const struct NamedCommand player_desc[] = {
  {"PLAYER0", 0},
  {NULL, 0},
};
static const luaL_Reg global_methods[] = {
  {"SetGenerateSpeed", lua_Set_generate_speed},
  {NULL, NULL}
};
void reg(lua_State *L) { lua_register(L, "PlaySound", lua_PlaySound); }
'''

SETTINGS_C = r'''
const struct NamedCommand conf_commands[] = {
  {"INSTALL_PATH", 1},
  {NULL, 0},
};
void load(void) { int64_t c = recognize_conf_command(buf, &pos, len, conf_commands); }
'''


def names(surface: kp.Surface, category: str, table: str) -> set[str]:
    return surface.tables[category].get(table, set())


class ExtractionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.s = kp.scan_sources({"src/config_rules.c": RULES_C, "src/lvl_script_commands.c": SCRIPT_C,
                                  "src/config_keeperfx.c": SETTINGS_C}, "test")

    def test_named_fields_are_config_keys_without_comments_or_null(self) -> None:
        self.assertEqual(names(self.s, "config key", "rules_game_named_fields"), {"POTOFGOLDHOLDS", "NESTED"})

    def test_legacy_key_table_is_recognised_by_its_parser_call(self) -> None:
        self.assertEqual(names(self.s, "config key", "rules_research_commands"), {"RESEARCH"})
        self.assertEqual(names(self.s, "config name", "research_desc"), {"MAGIC", "ROOM"})

    def test_toml_keys(self) -> None:
        self.assertEqual(names(self.s, "config key", "<toml:config_rules>"), {"TOMLKEY"})

    def test_script_commands_are_uppercased(self) -> None:
        self.assertEqual(names(self.s, "script command", "command_desc"), {"LEVEL_VERSION", "QUICK_MESSAGE"})
        self.assertEqual(names(self.s, "script name", "player_desc"), {"PLAYER0"})

    def test_lua_keeps_case(self) -> None:
        self.assertEqual(names(self.s, "lua", "global_methods"), {"SetGenerateSpeed"})
        self.assertEqual(names(self.s, "lua", "<global>"), {"PlaySound"})

    def test_settings_files_are_not_content(self) -> None:
        self.assertEqual(names(self.s, "settings", "conf_commands"), {"INSTALL_PATH"})
        self.assertNotIn("conf_commands", self.s.tables["config key"])


class CompareTest(unittest.TestCase):
    def test_missing_and_fork_only(self) -> None:
        up = kp.scan_sources({"src/lvl_script_commands.c": SCRIPT_C}, "up")
        ours_text = SCRIPT_C.replace('{"quick_message"', '{"NEW_ONE", "N", Cmd_X, NULL, NULL},\n  {"unused"')
        ours = kp.scan_sources({"src/lvl_script_commands.c": ours_text}, "ours")
        r = kp.compare(up, ours)["categories"]["script command"]
        self.assertEqual(r["missing"], {"command_desc": ["QUICK_MESSAGE"]})
        self.assertEqual(r["fork_only"], {"command_desc": ["NEW_ONE", "UNUSED"]})

    def test_a_table_moved_to_another_file_keeps_upstreams_category(self) -> None:
        up = kp.scan_sources({"src/lvl_script.h": SCRIPT_C}, "up")
        # This tree moved player_desc into a config_ file, which alone would make it a "config name".
        moved = 'const struct NamedCommand player_desc[] = {\n  {"PLAYER0", 0},\n  {NULL, 0},\n};\n'
        ours = kp.scan_sources({"src/lvl_script_commands.c": SCRIPT_C.replace(moved, ""),
                                "src/kfx_config/src/config_players.c": moved}, "ours")
        self.assertIn("player_desc", ours.tables["config name"])
        kp.canonicalise(up, ours)
        r = kp.compare(up, ours)["categories"]
        self.assertEqual(r["script name"]["missing"], {})
        self.assertEqual(r["config name"]["fork_only"], {})

    def test_renamed_table_is_matched_by_name(self) -> None:
        up = kp.scan_sources({"src/config_rules.c": RULES_C}, "up")
        ours = kp.scan_sources({"src/config_rules.c": RULES_C.replace("rules_game_named_fields", "rules_gameplay_fields")}, "ours")
        r = kp.compare(up, ours)["categories"]["config key"]
        self.assertEqual(r["missing"], {})
        self.assertEqual(r["fork_only"], {})
        self.assertEqual(r["tables_only_upstream"], ["rules_game_named_fields"])


if __name__ == "__main__":
    unittest.main()
