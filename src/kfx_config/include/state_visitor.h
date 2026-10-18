/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file state_visitor.h
 *     A visitor over raw state blobs: called with each piece's live address and size.
 * @par Purpose:
 *     The game state a network resync carries (kfx_net's resync_visit_state())
 *     is made of blobs owned by libraries on both sides of kfx_net: the ones
 *     above it hand theirs over through a port taking this visitor
 *     (GamePort, UiPort). The same pieces are what a replay rewind snapshots.
 *     Same shape as Ariadne's AriadneStateVisitor (ariadne_saved_state.h).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_STATE_VISITOR_H
#define DK_STATE_VISITOR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
/** Called with each piece of a state blob in turn: its live address and size. */
typedef void (*KfxStateVisitor)(void *ctx, void *data, size_t size);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
