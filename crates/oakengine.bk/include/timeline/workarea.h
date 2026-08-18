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

#ifndef OAK_EDITOR_TIMELINE_WORKAREA_H
#define OAK_EDITOR_TIMELINE_WORKAREA_H

#include "common/xmlutils.h"
#include "node/node.h"
#include "timeline/error.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief By-value handle to a timeline work area
 * (olive::TimelineWorkArea).
 *
 * Borrowed handles are obtained via oaktimeline_workarea_of() and box a
 * reference into the owning node; owning handles are created by
 * oaktimeline_workarea_create(). Either way, release with
 * oaktimeline_workarea_free() (or handle.release(handle.ctx)) when
 * done — release destroys the work area only for owning handles.
 */
typedef struct OakTimelineWorkArea {
	void *ctx; /**< Opaque pointer to the object's box. */
	void (*addref)(void *ctx); /**< Atomically increments the box count. */
	void (*release)(void *ctx); /**< Decrements the count, frees the box. */
	uint32_t abi_version; /**< OAKTIMELINE_ABI_VERSION. */
} OakTimelineWorkArea;

/**
 * @brief Create an owning handle to a new, default-constructed work
 * area. Empty handle (ctx == NULL) on allocation failure.
 */
OakTimelineWorkArea oaktimeline_workarea_create(void);

/**
 * @brief Borrowed work area of a viewer node (sequence). Empty handle
 * (ctx == NULL) for an empty node handle or when the node is not a
 * viewer.
 */
OakTimelineWorkArea oaktimeline_workarea_of(OakNodeNode owner);

/**
 * @brief Release a work area handle (destroys the work area itself only
 *        for owning handles). NULL / empty-handle no-op; clears w->ctx
 *        after releasing.
 */
void oaktimeline_workarea_free(OakTimelineWorkArea *w);

/**
 * @brief Set enabled directly (live).
 */
int oaktimeline_workarea_set_enabled(OakTimelineWorkArea w, int enabled);

/**
 * @brief Read the work area state. Out params may individually be NULL.
 */
int oaktimeline_workarea_get(OakTimelineWorkArea w, int *in_num,
							 int *in_den, int *out_num, int *out_den,
							 int *enabled);

/**
 * @brief Set the range directly (live).
 */
int oaktimeline_workarea_set_range(OakTimelineWorkArea w, int in_num,
								   int in_den, int out_num, int out_den);

/**
 * @brief Create a set-range command (olive::WorkareaSetRangeCommand).
 * The old range must be supplied by the caller (facade knows what it
 * changed from). Owned; free with oakundo_command_free().
 */
OakUndoCommand oaktimeline_workarea_set_range_command(
	OakTimelineWorkArea w, int in_num, int in_den, int out_num,
	int out_den, int old_in_num, int old_in_den, int old_out_num,
	int old_out_den);

/**
 * @brief Create a set-enabled command (olive::WorkareaSetEnabledCommand).
 */
OakUndoCommand oaktimeline_workarea_set_enabled_command(
	OakTimelineWorkArea w, int enabled);

/**
 * @brief The reset sentinel range (TimelineWorkArea::k_reset_in/out).
 */
int oaktimeline_workarea_reset(int *in_num, int *in_den, int *out_num,
							   int *out_den);

/**
 * @brief Load/save through oakcommon XML handles. The reader must be
 * positioned on the "workarea" element.
 */
int oaktimeline_workarea_load(OakTimelineWorkArea w, OakXmlReader reader);
int oaktimeline_workarea_save(OakTimelineWorkArea w,
							  OakXmlWriter writer);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_TIMELINE_WORKAREA_H
