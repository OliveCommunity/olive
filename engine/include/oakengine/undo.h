/***

  Oak - Non-Linear Video Editor
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

#ifndef OAKENGINE_UNDO_H
#define OAKENGINE_UNDO_H

#include <stdint.h>

#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file undo.h
 * @brief C ABI for the global undo stack (olive::UndoStack)
 *
 * Exposes the process-wide undo stack that backs every editing primitive:
 * pushing commands (the command objects themselves are still created by
 * the caller as opaque engine pointers), jumping to an arbitrary history
 * position, reading the command list for a history view, and the
 * undo/redo QActions for menus.
 *
 * Change notification: subscribe to OAKENGINE_EVENT_UNDO_INDEX_CHANGED on
 * oakengine_undo_handle(). The event fires after every stack mutation
 * (push/undo/redo/jump/clear); the command list must be re-read through
 * oakengine_undo_count()/oakengine_undo_command_text().
 *
 * Conventions (matching oakengine/project.h):
 *   - Return codes: 0 (OAKENGINE_OK) on success, negative OAKENGINE_E_*
 *     on failure. Functions returning a value return OAKENGINE_E_INVALID
 *     when no application core exists.
 *   - String output uses the buf/size convention.
 */

/**
 * @brief Borrowed handle of the global undo stack, for use as the
 * subscription handle of OAKENGINE_EVENT_UNDO_INDEX_CHANGED. Returns NULL
 * when no application core exists.
 */
OAKENGINE_API void *oakengine_undo_handle(void);

/**
 * @brief Push `command` (an opaque `olive::UndoCommand *`, e.g. the result
 * of oakengine_task_import_get_command()) onto the stack and execute its
 * redo. Takes ownership of `command` (an empty MultiUndoCommand is deleted
 * immediately, matching UndoStack::push). `name` is the user-visible
 * command label (NULL behaves like an empty label).
 */
OAKENGINE_API int oakengine_undo_push(void *command, const char *name);

/**
 * @brief Start collecting: subsequent facade undoable operations are added
 * as children to a group and executed eagerly, but not pushed individually.
 * Returns OAKENGINE_E_STATE if a group is already open.
 */
OAKENGINE_API int oakengine_undo_group_begin(const char *name);

/**
 * @brief End the group and push it as ONE undo entry.
 *
 * An empty group is discarded (no undo entry).  Returns OAKENGINE_E_STATE
 * if no group is open.
 */
OAKENGINE_API int oakengine_undo_group_end(void);

/**
 * @brief Abort the open group: undo all already-executed children and
 * discard the group.  Returns OAKENGINE_E_STATE if no group is open.
 */
OAKENGINE_API int oakengine_undo_group_abort(void);

/**
 * @brief Execute the redo of `command` without taking ownership
 * (UndoCommand::redo_now semantics). This is the facade replacement for
 * app code that used to call MultiUndoCommand::redo_now() directly.
 */
OAKENGINE_API int oakengine_undo_command_redo_now(void *command);

/**
 * @brief Execute the undo of `command` without taking ownership
 * (UndoCommand::undo_now semantics).
 */
OAKENGINE_API int oakengine_undo_command_undo_now(void *command);

/**
 * @brief Callback signatures for app-defined undo commands.
 *
 * These allow UI-side code to create undoable actions without defining
 * C++ subclasses of olive::UndoCommand. The engine wraps the callbacks
 * in an internal UndoCommand and forwards redo/undo/free calls.
 */
typedef void (*oakengine_undo_command_redo_fn)(void *userdata);
typedef void (*oakengine_undo_command_undo_fn)(void *userdata);
typedef void (*oakengine_undo_command_free_fn)(void *userdata);

/**
 * @brief Create an app-defined undo command backed by C callbacks.
 *
 * The returned pointer is an opaque `olive::UndoCommand *` suitable for
 * oakengine_undo_push() or oakengine_undo_command_multi_add_child().
 * The command takes ownership of `userdata`; `free_fn` is called when
 * the command is destroyed (whether pushed or freed directly).
 *
 * `name` is the user-visible label. Any callback may be NULL; a NULL
 * redo/undo callback makes that direction a no-op.
 */
OAKENGINE_API void *oakengine_undo_command_create(
		const char *name,
		oakengine_undo_command_redo_fn redo,
		oakengine_undo_command_undo_fn undo,
		oakengine_undo_command_free_fn free_fn,
		void *userdata);

/**
 * @brief Create an empty MultiUndoCommand as an opaque command pointer.
 *
 * The returned pointer is owned by the caller until it is passed to
 * oakengine_undo_push() or freed with oakengine_undo_command_free().
 */
OAKENGINE_API void *oakengine_undo_command_create_multi(void);

OAKENGINE_API void *oakengine_node_add_command(void *project, void *node);
OAKENGINE_API void *oakengine_node_set_position_command(
	void *node, void *context, double x, double y, int expanded);
OAKENGINE_API void *oakengine_node_remove_position_command(
	void *node, void *context);
OAKENGINE_API void *oakengine_node_set_value_hint_command(
	void *node, const char *input, int element, int type, int index,
	const char *tag);
