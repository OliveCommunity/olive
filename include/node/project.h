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

#ifndef OAK_EDITOR_NODE_PROJECT_H
#define OAK_EDITOR_NODE_PROJECT_H

#include <stdint.h>

#include "node/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file project.h
 * @brief C ABI for olive::Project (oaknode)
 *
 * An OakNodeProject owns its whole node graph: nodes added with
 * oaknode_project_add_node() (directly, or indirectly through the folder and
 * footage families) are deleted when the project's last reference is
 * released. Handles to nodes, folders and footage obtained from a project
 * are borrowed views: releasing them only releases the handle itself.
 *
 * Conventions (shared by all oaknode C API families):
 *   - Return codes: 0 (OAKNODE_OK) on success, a negative OAKNODE_E_* code on
 *     failure.
 *   - String getters are two-stage: pass buf == NULL (or a short buffer) to
 *     query the required size; the return value is the required buffer size in
 *     bytes INCLUDING the terminating NUL. The output is NUL-terminated
 *     whenever buf_size > 0.
 *   - Empty handles (ctx == NULL) yield OAKNODE_E_INVALID (or a no-op for
 *     free()).
 *   - Disk save/load of project files is NOT part of this layer; it belongs to
 *     oakstorage (milestone M10).
 */

/**
 * @brief Reference-counted handle to a project (olive::Project).
 *
 * Semantics are shared_ptr-like: oaknode_project_init() returns a handle
 * whose underlying object has reference count 1, addref(ctx) takes another
 * reference, and release(ctx) (or oaknode_project_free()) drops one; the
 * project and every node it owns are destroyed when the count reaches zero.
 */
typedef struct OakNodeProject {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKNODE_ABI_VERSION. */
} OakNodeProject;

/**
 * @brief Node handle (defined by the node family; forward-declared
 * here so the headers can be included in any order).
 */
typedef struct OakNodeNode OakNodeNode;

/**
 * @brief Folder handle (defined in node/folder.h; forward-declared
 * here so the headers can be included in any order). Handles obtained from
 * a project are borrowed from it.
 */
typedef struct OakNodeFolder OakNodeFolder;

/**
 * @brief Create an empty project shell.
 *
 * The project has no root folder until oaknode_project_initialize() is
 * called (mirrors Project::initialize()).
 *
 * @return Project handle with reference count 1 (release with
 *         oaknode_project_free()); ctx is NULL on allocation failure.
 */
OakNodeProject oaknode_project_init(void);

/**
 * @brief Release one reference to a project handle.
 *
 * Destroys the project and every node it owns when the count reaches zero.
 * NULL handle or NULL ctx is a no-op; clears `project->ctx` after releasing.
 */
void oaknode_project_free(OakNodeProject *project);

/**
 * @brief Initialize the project: create the root folder (Project::initialize()).
 *
 * @return OAKNODE_OK, or OAKNODE_E_STATE if already initialized.
 */
int oaknode_project_initialize(OakNodeProject project);

/**
 * @brief Destructively destroy all nodes in the graph (Project::clear()).
 *
 * The project shell stays usable; oaknode_project_initialize() may be called
 * again afterwards.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_clear(OakNodeProject project);

/**
 * @brief Borrowed handle of the project's root folder (Project::root()).
 *
 * The returned handle only releases the handle itself; the project owns the
 * folder. Empty handle (ctx == NULL) if the project has not been
 * initialized.
 */
OakNodeFolder oaknode_project_root(OakNodeProject project);

/**
 * @brief Project display name (Project::name(): the filename's base name, or
 * "(untitled)"). Two-stage string getter.
 *
 * @return Required buffer size in bytes including the NUL, or a negative
 *         OAKNODE_E_* error code.
 */
int oaknode_project_name(OakNodeProject project, char *buf, int buf_size);

/**
 * @brief Full path the project was saved as, or "" if untitled
 * (Project::filename()). Two-stage string getter.
 */
int oaknode_project_filename(OakNodeProject project, char *buf,
							 int buf_size);

/**
 * @brief Display name safe for window titles (Project::pretty_filename()).
 * Two-stage string getter.
 */
int oaknode_project_pretty_filename(OakNodeProject project, char *buf,
									int buf_size);

/**
 * @brief Set the project's filename (Project::set_filename()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_filename(OakNodeProject project, const char *filename);

/**
 * @brief 1 if the project has unsaved changes, 0 otherwise
 * (Project::is_modified()). Negative OAKNODE_E_* code on an empty handle.
 */
int oaknode_project_is_modified(OakNodeProject project);

/**
 * @brief Set the modified flag (Project::set_modified()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_modified(OakNodeProject project, int modified);

/**
 * @brief 1 if the project is new (untitled and unmodified, Project::is_new()).
 * Negative OAKNODE_E_* code on an empty handle.
 */
int oaknode_project_is_new(OakNodeProject project);

/**
 * @brief Effective cache directory (Project::cache_path(), honoring the cache
 * location setting). Two-stage string getter.
 */
int oaknode_project_cache_path(OakNodeProject project, char *buf,
							   int buf_size);

/**
 * @brief Copy all project settings (Project::copy_settings()).
 */
int oaknode_project_copy_settings(OakNodeProject dst,
								  OakNodeProject src);

/**
 * @brief Cache location setting enum value
 * (Project::get_cache_location_setting(): 0 = default location,
 * 1 = alongside project, 2 = custom path). Negative OAKNODE_E_* code on an
 * empty handle.
 */
int oaknode_project_get_cache_location_setting(OakNodeProject project);

/**
 * @brief Set the cache location setting (0/1/2, see
 * oaknode_project_get_cache_location_setting()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_cache_location_setting(OakNodeProject project,
											   int setting);

/**
 * @brief Custom cache directory, or "" when none is set
 * (Project::get_custom_cache_path()). Two-stage string getter.
 */
int oaknode_project_get_custom_cache_path(OakNodeProject project,
										  char *buf, int buf_size);

/**
 * @brief Set a custom cache directory (Project::set_custom_cache_path()).
 * NULL clears it.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_custom_cache_path(OakNodeProject project,
										  const char *path);

/**
 * @brief Project UUID string (Project::get_uuid()). Two-stage string getter.
 */
int oaknode_project_get_uuid(OakNodeProject project, char *buf,
							 int buf_size);

/**
 * @brief Add a node to the graph; the graph assumes the node's lifetime
 * (Project::add_node()).
 *
 * After a successful call the graph owns the node: releasing `node` only
 * releases the handle itself.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_add_node(OakNodeProject project, OakNodeNode node);

/**
 * @brief Detach a node from the graph without deleting it
 * (Project::remove_node()).
 *
 * @return OAKNODE_OK, OAKNODE_E_NOT_FOUND if the node is not in the graph, or
 *         another negative OAKNODE_E_* error code.
 */
int oaknode_project_remove_node(OakNodeProject project, OakNodeNode node);

/**
 * @brief Number of nodes belonging to the graph (Project::nodes().size()).
 * Negative OAKNODE_E_* code on an empty handle.
 */
int oaknode_project_node_count(OakNodeProject project);

/**
 * @brief Borrowed handle of the graph node at `index`.
 *
 * The returned handle only releases the handle itself. Empty handle
 * (ctx == NULL) when out of range.
 */
OakNodeNode oaknode_project_node_at(OakNodeProject project, int index);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_PROJECT_H
