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

#ifndef OAK_EDITOR_TIMELINE_MARKER_H
#define OAK_EDITOR_TIMELINE_MARKER_H

#include "common/xmlutils.h"
#include "node/node.h"
#include "timeline/error.h"
#include "undo/undocommand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Borrowed by-value handle to a timeline marker list
 * (olive::TimelineMarkerList), owned by a viewer node.
 *
 * Obtained via oaktimeline_marker_list_of(). The handle boxes a
 * reference into the owning node: addref/release manage the box only,
 * never the list. Release the box with oaktimeline_marker_list_free()
 * (or handle.release(handle.ctx)) when done.
 */
typedef struct OakTimelineMarkerList {
	void *ctx; /**< Opaque pointer to the borrowed object's box. */
	void (*addref)(void *ctx); /**< Atomically increments the box count. */
	void (*release)(void *ctx); /**< Decrements the count, frees the box. */
	uint32_t abi_version; /**< OAKTIMELINE_ABI_VERSION. */
} OakTimelineMarkerList;

/**
 * @brief Borrowed marker list of a viewer node (sequence). Empty handle
 * (ctx == NULL) for an empty node handle or when the node is not a
 * viewer.
 */
OakTimelineMarkerList oaktimeline_marker_list_of(OakNodeNode owner);

/**
 * @brief Release a borrowed marker list box (never the list itself).
 * NULL / empty-handle no-op; clears list->ctx after releasing.
 */
void oaktimeline_marker_list_free(OakTimelineMarkerList *list);

/**
 * @brief Number of markers. Out-param convention; OAKTIMELINE_E_INVALID
 * for empty/NULL arguments.
 */
int oaktimeline_marker_count(OakTimelineMarkerList list, int *out_count);

/**
 * @brief Marker at index: time as num/den pairs, color and name
 * (two-stage string). OAKTIMELINE_E_NOT_FOUND when out of range.
 */
int oaktimeline_marker_at(OakTimelineMarkerList list, int index,
						  int *in_num, int *in_den, int *out_num, int *out_den,
						  int *color, char *name_buf, int buf_size);

/**
 * @brief Create a command that adds a marker (olive::MarkerAddCommand).
 *
 * Owned command; free with oakundo_command_free(). Redo it directly or
 * push it on an undo stack. Empty handle on failure.
 */
OakUndoCommand oaktimeline_marker_add_command(
	OakTimelineMarkerList list, int in_num, int in_den, int out_num,
	int out_den, const char *name, int color);

/**
 * @brief Create a command that removes the marker at `index`.
 * OAKTIMELINE_E_NOT_FOUND (as an empty result documented by error) is
 * reported by returning an empty handle.
 */
OakUndoCommand oaktimeline_marker_remove_at_command(
	OakTimelineMarkerList list, int index);

/**
 * @brief Create a command that sets a marker's time range.
 */
OakUndoCommand oaktimeline_marker_set_time_command(
	OakTimelineMarkerList list, int index, int in_num, int in_den,
	int out_num, int out_den);

/**
 * @brief Create a command that sets a marker's color and/or name.
 * `name` may be NULL to leave the name unchanged (color still applies
 * when >= 0; both NULL-name and color < 0 is a no-op error).
 */
OakUndoCommand oaktimeline_marker_set_props_command(
	OakTimelineMarkerList list, int index, int color, const char *name);

/**
 * @brief Load/save the list through oakcommon XML handles. The reader
 * must be positioned on the wrapping element (e.g. "markers").
 */
int oaktimeline_marker_list_load(OakTimelineMarkerList list,
								 OakXmlReader reader);
int oaktimeline_marker_list_save(OakTimelineMarkerList list,
								 OakXmlWriter writer);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_TIMELINE_MARKER_H
