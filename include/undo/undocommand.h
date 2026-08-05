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

#include "undo/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to an undo command (olive::UndoCommand).
 *
 * Handles created by oakundo_command_init() /
 * oakundo_command_init_multi() own the underlying command and must be
 * released with oakundo_command_free() UNLESS they are pushed onto an
 * OakUndoStack, which takes ownership. Handles returned by
 * oakundo_command_multi_child() are borrowed wrappers: free the wrapper
 * with oakundo_command_free(), the underlying command stays owned by the
 * multi command.
 */
typedef struct OakUndoCommand OakUndoCommand;

/**
 * @brief Callback table backing a caller-defined undo command.
 *
 * Any callback may be NULL; a NULL redo/undo makes that direction a
 * no-op. free_fn is invoked when the command is destroyed (whether pushed
 * onto a stack or freed directly) and releases userdata.
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
 * @return Command handle, or NULL on invalid argument or allocation
 *         failure.
 */
OakUndoCommand *oakundo_command_init(const OakUndoCommandVtable *vtable,
									 void *userdata);

/**
 * @brief Create an empty multi command (olive::MultiUndoCommand).
 *
 * @return Command handle, or NULL on allocation failure.
 */
OakUndoCommand *oakundo_command_init_multi(void);

/**
 * @brief Add `child` to the multi command `multi`.
 *
 * On success the multi command takes ownership of the underlying child
 * command; the child handle wrapper is consumed and must not be used or
 * freed afterwards.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_multi_add_child(OakUndoCommand *multi,
									OakUndoCommand *child);

/**
 * @brief Query the number of children in a multi command.
 *
 * @param out_count Receives the result. Must not be NULL.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_multi_child_count(OakUndoCommand *multi, int *out_count);

/**
 * @brief Borrow a handle to the child at `index` of a multi command.
 *
 * The returned handle is a wrapper owned by the caller (free with
 * oakundo_command_free()); the underlying command is owned by `multi`.
 *
 * @return OAKUNDO_OK, OAKUNDO_E_NOT_FOUND for an out-of-range index, or
 *         another negative OAKUNDO_E_* error code.
 */
int oakundo_command_multi_child(OakUndoCommand *multi, int index,
								OakUndoCommand **out_child);

/**
 * @brief Execute the command's redo without a stack
 * (olive::UndoCommand::redo_now semantics; a no-op if already done).
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_redo_now(OakUndoCommand *command);

/**
 * @brief Execute the command's undo without a stack
 * (olive::UndoCommand::undo_now semantics; a no-op if not done).
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_command_undo_now(OakUndoCommand *command);

/**
 * @brief Destroy a command handle.
 *
 * Owned handles destroy the underlying command; borrowed handles (from
 * oakundo_command_multi_child()) only destroy the wrapper. Commands
 * pushed onto an OakUndoStack are owned by the stack and must not be
 * freed by the caller. NULL is a no-op.
 */
void oakundo_command_free(OakUndoCommand *command);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_UNDO_UNDOCOMMAND_H
