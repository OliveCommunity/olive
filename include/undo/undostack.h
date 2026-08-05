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

#ifndef OAK_EDITOR_UNDO_UNDOSTACK_H
#define OAK_EDITOR_UNDO_UNDOSTACK_H

#include <stdint.h>

#include "undo/error.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to an undo stack (olive::UndoStack).
 */
typedef struct OakUndoStack OakUndoStack;

/**
 * @brief Create an undo stack.
 *
 * A fresh stack contains a single "New/Open Project" empty command,
 * matching olive::UndoStack::clear().
 *
 * @return Stack handle, or NULL on allocation failure.
 */
OakUndoStack *oakundo_undostack_init(void);

/**
 * @brief Destroy an undo stack and all commands it owns.
 *
 * NULL is a no-op.
 */
void oakundo_undostack_free(OakUndoStack *stack);

/**
 * @brief Push `command` onto the stack and execute its redo.
 *
 * On success the stack takes ownership of the underlying command; the
 * handle wrapper is consumed and must not be used or freed afterwards.
 * An empty multi command is deleted immediately (not pushed), matching
 * olive::UndoStack::push. `name` is the user-visible label (NULL behaves
 * like an empty label).
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_push(OakUndoStack *stack, OakUndoCommand *command,
						   const char *name);

/**
 * @brief Push a command that has already been executed (redo skipped).
 *
 * Ownership rules match oakundo_undostack_push().
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_push_pre_executed(OakUndoStack *stack,
										OakUndoCommand *command,
										const char *name);

/**
 * @brief Undo the most recently done command, if any.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_undo(OakUndoStack *stack);

/**
 * @brief Redo the most recently undone command, if any.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_redo(OakUndoStack *stack);

/**
 * @brief Undo/redo until the done-command count equals `index`
 * (olive::UndoStack::jump semantics). Negative values are clamped to 0.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_jump(OakUndoStack *stack, int64_t index);

/**
 * @brief Delete all commands and push the fresh "New/Open Project" empty
 * command (olive::UndoStack::clear).
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_clear(OakUndoStack *stack);

/**
 * @brief Query whether undo (redo) is currently possible.
 *
 * @param out_value Receives 1/0. Must not be NULL.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_can_undo(OakUndoStack *stack, int *out_value);
int oakundo_undostack_can_redo(OakUndoStack *stack, int *out_value);

/**
 * @brief Total number of history rows (done + undone commands).
 *
 * @param out_count Receives the result. Must not be NULL.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_count(OakUndoStack *stack, int64_t *out_count);

/**
 * @brief Current position in the history: the number of done commands
 * (rows at or above this index are undone).
 *
 * @param out_index Receives the result. Must not be NULL.
 *
 * @return OAKUNDO_OK or a negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_index(OakUndoStack *stack, int64_t *out_index);

/**
 * @brief Label of the history row at `row` (0-based, two-stage getter).
 *
 * @return Required buffer size in bytes including the terminating NUL
 *         (non-negative), OAKUNDO_E_NOT_FOUND for an invalid row, or
 *         another negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_command_text(OakUndoStack *stack, int64_t row,
								   char *buf, int buf_size);

/**
 * @brief Query whether the row at `row` is currently done (not undone).
 *
 * @param out_value Receives 1 (done) / 0 (undone). Must not be NULL.
 *
 * @return OAKUNDO_OK, OAKUNDO_E_NOT_FOUND for an invalid row, or another
 *         negative OAKUNDO_E_* error code.
 */
int oakundo_undostack_command_is_done(OakUndoStack *stack, int64_t row,
									  int *out_value);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_UNDO_UNDOSTACK_H
