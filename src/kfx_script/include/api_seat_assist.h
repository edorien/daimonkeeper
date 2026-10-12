/**
 * @file api_seat_assist.h
 * @brief The combat assistant in the API (docs/newFeatures/llm/02-rule-language.md): own.assist in the view. set_assist's
 *        JSON is read by kfx_config's keeper_assist_parse_set(), shared with profiles and presets.
 */
#ifndef DK_API_SEAT_ASSIST_H
#define DK_API_SEAT_ASSIST_H

#include "globals.h"
#include "keeper_assist_rules.h"
#include <json-dom.h>

#ifdef __cplusplus
extern "C" {
#endif

struct KeeperAssistState;

/** Adds own.assist (settings, each rule with what became of it, the latest casts) to a view for `plyr_idx`. */
void api_assist_add_to_view(VALUE *own, PlayerNumber plyr_idx);

#ifdef __cplusplus
}
#endif

#endif
