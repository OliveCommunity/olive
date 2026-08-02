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

#ifndef OAKENGINE_PROJECT_H
#define OAKENGINE_PROJECT_H

#include "export.h"
#include "init.h"

/* Forward declarations from node.h (included by callers in either order). */
typedef struct OakEngineNode OakEngineNode;

/* Forward declaration for playback cache from viewer.h. */
typedef struct OakEnginePlaybackCache OakEnginePlaybackCache;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file project.h
 * @brief C ABI for Oak project files (.ove)
 *
 * An OakEngineProject wraps the engine's olive::Project node graph. Projects
 * are created empty with oakengine_project_create() and must then be given
 * content exactly once: either oakengine_project_new() for a blank project or
 * oakengine_project_load() to read a project file (the engine's project
 * serializers require a fresh, uninitialized project, so loading into a
 * project that already has content is rejected with OAKENGINE_E_STATE).
 *
 * Sequences are owned by their project through Qt's QObject parent chain;
 * OakEngineSequence handles are borrowed views whose lifetime follows the
 * project. See timeline.h for the sequence function family.
 *
 * Conventions (matching oakengine/ipc.h):
 *   - Booleans are int (1/0).
 *   - Return codes: 0 (OAKENGINE_OK) on success, negative OAKENGINE_E_* on
 *     failure.
 *   - String output uses the buf/size convention: the return value is the
 *     number of characters that would have been written excluding the NUL,
 *     so buf == NULL or a short buffer queries the required size. The output
 *     is NUL-terminated whenever buf_size > 0. A negative return value is an
 *     OAKENGINE_E_* error code.
 *   - NULL handles are accepted everywhere and yield a no-op / zero result /
 *     OAKENGINE_E_INVALID.
 *
 * Undo/redo note: the engine's undo stack is a process-wide singleton held by
 * EngineCore (EngineCore::undo_stack()), not a per-project object. The
 * project_undo/project_redo family therefore operates on that global stack
 * and is not scoped to `self`; oakengine_project_new() and
 * oakengine_project_load() clear it, matching the application's
 * new/open-project behavior.
 */

/**
 * @brief Opaque project handle. Owned by the caller; release with
 * oakengine_project_free().
 */
typedef struct OakEngineProject OakEngineProject;

/**
 * @brief Opaque sequence (timeline) handle.
 *
 * Typedef'd here so project.h and timeline.h can be included in any order.
 * Handles are borrowed from their owning OakEngineProject (QObject parent
 * chain) and must NOT be freed; they become invalid when the project is
 * freed. The sequence function family lives in oakengine/timeline.h.
 */
typedef struct OakEngineSequence OakEngineSequence;

/**
 * @brief Allocate an empty project shell. Owned by the caller.
 *
 * The project has no content until exactly one of oakengine_project_new() or
 * oakengine_project_load() succeeds on it.
 */
OAKENGINE_API OakEngineProject *oakengine_project_create(void);

/**
 * @brief Destroy a project and everything it owns (including sequences).
 * NULL-safe.
 */
OAKENGINE_API void oakengine_project_free(OakEngineProject *self);

/**
 * @brief Initialize `self` as a new, blank project (untitled, not modified).
 *
 * Clears the global undo stack. Returns OAKENGINE_E_STATE if the project
 * already has content (new/load may only be applied once).
 */
OAKENGINE_API int oakengine_project_new(OakEngineProject *self);

/**
 * @brief Load project content from a .ove file.
 *
 * On success the project's filename is set to `path` and the modified flag is
 * cleared; the global undo stack is cleared. Footage is validated like the
 * engine does (EngineCore::validate_footage_in_loaded_project): files that
 * moved together with the project are resolved relatively; missing footage is
 * tolerated (no relink UI exists at this layer, so the project is accepted
 * as-is). On failure a human-readable reason is written to `err` using the
 * buf/size convention (err may be NULL) and a negative code is returned.
 */
OAKENGINE_API int oakengine_project_load(OakEngineProject *self,
										 const char *path, char *err,
										 int err_size);

/**
 * @brief Save the project to `path`.
 *
 * `path` == NULL saves under the project's current filename
 * (Project::filename()); saving an untitled project without a path fails with
 * OAKENGINE_E_INVALID. Files are gzip-compressed unless the target name ends
 * in ".ovexml" (mirrors the application behavior). On success the project's
 * filename is set to the target and the modified flag is cleared
 * (EngineCore::on_project_saved() semantics, minus the UI recent-list).
 */
