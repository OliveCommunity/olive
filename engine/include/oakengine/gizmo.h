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

#ifndef OAKENGINE_GIZMO_H
#define OAKENGINE_GIZMO_H

#include <stdint.h>

#include "export.h"
#include "node.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gizmo.h
 * @brief C ABI for gizmo data exchange (text gizmo POD + draggable helpers)
 *
 * TextGizmo has been POD-ified: the app retrieves a flat snapshot of the
 * text v3 node's gizmo state through a single C call instead of holding a
 * C++ TextGizmo pointer. The 4 Qt signals (activated/deactivated/
 * rect_changed/vertical_alignment_changed) are replaced by the existing
 * OAKENGINE_EVENT_NODE_INPUT_VALUE_CHANGED on the text v3 node.
 *
 * DraggableGizmo's drag lifecycle (start/move/end) is exposed as thin C
 * wrappers so the app can drive dragging without importing engine C++ symbols.
 */

/**
 * @brief Flat snapshot of a text v3 node's gizmo state.
 *
 * Retrieved via oakengine_text_gizmo_get(). The HTML content is accessed
 * separately through oakengine_text_gizmo_get_html() because it is a
 * variable-length string.
 */
typedef struct oakengine_text_gizmo {
    double rect_x;          /**< Bounding rect left */
    double rect_y;          /**< Bounding rect top */
    double rect_w;          /**< Bounding rect width */
    double rect_h;          /**< Bounding rect height */
    int vertical_alignment; /**< 0 = AlignTop, 1 = AlignBottom, 2 = AlignVCenter */
} oakengine_text_gizmo;

/**
 * @brief Retrieve the text gizmo POD from a TextGeneratorV3 node.
 *
 * @param node  The text v3 node (OakEngineNode*). Must be a TextGeneratorV3
 *              (checked at runtime; returns OAKENGINE_E_INVALID otherwise).
 * @param time_num  Numerator of the rational time at which to evaluate.
 * @param time_den  Denominator of the rational time.
 * @param out       Output struct filled on success.
 * @return OAKENGINE_OK on success, OAKENGINE_E_INVALID if node is not a
 *         TextGeneratorV3 or out is NULL.
 */
OAKENGINE_API int oakengine_text_gizmo_get(OakEngineNode *node,
    int64_t time_num, int64_t time_den, oakengine_text_gizmo *out);

/**
 * @brief Retrieve the text gizmo's HTML content as a string.
 *
 * buf/size convention: pass NULL/0 to get the required length (including
 * NUL terminator). On success returns the number of bytes written (excluding
 * NUL). Requires a valid TextGeneratorV3 node.
 */
OAKENGINE_API int oakengine_text_gizmo_get_html(OakEngineNode *node,
    int64_t time_num, int64_t time_den, char *buf, int buf_size);

/**
 * @brief Update the HTML content of a text v3 node's text input (undoable).
 *
 * Equivalent to the old TextGizmo::update_input_html().
 */
OAKENGINE_API int oakengine_text_gizmo_update_html(OakEngineNode *node,
    const char *html, int64_t time_num, int64_t time_den);

/**
 * @brief Set the vertical alignment of a text v3 node (undoable).
 *
 * `alignment`: 0 = AlignTop, 1 = AlignBottom, 2 = AlignVCenter.
 * Equivalent to the old TextGizmo::set_vertical_alignment().
 */
OAKENGINE_API int oakengine_text_gizmo_set_vertical_alignment(
    OakEngineNode *node, int alignment);

/**
 * @brief Notify that a text gizmo has been activated (emits the equivalent of
 * the old TextGizmo::activated signal via event mechanism).
 *
 * Currently a no-op since activation events are app-internal; kept for
 * API completeness.
 */
OAKENGINE_API int oakengine_text_gizmo_activated(OakEngineNode *node);

/**
 * @brief Notify that a text gizmo has been deactivated.
 */
OAKENGINE_API int oakengine_text_gizmo_deactivated(OakEngineNode *node);

/**
 * @brief Activate/Deactivate the text gizmo on a text v3 node.
 *
 * These replace the old TextGizmo::activated()/deactivated() signal emissions.
 * The app calls these to notify the engine that the text editor opened/closed.
 */

/**
 * @brief Get the DragValueBehavior of a gizmo node.
 *
 * Returns: 0 = k_absolute, 1 = k_delta_from_previous, 2 = k_delta_from_start.
 * Returns OAKENGINE_E_INVALID if the gizmo is not a DraggableGizmo.
 */
OAKENGINE_API int oakengine_gizmo_get_drag_value_behavior(void *gizmo);

/**
 * @brief Start a drag on a DraggableGizmo.
 *
 * Wraps DraggableGizmo::drag_start(). The gizmo's internal NodeInputDraggers
 * are started at the given time. `row` is a pointer to a NodeValueRow
 * (populated e.g. by oakengine_traverse_generate_row); pass NULL for an
 * empty row.
 */
OAKENGINE_API int oakengine_gizmo_drag_start(void *gizmo,
    void *row, double abs_x, double abs_y, int64_t time_num,
    int64_t time_den);

/**
 * @brief Move a drag (emits handle_movement signal on the gizmo).
 */
OAKENGINE_API int oakengine_gizmo_drag_move(void *gizmo,
    double x, double y, int qt_keyboard_modifiers);

/**
 * @brief End a drag and push an undoable command.
 *
 * @param gizmo    The DraggableGizmo pointer (void* for C ABI).
 * @param command  A MultiUndoCommand* (void*) to append undo entries to.
 *                 Pass NULL to create a standalone command.
 */
OAKENGINE_API int oakengine_gizmo_drag_end(void *gizmo, void *command);

#ifdef __cplusplus
}
#endif

#endif // OAKENGINE_GIZMO_H
