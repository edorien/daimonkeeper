/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_campaign.h
 *     docs/refactor/editor/fx-plans/08 -- the Campaign editor window (K1: read-only browser: identity, levels, check).
 * @par Comment:
 *     Internal to kfx_editor; reached through content_tools (main menu and Map Editor Tools menus).
 */
/******************************************************************************/
#ifndef DK_CONTENT_CAMPAIGN_H
#define DK_CONTENT_CAMPAIGN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void content_campaign_open(bool map_host);
void content_campaign_frame(void);
bool content_campaign_is_open(void);

/* Functional-test hooks: the window's own edit and apply paths, without drawing it. */
bool content_campaign_test_select(const char *fname);
void content_campaign_test_set(const char *block, const char *key, const char *value);
bool content_campaign_test_apply(void);
bool content_campaign_test_own_config(void);
/* "add_single", "add_extra", "bonus" (of the level `at`), "up" (level `n` one place up), "remove" (level `n`, entry too). */
bool content_campaign_test_level_op(const char *op, int64_t n, int64_t at);
/* The Config files page: creates the campaign-layer file `name` when it is missing; returns how many files the campaign has then (-1: no such file). */
int64_t content_campaign_test_create_file(const char *name);

#ifdef __cplusplus
}
#endif
#endif
