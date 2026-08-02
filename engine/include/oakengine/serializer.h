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

#ifndef OAKENGINE_SERIALIZER_H
#define OAKENGINE_SERIALIZER_H

#include "export.h"
#include "init.h"
#include "node.h"
#include "project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file serializer.h
 * @brief C ABI for project serialization / copy-paste (olive::ProjectSerializer)
 *
 * A thin facade over ProjectSerializer's load/save/copy/paste primitives. The
 * opaque OakEngineClipboard handle bundles a SaveData object (for copy/save)
 * or the LoadData result of the last paste operation. Clipboard handles are
 * owned by the caller and must be released with oakengine_clipboard_free().
 *
 * Conventions match the other facade families:
 *   - 0 (OAKENGINE_OK) / negative OAKENGINE_E_* codes.
 *   - String output uses the buf/size convention.
 */

/** @brief Opaque clipboard context. */
typedef struct OakEngineClipboard OakEngineClipboard;

/** @brief Marker handle (defined in oakengine/timeline.h). */
typedef struct OakEngineMarker OakEngineMarker;

#define OAKENGINE_CLIPBOARD_PROJECT 0
#define OAKENGINE_CLIPBOARD_NODES 1
#define OAKENGINE_CLIPBOARD_CLIPS 2
#define OAKENGINE_CLIPBOARD_MARKERS 3
#define OAKENGINE_CLIPBOARD_KEYFRAMES 4

#define OAKENGINE_SERIALIZER_OK 0
#define OAKENGINE_SERIALIZER_TOO_OLD 1
#define OAKENGINE_SERIALIZER_TOO_NEW 2
#define OAKENGINE_SERIALIZER_UNKNOWN_VERSION 3
#define OAKENGINE_SERIALIZER_FILE_ERROR 4
#define OAKENGINE_SERIALIZER_XML_ERROR 5
#define OAKENGINE_SERIALIZER_OVERWRITE_ERROR 6
#define OAKENGINE_SERIALIZER_NO_DATA 7

/**
 * @brief Returns 1 if `filename` is a compressed project file, 0 otherwise.
 */
OAKENGINE_API int oakengine_serializer_check_compressed(const char *filename);

/**
 * @brief Create a clipboard context for copy/save operations.
 *
 * `load_type` is one of OAKENGINE_CLIPBOARD_*. `project` may be NULL for
 * load types that do not require it. `filename` may be NULL.
 */
OAKENGINE_API OakEngineClipboard *oakengine_clipboard_create(
    int load_type, OakEngineProject *project, const char *filename);

/**
 * @brief Set the nodes to serialize on this clipboard.
 *
 * Replaces any previously set nodes. `nodes` is an array of `count` borrowed
 * OakEngineNode handles. Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_clipboard_set_nodes(OakEngineClipboard *cb,
                                                const OakEngineNode *const *nodes,
                                                int count);

/**
 * @brief Set the markers to serialize on this clipboard.
 */
OAKENGINE_API int oakengine_clipboard_set_markers(
    OakEngineClipboard *cb, const OakEngineMarker *const *markers, int count);

/**
 * @brief Set the keyframes to serialize on this clipboard.
 */
OAKENGINE_API int oakengine_clipboard_set_keyframes(
    OakEngineClipboard *cb, const OakEngineKeyframe *const *keyframes,
    int count);

/**
 * @brief Set a serialized property attached to a node.
 *
 * Properties are free-form (key, value) strings attached to pasted nodes; the
 * editor uses them for clip in-points/track-refs and node graph positions.
 * Replaces the value if the same (node, key) pair is set twice. Returns
 * OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_clipboard_set_property(OakEngineClipboard *cb,
                                                   OakEngineNode *node,
                                                   const char *key,
                                                   const char *value);

/**
 * @brief Copy this clipboard's data to the system clipboard.
 *
 * Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_clipboard_copy(OakEngineClipboard *cb);

/**
 * @brief Serialize this clipboard's data to XML (buf/size convention).
 *
 * Returns the string length on success, or a negative OAKENGINE_E_* code on
 * error.
 */
OAKENGINE_API int oakengine_clipboard_save_to_xml(OakEngineClipboard *cb,
                                                  char *buf, int buf_size);

