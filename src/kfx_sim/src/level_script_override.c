/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file level_script_override.c
 *     See level_script_override.h.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "level_script_override.h"

#include <stdlib.h>
#include <string.h>
#include "post_inc.h"

/******************************************************************************/
static TbBool override_set = false;
static LevelNumber override_lvnum = 0;
static char *override_prelude = NULL;
static char *override_masked = NULL;

static char *copy_string(const char *s)
{
    size_t n = (s != NULL) ? strlen(s) : 0;
    char *c = (char *)malloc(n + 1);
    if (c == NULL)
        return NULL;
    if (n > 0)
        memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

void level_script_override_clear(void)
{
    free(override_prelude);
    free(override_masked);
    override_prelude = NULL;
    override_masked = NULL;
    override_lvnum = 0;
    override_set = false;
}

void level_script_override_set(LevelNumber lvnum, const char *prelude, const char *masked)
{
    level_script_override_clear();
    override_prelude = copy_string(prelude);
    override_masked = copy_string(masked);
    if (override_prelude == NULL || override_masked == NULL)
    {
        level_script_override_clear(); // out of memory: run the shipped script rather than half an override
        return;
    }
    override_lvnum = lvnum;
    override_set = true;
}

TbBool level_script_override_is_set(void)
{
    return override_set;
}

LevelNumber level_script_override_level(void)
{
    return override_set ? override_lvnum : 0;
}

TbBool level_script_override_matches(LevelNumber lvnum)
{
    return override_set && (override_lvnum == lvnum);
}

const char *level_script_override_prelude(void)
{
    return override_set ? override_prelude : NULL;
}

const char *level_script_override_masked(void)
{
    return override_set ? override_masked : NULL;
}
