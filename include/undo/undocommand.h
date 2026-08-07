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

#ifndef OAK_EDITOR_UNDO_UNDOCOMMAND_H
#define OAK_EDITOR_UNDO_UNDOCOMMAND_H

#include <stdint.h>

#include "undo/error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OAKUNDO_ABI_VERSION 1

/**
 * @brief Reference-counted handle to an undo command
 * (olive::UndoCommand).
 *
 * The object never leaves the library that created it; every external
 * reference is one of these handles. Semantics are shared_ptr-like:
 * init/factory functions return a handle with count 1, addref(ctx)
 * takes another reference, release(ctx) drops one and the library
 * destroys the object when the count reaches zero.
 *
 * Pushing a command onto an OakUndoStack transfers one reference to the
 * stack (the stack releases it when the command is discarded); callers
 * may keep their own reference or release it right after the push.
 */
typedef struct OakUndoCommand {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKUNDO_ABI_VERSION. */
} OakUndoCommand;

/**
 * @brief Callback table backing a caller-defined undo command.
 *
 * Any callback may be NULL; a NULL redo/undo makes that direction a
 * no-op. free_fn is invoked when the command is destroyed (whether held
 * by a stack or released directly) and releases userdata.
 */
typedef struct OakUndoCommandVtable {
	void (*redo)(void *userdata);
	void (*undo)(void *userdata);
	void (*free_fn)(void *userdata);
} OakUndoCommandVtable;

/**
 * @brief Create an undo command backed by C callbacks.
 *
 * The command takes ownership of `userdata`; `vtable` is copied.
 *
 * @return Command handle with count 1; ctx is NULL on invalid argument
 *         or allocation failure.
 */
OakUndoCommand oakundo_command_init(const OakUndoCommandVtable *vtable,
									void *userdata);

/**
 * @brief Create an empty multi command (olive::MultiUndoCommand).
 *
 * @return Command handle with count 1; ctx is NULL on allocation
 *         failure.
 */
OakUndoCommand oakundo_command_init_multi(void);

/**
 * @brief Add `child` to the multi command `multi`.
 *
 * The multi command takes one reference to the child; the caller keeps
 * its own reference and may release it after the call.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_multi_add_child(OakUndoCommand multi,
									OakUndoCommand child);

/**
 * @brief Query the number of children in a multi command.
 *
 * @param out_count Receives the result. Must not be NULL.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_multi_child_count(OakUndoCommand multi,
									  int *out_count);

/**
 * @brief Reference to the child at `index` of a multi command.
 *
 * The returned handle carries its own reference; release it with
 * oakundo_command_free().
 *
 * @return OAKUNDO_OK, OAKUNDO_E_NOT_FOUND for an out-of-range index, or
 *         another negative OAKUNDO_E_* error code.
 */
int oakundo_command_multi_child(OakUndoCommand multi, int index,
								OakUndoCommand *out_child);

/**
 * @brief Execute the command's redo without a stack
 * (olive::UndoCommand::redo_now semantics; a no-op if already done).
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_redo_now(OakUndoCommand command);

/**
 * @brief Execute the command's undo without a stack
 * (olive::UndoCommand::undo_now semantics; a no-op if not done).
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_undo_now(OakUndoCommand command);

/**
 * @brief Release one reference to a command handle.
 *
 * Convenience wrapper around handle.release(handle.ctx): destroys the
 * command when the count reaches zero. NULL handle or NULL ctx is a
 * no-op; clears `command->ctx` after releasing.
 */
void oakundo_command_free(OakUndoCommand *command);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_UNDO_UNDOCOMMAND_H