/**
 * @brief Paste data from the system clipboard into `project`.
 *
 * `load_type` selects what kind of data to paste. On success `*result_code`
 * receives OAKENGINE_SERIALIZER_OK and the clipboard is populated with the
 * paste result (accessible through the oakengine_clipboard_get_loaded_*
 * accessors). On failure `*result_code` receives one of the
 * OAKENGINE_SERIALIZER_* error codes and a human-readable detail string is
 * written to `details_buf` (may be NULL). Returns OAKENGINE_OK on success or
 * an error code.
 */
OAKENGINE_API int oakengine_clipboard_paste(OakEngineClipboard *cb,
                                            int load_type,
                                            OakEngineProject *project,
                                            int *result_code,
                                            char *details_buf,
                                            int details_buf_size);

/**
 * @brief Paste data from the system clipboard, invoking `map_fn` for each
 *        original->new node mapping.
 *
 * The callback is called once per (original node pointer, pasted node pointer)
 * pair found in the paste result. The app can use it to build an existing-node
 * map without exposing C++ containers across the boundary. Returning non-zero
 * from the callback stops iteration early. Other semantics match
 * oakengine_clipboard_paste().
 */
OAKENGINE_API int oakengine_clipboard_paste_with_map(
    OakEngineClipboard *cb, int load_type, OakEngineProject *project,
    int (*map_fn)(OakEngineNode *old, OakEngineNode *new_node, void *userdata),
    void *userdata, int *result_code, char *details_buf,
    int details_buf_size);

/**
 * @brief Destroy a clipboard context.
 */
OAKENGINE_API void oakengine_clipboard_free(OakEngineClipboard *cb);

/* ---- Paste result accessors (valid after a successful paste) -------------- */

/**
 * @brief Number of nodes loaded by the last paste operation.
 */
OAKENGINE_API int oakengine_clipboard_get_loaded_node_count(
    OakEngineClipboard *cb);

/**
 * @brief Borrowed node handle loaded at `index`.
 */
OAKENGINE_API OakEngineNode *oakengine_clipboard_get_loaded_node_at(
    OakEngineClipboard *cb, int index);

/**
 * @brief Number of markers loaded by the last paste operation.
 */
OAKENGINE_API int oakengine_clipboard_get_loaded_marker_count(
    OakEngineClipboard *cb);

/**
 * @brief Borrowed marker handle loaded at `index`.
 */
OAKENGINE_API OakEngineMarker *oakengine_clipboard_get_loaded_marker_at(
    OakEngineClipboard *cb, int index);

/**
 * @brief Number of keyframes loaded by the last paste operation.
 */
OAKENGINE_API int oakengine_clipboard_get_loaded_keyframe_count(
    OakEngineClipboard *cb);

/**
 * @brief Borrowed keyframe handle loaded at `index`.
 */
OAKENGINE_API OakEngineKeyframe *oakengine_clipboard_get_loaded_keyframe_at(
    OakEngineClipboard *cb, int index);

/**
 * @brief Iterate over the serialized properties attached to pasted nodes.
 *
 * For each (node, key, value) triple `fn` is called. Returning non-zero stops
 * iteration early. Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_clipboard_foreach_property(
    OakEngineClipboard *cb,
    int (*fn)(OakEngineNode *node, const char *key, const char *value,
              void *userdata),
    void *userdata);

/**
 * @brief Iterate over keyframes loaded by the last paste operation.
 *
 * For each keyframe `fn` is called with the node id string it belongs to and
 * the keyframe handle. Returning non-zero stops iteration early. The app can
 * group keyframes by node id and route them to the correct destination node.
 * Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_clipboard_foreach_keyframe(
    OakEngineClipboard *cb,
    int (*fn)(const char *node_id, OakEngineKeyframe *keyframe,
              void *userdata),
    void *userdata);

/**
 * @brief Iterate over promised connections from the paste result.
 *
 * For each promised edge `fn` is called with the output node, input node,
 * input id and element index. Returning non-zero stops iteration early.
 * Returns OAKENGINE_OK or an error code.
 */
OAKENGINE_API int oakengine_clipboard_foreach_connection(
    OakEngineClipboard *cb,
    int (*fn)(OakEngineNode *output_node, OakEngineNode *input_node,
              const char *input_id, int element, void *userdata),
    void *userdata);

#ifdef __cplusplus
}
Q_DECLARE_OPAQUE_POINTER(OakEngineClipboard *)
Q_DECLARE_OPAQUE_POINTER(OakEngineMarker *)
#endif

#endif /* OAKENGINE_SERIALIZER_H */
