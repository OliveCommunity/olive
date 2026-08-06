/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#ifndef OAK_EDITOR_NODE_BLOCK_H
#define OAK_EDITOR_NODE_BLOCK_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "node/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a timeline block (olive::Block).
 *
 * Covers the whole Block family: ClipBlock, GapBlock and the concrete
 * TransitionBlock subclasses. The handle IS the C++ object pointer; no
 * wrapper is allocated. Concrete instances are created through the
 * oaknode_block_*_create() factories below; callers never touch C++
 * subclasses directly.
 */
typedef struct OakNodeBlock OakNodeBlock;

/**
 * @brief Opaque handle to a track (olive::Track), see node/track.h.
 *
 * Re-declared here so block.h is self-contained; the typedef is identical.
 */
typedef struct OakNodeTrack OakNodeTrack;

/**
 * @brief Opaque handle to a node (olive::Node), see node/node.h.
 *
 * Re-declared here so block.h is self-contained; the typedef is identical.
 */
typedef struct OakNodeNode OakNodeNode;

/**
 * @brief Concrete transition kinds for oaknode_block_transition_create().
 */
enum OakNodeTransitionKind {
	OAKNODE_TRANSITION_CROSS_DISSOLVE = 0, /**< CrossDissolveTransition. */
	OAKNODE_TRANSITION_DIP_TO_COLOR = 1 /**< DipToColorTransition. */
};

/**
 * @brief Input ids of a TransitionBlock's block connections
 * (TransitionBlock::k_out_block_input / k_in_block_input). Pinned by
 * test; pass to oaknode_node_connect()/oaknode_node_disconnect().
 */
#define OAKNODE_TRANSITION_OUT_BLOCK_INPUT "out_block_in"
#define OAKNODE_TRANSITION_IN_BLOCK_INPUT "in_block_in"

/**
 * @brief Create a ClipBlock.
 *
 * The caller owns the block until it is placed on a track that belongs to
 * a project; a block that was never placed must be released with
 * oaknode_block_free().
 *
 * @return Block handle, or NULL on allocation failure.
 */
OakNodeBlock *oaknode_block_clip_create(void);

/**
 * @brief Create a GapBlock. Ownership as oaknode_block_clip_create().
 *
 * @return Block handle, or NULL on allocation failure.
 */
OakNodeBlock *oaknode_block_gap_create(void);

/**
 * @brief Create a concrete TransitionBlock.
 *
 * @param kind One of the OakNodeTransitionKind values.
 * @return Block handle, or NULL on invalid kind / allocation failure.
 */
OakNodeBlock *oaknode_block_transition_create(int kind);

/**
 * @brief Destroy a block. No-op on NULL.
 *
 * The block must not be placed on a track or linked to other nodes; the
 * caller is responsible for detaching it first.
 */
void oaknode_block_free(OakNodeBlock *block);

enum OakNodeBlockKind {
	OAKNODE_BLOCK_OTHER = 0,
	OAKNODE_BLOCK_CLIP = 1,
	OAKNODE_BLOCK_GAP = 2,
	OAKNODE_BLOCK_TRANSITION = 3
};

/**
 * @brief Concrete kind of a block (dynamic_cast query).
 */
int oaknode_block_get_kind(OakNodeBlock *block, int *out_kind);

/**
 * @brief Borrowed cast from a block handle to its node handle.
 *
 * Every Block is a Node; the result must not be freed. NULL for NULL.
 */
OakNodeNode *oaknode_block_as_node(OakNodeBlock *block);

/**
 * @brief Borrowed cast from a node handle to a block handle.
 *
 * Returns NULL if the node is not a Block (or for NULL).
 */
OakNodeBlock *oaknode_block_from_node(OakNodeNode *node);

/**
 * @brief Rational getters/setters use numerator/denominator out pairs.
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_block_get_in(OakNodeBlock *block, int *numerator, int *denominator);
int oaknode_block_set_in(OakNodeBlock *block, int numerator, int denominator);
int oaknode_block_get_out(OakNodeBlock *block, int *numerator, int *denominator);
int oaknode_block_set_out(OakNodeBlock *block, int numerator, int denominator);
int oaknode_block_get_length(OakNodeBlock *block, int *numerator,
							 int *denominator);

/**
 * @brief Set the block length, keeping the media out/in point anchored
 * (olive::Block::set_length_and_media_out / _media_in).
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_block_set_length_and_media_out(OakNodeBlock *block, int numerator,
										   int denominator);
int oaknode_block_set_length_and_media_in(OakNodeBlock *block, int numerator,
										  int denominator);

/**
 * @brief Enabled flag (olive::Block::is_enabled/set_enabled).
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_block_get_enabled(OakNodeBlock *block, int *enabled);
int oaknode_block_set_enabled(OakNodeBlock *block, int enabled);

/**
 * @brief Adjacency accessors. `out` receives a borrowed handle (NULL when
 * there is no neighbour / the block is not on a track).
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_block_get_previous(OakNodeBlock *block, OakNodeBlock **out);
int oaknode_block_get_next(OakNodeBlock *block, OakNodeBlock **out);
int oaknode_block_get_track(OakNodeBlock *block, OakNodeTrack **out);

/**
 * @brief Link two blocks (olive::Node::link/unlink/are_linked).
 *
 * Linked blocks move together in timeline edits.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_FAILED (already
 * linked / not linked).
 */
