/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file port_check.c
 *     Safety checks for the cross-layer callback tables. See port_check.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "port_check.h"

#include <string.h>
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
TbBool kfx_table_complete(const void *table, size_t slots, const char *name)
{
    if (table == NULL) {
        ERRORLOG("%s: no table installed", name);
        return false;
    }
    TbBool complete = true;
    const unsigned char *bytes = (const unsigned char *)table;
    for (size_t i = 0; i < slots; i++)
    {
        void (*slot)(void);
        memcpy(&slot, bytes + i * sizeof(slot), sizeof(slot));
        if (slot == NULL) {
            ERRORLOG("%s: slot %d (0-based, in declaration order) is NULL", name, (int)i);
            complete = false;
        }
    }
    return complete;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