OAKENGINE_API int oakengine_project_save(OakEngineProject *self,
										 const char *path);

OAKENGINE_API int oakengine_project_is_modified(const OakEngineProject *self);

/**
 * @brief Set the project modified flag (Project::set_modified).
 */
OAKENGINE_API int oakengine_project_set_modified(OakEngineProject *self,
												 int modified);

/**
 * @brief Project display name (Project::name(): the filename's base name, or
 * "(untitled)"). Uses the buf/size convention.
 */
OAKENGINE_API int oakengine_project_name(const OakEngineProject *self,
										 char *buf, int buf_size);

/**
 * @brief Full path the project was loaded from / saved to, or "" if untitled
 * (Project::filename()). Uses the buf/size convention.
 */
OAKENGINE_API int oakengine_project_filename(const OakEngineProject *self,
											 char *buf, int buf_size);

/**
 * @brief Number of footage items in the project.
 */
OAKENGINE_API int oakengine_project_footage_count(const OakEngineProject *self);

/**
 * @brief Stored filename of the footage item at `index` (Footage::filename(),
 * as recorded in the project file; may be relative). Uses the buf/size
 * convention; returns OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int oakengine_project_footage_filename(
	const OakEngineProject *self, int index, char *buf, int buf_size);

/**
 * @brief 1 if the footage file at `index` exists on disk, 0 if not.
 *
 * Mirrors the engine's validation rule: the stored path is checked as-is
 * first, then resolved relative to the project file's directory (footage
 * that moved together with the project stays online). Returns
 * OAKENGINE_E_NOT_FOUND for an out-of-range index.
 */
OAKENGINE_API int
oakengine_project_footage_is_online(const OakEngineProject *self, int index);

/* ---- Undo (global singleton stack, see the file comment above) ---------- */

OAKENGINE_API int oakengine_project_can_undo(const OakEngineProject *self);
OAKENGINE_API int oakengine_project_can_redo(const OakEngineProject *self);
OAKENGINE_API int oakengine_project_undo(OakEngineProject *self);
OAKENGINE_API int oakengine_project_redo(OakEngineProject *self);

/**
 * @brief Number of sequences (timelines) in the project.
 */
OAKENGINE_API int
oakengine_project_sequence_count(const OakEngineProject *self);

/**
 * @brief Borrowed handle of the sequence at `index`, or NULL when out of
 * range. Do NOT free; the sequence is owned by the project.
 */
OAKENGINE_API OakEngineSequence *
oakengine_project_sequence_at(const OakEngineProject *self, int index);

/* ---- Folder operations ---------------------------------------------------- */

/**
 * @brief Create a folder node named `name` under `parent` in `project`.
 * Returns a borrowed handle, or NULL on failure.
 */
OAKENGINE_API OakEngineNode *oakengine_folder_create(OakEngineProject *project,
													 OakEngineNode *parent,
													 const char *name);

/**
 * @brief 1 if `folder` recursively contains `child`, 0 otherwise.
 * 0 when either handle is NULL or `folder` is not a Folder.
 */
OAKENGINE_API int oakengine_folder_has_child_recursive(
	const OakEngineNode *folder, const OakEngineNode *child);

/**
 * @brief Index of `child` in `folder`'s direct children, or
 * OAKENGINE_E_NOT_FOUND. Returns OAKENGINE_E_INVALID when
 * `folder` is not a Folder node.
 */
OAKENGINE_API int oakengine_folder_index_of_child(
	const OakEngineNode *folder, const OakEngineNode *child);

/**
 * @brief Number of direct item children of a folder
 * (Folder::item_child_count()). 0 when `folder` is NULL or not a Folder.
 */
OAKENGINE_API int oakengine_folder_item_child_count(
	const OakEngineNode *folder);

/**
 * @brief Borrowed handle of the item child at `index`
 * (Folder::item_child()). NULL when out of range or `folder` is not a
 * Folder.
 */
OAKENGINE_API OakEngineNode *oakengine_folder_item_child(
	const OakEngineNode *folder, int index);

/**
 * @brief Static input key string for Folder children (Folder::k_child_input).
 * Never freed.
 */