OAKENGINE_API void *oakengine_node_remove_and_disconnect_command(void *node);

OAKENGINE_API void *oakengine_track_place_block_command(
	void *track_list, int track_index, void *block, int64_t in_ts);
OAKENGINE_API void *oakengine_track_replace_block_with_gap_command(
	void *track, void *block, int handle_transitions);
OAKENGINE_API void *oakengine_block_trim_command(
	void *track, void *block, int64_t new_length_num, int64_t new_length_den,
	int movement_mode, int roll_edit);
OAKENGINE_API void *oakengine_transition_remove_command(
	void *transition, int remove_from_graph);
OAKENGINE_API void *oakengine_track_slide_command(
	void *track, void *const *blocks, int block_count,
	void *in_adjacent, void *out_adjacent,
	int64_t movement_num, int64_t movement_den);
OAKENGINE_API void *oakengine_block_split_preserving_links_command(
	void *const *blocks, int count, int64_t point_ts);
OAKENGINE_API void *oakengine_block_split_get_split(
	void *command, void *block, int time_index);
OAKENGINE_API void *oakengine_block_resize_with_media_in_command(
	void *block, int64_t length_num, int64_t length_den);
OAKENGINE_API void *oakengine_block_set_media_in_command(
	void *block, int64_t media_in_num, int64_t media_in_den);
OAKENGINE_API void *oakengine_timeline_ripple_delete_gaps_command(
	void *sequence, const int64_t *range_in_ts, const int64_t *range_out_ts,
	const int *track_types, const int *track_indexes, int range_count);

/**
 * @brief Create a TrackListInsertGaps command as an opaque command pointer.
 * `point_num`/`point_den` is the insertion point in rational seconds;
 * `length_num`/`length_den` is the gap length in rational seconds.
 */
OAKENGINE_API void *oakengine_track_list_insert_gaps_command(
	void *track_list, int64_t point_num, int64_t point_den,
	int64_t length_num, int64_t length_den);

/**
 * @brief Add `child` (an opaque command pointer) to the MultiUndoCommand
 * `multi`. Returns OAKENGINE_OK on success, OAKENGINE_E_INVALID if either
 * argument is NULL.
 */
OAKENGINE_API int oakengine_undo_command_multi_add_child(void *multi,
													 void *child);

/**
 * @brief Return the number of children in the MultiUndoCommand `multi`,
 * or OAKENGINE_E_INVALID if `multi` is NULL.
 */
OAKENGINE_API int oakengine_undo_command_multi_child_count(void *multi);

/**
 * @brief Destroy a command created by oakengine_undo_command_create() or
 * oakengine_undo_command_create_multi() without pushing it onto the stack.
 * Commands passed to oakengine_undo_push() are owned by the stack and
 * must not be freed by the caller.
 */
OAKENGINE_API void oakengine_undo_command_free(void *command);

/**
 * @brief Total number of history rows (done + undone commands), or
 * OAKENGINE_E_INVALID when no stack exists.
 */
OAKENGINE_API int64_t oakengine_undo_count(void);

/**
 * @brief Current position in the history: the number of done commands
 * (rows below this index are undone). Emitted as payload `a` of
 * OAKENGINE_EVENT_UNDO_INDEX_CHANGED.
 */
OAKENGINE_API int64_t oakengine_undo_index(void);

/**
 * @brief Label of the history row at `row` (0-based, buf/size convention).
 * Falls back to the translated "Command" placeholder for empty labels.
 *
 * @return the label length, or OAKENGINE_E_NOT_FOUND for an invalid row.
 */
OAKENGINE_API int oakengine_undo_command_text(int64_t row, char *buf,
											  int buf_size);

/**
 * @brief 1 when the row at `row` is currently done (not undone), 0 when it
 * is undone, OAKENGINE_E_NOT_FOUND for an invalid row.
 */
OAKENGINE_API int oakengine_undo_command_is_done(int64_t row);

/**
 * @brief Undo/redo until the done-command count equals `index`
 * (UndoStack::jump semantics).
 */
OAKENGINE_API int oakengine_undo_jump(int64_t index);

/**
 * @brief Delete all commands and push the fresh "New/Open Project" empty
 * command (UndoStack::clear).
 */
OAKENGINE_API int oakengine_undo_clear(void);

/**
 * @brief Refresh the undo/redo action labels and enabled state
 * (UndoStack::update_actions).
 */
OAKENGINE_API int oakengine_undo_update_actions(void);

/**
 * @brief 1/0 whether undo (redo) is currently possible,
 * OAKENGINE_E_INVALID when no stack exists.
 */
OAKENGINE_API int oakengine_undo_can_undo(void);
OAKENGINE_API int oakengine_undo_can_redo(void);

/**
 * @brief The stack's undo (redo) QAction as an opaque `void *` (actually a
 * `QAction *`; Qt types are allowed at this boundary). Borrowed; owned by
 * the stack. NULL when no stack exists.
 */
OAKENGINE_API void *oakengine_undo_undo_action(void);
OAKENGINE_API void *oakengine_undo_redo_action(void);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_UNDO_H */
