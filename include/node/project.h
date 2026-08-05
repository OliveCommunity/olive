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
 * footage families) are deleted by oaknode_project_free(). Handles to nodes,
 * folders and footage obtained from a project are borrowed views and must not
 * be freed.
 *
 * Conventions (shared by all oaknode C API families):
 *   - Return codes: 0 (OAKNODE_OK) on success, a negative OAKNODE_E_* code on
 *     failure.
 *   - String getters are two-stage: pass buf == NULL (or a short buffer) to
 *     query the required size; the return value is the required buffer size in
 *     bytes INCLUDING the terminating NUL. The output is NUL-terminated
 *     whenever buf_size > 0.
 *   - NULL handles yield OAKNODE_E_INVALID (or a no-op for free()).
 *   - Disk save/load of project files is NOT part of this layer; it belongs to
 *     oakstorage (milestone M10).
 */

/**
 * @brief Opaque project handle. Owned by the caller; release with
 * oaknode_project_free().
 */
typedef struct OakNodeProject OakNodeProject;

/**
 * @brief Opaque node handle (defined by the node family; forward-declared
 * here so the headers can be included in any order).
 */
typedef struct OakNodeNode OakNodeNode;

/**
 * @brief Opaque folder handle (defined in node/folder.h; forward-declared
 * here so the headers can be included in any order). Borrowed from the
 * owning project.
 */
typedef struct OakNodeFolder OakNodeFolder;

/**
 * @brief Create an empty project shell.
 *
 * The project has no root folder until oaknode_project_initialize() is
 * called (mirrors Project::initialize()).
 *
 * @return Project handle, or NULL on allocation failure.
 */
OakNodeProject *oaknode_project_init(void);

/**
 * @brief Destroy a project and every node it owns. NULL is a no-op.
 */
void oaknode_project_free(OakNodeProject *project);

/**
 * @brief Initialize the project: create the root folder (Project::initialize()).
 *
 * @return OAKNODE_OK, or OAKNODE_E_STATE if already initialized.
 */
int oaknode_project_initialize(OakNodeProject *project);

/**
 * @brief Destructively destroy all nodes in the graph (Project::clear()).
 *
 * The project shell stays usable; oaknode_project_initialize() may be called
 * again afterwards.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_clear(OakNodeProject *project);

/**
 * @brief Borrowed handle of the project's root folder (Project::root()).
 *
 * NULL if the project has not been initialized.
 */
OakNodeFolder *oaknode_project_root(OakNodeProject *project);

/**
 * @brief Project display name (Project::name(): the filename's base name, or
 * "(untitled)"). Two-stage string getter.
 *
 * @return Required buffer size in bytes including the NUL, or a negative
 *         OAKNODE_E_* error code.
 */
int oaknode_project_name(const OakNodeProject *project, char *buf, int buf_size);

/**
 * @brief Full path the project was saved as, or "" if untitled
 * (Project::filename()). Two-stage string getter.
 */
int oaknode_project_filename(const OakNodeProject *project, char *buf,
							 int buf_size);

/**
 * @brief Display name safe for window titles (Project::pretty_filename()).
 * Two-stage string getter.
 */
int oaknode_project_pretty_filename(const OakNodeProject *project, char *buf,
									int buf_size);

/**
 * @brief Set the project's filename (Project::set_filename()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_filename(OakNodeProject *project, const char *filename);

/**
 * @brief 1 if the project has unsaved changes, 0 otherwise
 * (Project::is_modified()). Negative OAKNODE_E_* code on NULL.
 */
int oaknode_project_is_modified(const OakNodeProject *project);

/**
 * @brief Set the modified flag (Project::set_modified()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_modified(OakNodeProject *project, int modified);

/**
 * @brief 1 if the project is new (untitled and unmodified, Project::is_new()).
 * Negative OAKNODE_E_* code on NULL.
 */
int oaknode_project_is_new(const OakNodeProject *project);

/**
 * @brief Effective cache directory (Project::cache_path(), honoring the cache
 * location setting). Two-stage string getter.
 */
int oaknode_project_cache_path(const OakNodeProject *project, char *buf,
							   int buf_size);

/**
 * @brief Cache location setting enum value
 * (Project::get_cache_location_setting(): 0 = default location,
 * 1 = alongside project, 2 = custom path). Negative OAKNODE_E_* code on NULL.
 */
int oaknode_project_get_cache_location_setting(const OakNodeProject *project);

/**
 * @brief Set the cache location setting (0/1/2, see
 * oaknode_project_get_cache_location_setting()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_cache_location_setting(OakNodeProject *project,
											   int setting);

/**
 * @brief Custom cache directory, or "" when none is set
 * (Project::get_custom_cache_path()). Two-stage string getter.
 */
int oaknode_project_get_custom_cache_path(const OakNodeProject *project,
										  char *buf, int buf_size);

/**
 * @brief Set a custom cache directory (Project::set_custom_cache_path()).
 * NULL clears it.
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_set_custom_cache_path(OakNodeProject *project,
										  const char *path);

/**
 * @brief Project UUID string (Project::get_uuid()). Two-stage string getter.
 */
int oaknode_project_get_uuid(const OakNodeProject *project, char *buf,
							 int buf_size);

/**
 * @brief Add a node to the graph; the project takes ownership
 * (Project::add_node()).
 *
 * @return OAKNODE_OK or a negative OAKNODE_E_* error code.
 */
int oaknode_project_add_node(OakNodeProject *project, OakNodeNode *node);

/**
 * @brief Detach a node from the graph without deleting it
 * (Project::remove_node()).
 *
 * @return OAKNODE_OK, OAKNODE_E_NOT_FOUND if the node is not in the graph, or
 *         another negative OAKNODE_E_* error code.
 */
int oaknode_project_remove_node(OakNodeProject *project, OakNodeNode *node);

/**
 * @brief Number of nodes belonging to the graph (Project::nodes().size()).
 * Negative OAKNODE_E_* code on NULL.
 */
int oaknode_project_node_count(const OakNodeProject *project);

/**
 * @brief Borrowed handle of the graph node at `index`, or NULL when out of
 * range.
 */
OakNodeNode *oaknode_project_node_at(const OakNodeProject *project, int index);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_PROJECT_H