OAKENGINE_API const char *oakengine_folder_child_input_key(void);

/**
 * @brief Add `child` to `folder` (undoable). OAKENGINE_E_INVALID when
 * `folder` is not a Folder node or on NULL args.
 */
OAKENGINE_API int oakengine_folder_add_child(OakEngineNode *folder,
											 OakEngineNode *child);

/**
 * @brief Move `node` from its current folder to `new_folder` (undoable).
 * Removes the node from its old folder first — a true move, not a copy.
 * Returns OAKENGINE_OK or a negative error code.
 */
OAKENGINE_API int oakengine_folder_move_child(OakEngineNode *node,
											  OakEngineNode *new_folder);

/**
 * @brief Create a Folder::RemoveElementCommand as an opaque command pointer.
 * Returns NULL on invalid arguments.
 */
OAKENGINE_API void *oakengine_folder_remove_element_command(
	OakEngineNode *folder, OakEngineNode *child);

/**
 * @brief Move several nodes into `dest_folder` as ONE undoable command
 * (each node is removed from its old folder, then added to `dest_folder`).
 * Nodes already directly inside `dest_folder` are skipped. `undo_name`
 * may be NULL. Returns OAKENGINE_OK or a negative error code.
 */
OAKENGINE_API int oakengine_folder_move_children(
	OakEngineNode *const *nodes, int count, OakEngineNode *dest_folder,
	const char *undo_name);

/* ---- Project extras ------------------------------------------------------- */

/** @brief Root folder node of the project (Project::root()). */
OAKENGINE_API OakEngineNode *oakengine_project_root(OakEngineProject *self);

/** @brief Display name for the project that is safe for window titles
 *  (Project::pretty_filename()). buf/size convention. */
OAKENGINE_API int oakengine_project_pretty_filename(const OakEngineProject *self,
													char *buf, int buf_size);

/** @brief Set the project's filename (Project::set_filename()).
 *  Returns OAKENGINE_OK or OAKENGINE_E_INVALID on NULL. */
OAKENGINE_API int oakengine_project_set_filename(OakEngineProject *self,
												 const char *path);

/** @brief The project's default cache directory (Project::cache_path()).
 *  buf/size convention. */
OAKENGINE_API int oakengine_project_cache_path(const OakEngineProject *self,
											   char *buf, int buf_size);

/** @brief The project's alongside cache directory
 *  (Project::cache_alongside_path()). buf/size convention. */
OAKENGINE_API int oakengine_project_cache_alongside_path(
	const OakEngineProject *self, char *buf, int buf_size);

/** @brief Set a custom cache directory path (Project::set_custom_cache_path()).
 *  NULL clears it. */
OAKENGINE_API int oakengine_project_set_custom_cache_path(
	OakEngineProject *self, const char *path);

/** @brief Get the custom cache directory path, or "" when none is set.
 *  buf/size convention; returns 0 when no custom path is set. */
OAKENGINE_API int oakengine_project_get_custom_cache_path(
	const OakEngineProject *self, char *buf, int buf_size);

/** @brief Cache location setting enum value
 *  (Project::get_cache_location_setting()). Returns < 0 on NULL. */
OAKENGINE_API int oakengine_project_get_cache_location_setting(
	const OakEngineProject *self);

/** @brief Static MIME type string for project items (Project::item_mime_type()).
 *  Never freed. */
OAKENGINE_API const char *oakengine_project_item_mime_type(void);

/** @brief Resolve a project node to its owning OakEngineProject
 *  (Project::get_project_from_object()). Returns NULL when the node is
 *  not part of a project or on NULL input. */
OAKENGINE_API OakEngineProject *
oakengine_project_from_object(const OakEngineNode *node);

/** @brief Get the project's color reference space name (buf/size). */
OAKENGINE_API int oakengine_project_get_color_reference_space(
	const OakEngineProject *self, char *buf, int buf_size);

/** @brief Set the project's color reference space (undoable). */
OAKENGINE_API int oakengine_project_set_color_reference_space(
	OakEngineProject *self, const char *colorspace);

#ifdef __cplusplus
}
#include <QtCore/qmetatype.h>
Q_DECLARE_OPAQUE_POINTER(OakEngineProject *)
Q_DECLARE_OPAQUE_POINTER(OakEngineSequence *)
#endif

#endif /* OAKENGINE_PROJECT_H */
