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

#ifndef OAK_EDITOR_DROPWORKFLOWBEHAVIOR_H
#define OAK_EDITOR_DROPWORKFLOWBEHAVIOR_H

#include "common/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Behavior when media is dropped onto a timeline without a
 * sequence.
 *
 * Mirrors olive::DropWithoutSequenceBehavior in
 * src/common/src/dropworkflowbehavior.h; enumerator order and values
 * must stay identical because the config layer persists them as ints.
 */
enum OakCommonDropWorkflowBehavior {
	OAKCOMMON_DWS_ASK = 0, /**< Ask the user every time. */
	OAKCOMMON_DWS_AUTO = 1, /**< Automatically create a sequence. */
	OAKCOMMON_DWS_MANUAL = 2, /**< Never create; import manually. */
	OAKCOMMON_DWS_DISABLE = 3 /**< Disable dropping entirely. */
};

/**
 * @brief Check whether value is a valid OakCommonDropWorkflowBehavior.
 *
 * @param value Integer behavior value (e.g. read from config).
 * @return 1 if valid, 0 otherwise (this is a predicate, not a status
 * code).
 */
int oakcommon_drop_workflow_behavior_is_valid(int value);

/**
 * @brief Copy the printable name of a behavior into buf.
 *
 * Two-segment string getter: if buf is NULL or buf_size is too small,
 * nothing is written. Invalid values yield "UNKNOWN".
 *
 * @param value One of OakCommonDropWorkflowBehavior.
 * @param buf Destination buffer, may be NULL to query the size.
 * @param buf_size Size of buf in bytes.
 * @return Required buffer size in bytes including the terminating NUL
 * (non-negative).
 */
int oakcommon_drop_workflow_behavior_name(int value, char *buf,
					  int buf_size);

#ifdef __cplusplus
}
#endif

#endif // OAK_EDITOR_DROPWORKFLOWBEHAVIOR_H