int oaknode_block_link(OakNodeBlock *a, OakNodeBlock *b);
int oaknode_block_unlink(OakNodeBlock *a, OakNodeBlock *b);
int oaknode_block_are_linked(OakNodeBlock *a, OakNodeBlock *b, int *linked);

/**
 * @brief Number of blocks linked to `block` (olive::Node::links()).
 *
 * @return OAKNODE_OK or OAKNODE_E_INVALID.
 */
int oaknode_block_get_link_count(OakNodeBlock *block, int *count);

/**
 * @brief Borrowed handle to the linked block at `index`.
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_NOT_FOUND.
 */
int oaknode_block_get_link_at(OakNodeBlock *block, int index,
							  OakNodeBlock **out);

/* ---------------------------------------------------------------- Clip */

/**
 * @brief Media in/out accessors (olive::ClipBlock). Non-clip blocks return
 * OAKNODE_E_INVALID.
 */
int oaknode_clip_get_media_in(OakNodeBlock *clip, int *numerator,
							  int *denominator);
int oaknode_clip_set_media_in(OakNodeBlock *clip, int numerator,
							  int denominator);

/**
 * @brief Playback speed factor, 1.0 = normal (olive::ClipBlock speed input).
 */
int oaknode_clip_get_speed(OakNodeBlock *clip, double *speed);
int oaknode_clip_set_speed(OakNodeBlock *clip, double speed);

/**
 * @brief Reverse playback flag.
 */
int oaknode_clip_get_reverse(OakNodeBlock *clip, int *reverse);
int oaknode_clip_set_reverse(OakNodeBlock *clip, int reverse);

/**
 * @brief Maintain-audio-pitch flag.
 */
int oaknode_clip_get_maintain_audio_pitch(OakNodeBlock *clip, int *maintain);
int oaknode_clip_set_maintain_audio_pitch(OakNodeBlock *clip, int maintain);

/**
 * @brief Loop mode, one of the OakLoopMode values
 * (olive::ClipBlock::loop_mode/set_loop_mode).
 */
int oaknode_clip_get_loop_mode(OakNodeBlock *clip, int *loop_mode);
int oaknode_clip_set_loop_mode(OakNodeBlock *clip, int loop_mode);

/**
 * @brief Type of the track the clip sits on (OakNodeTrackType values,
 * OAKNODE_TRACK_TYPE_NONE when trackless).
 */
int oaknode_clip_get_track_type(OakNodeBlock *clip, int *type);

/* ----------------------------------------------------------- Transition */

/**
 * @brief Transition offsets (olive::TransitionBlock). Non-transition blocks
 * return OAKNODE_E_INVALID.
 */
int oaknode_transition_get_in_offset(OakNodeBlock *transition, int *numerator,
									 int *denominator);
int oaknode_transition_get_out_offset(OakNodeBlock *transition, int *numerator,
									  int *denominator);
int oaknode_transition_get_offset_center(OakNodeBlock *transition,
										 int *numerator, int *denominator);
int oaknode_transition_set_offset_center(OakNodeBlock *transition,
										 int numerator, int denominator);
int oaknode_transition_set_offsets_and_length(OakNodeBlock *transition,
											  int in_num, int in_den,
											  int out_num, int out_den);

/**
 * @brief Whether both sides of the transition are connected to clips.
 */
int oaknode_transition_is_dual(OakNodeBlock *transition, int *dual);

/**
 * @brief Borrowed handles to the connected out/in side blocks (NULL when
 * unconnected).
 */
int oaknode_transition_get_connected_out_block(OakNodeBlock *transition,
											   OakNodeBlock **out);
int oaknode_transition_get_connected_in_block(OakNodeBlock *transition,
											  OakNodeBlock **out);

/**
 * @brief Forward cache passthroughs from another clip
 * (ClipBlock::add_cache_passthrough_from()). Used after splitting a
 * clip so the new part shares the render caches.
 */
int oaknode_clip_add_cache_passthrough_from(OakNodeBlock *clip,
											OakNodeBlock *other);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_BLOCK_H
