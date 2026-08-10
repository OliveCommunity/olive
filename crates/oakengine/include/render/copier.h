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

#ifndef OAK_EDITOR_RENDER_COPIER_H
#define OAK_EDITOR_RENDER_COPIER_H

#include "node/node.h"
#include "node/project.h"
#include "render/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reference-counted handle to a project copier
 *        (olive::ProjectCopier): deep-copies a project graph for
 *        background processing (export/precache).
 *
 * By-value handle (shared_ptr semantics, see oakcommon's
 * common/handle.h): oakrender_project_copier_create() returns a handle
 * with reference count 1; release it with
 * oakrender_project_copier_free().
 */
typedef struct OakRenderProjectCopier {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKRENDER_ABI_VERSION. */
} OakRenderProjectCopier;

/**
 * @brief Create a copier. The copy is built by
 *        oakrender_project_copier_set_project().
 *
 * @return Copier handle with reference count 1; ctx is NULL on
 *         allocation failure.
 */
OakRenderProjectCopier oakrender_project_copier_create(void);

/**
 * @brief Release one reference to a copier; the final release frees the
 * copier AND its copied project. NULL / empty-handle no-op; clears
 * copier->ctx after releasing.
 */
void oakrender_project_copier_free(OakRenderProjectCopier *copier);

/** @brief (Re)build the copy from `project` (borrowed handle). */
int oakrender_project_copier_set_project(OakRenderProjectCopier copier,
										 OakNodeProject project);

/** @brief The copied counterpart of an original node (borrowed handle;
 *        freeing it only releases the handle box), empty handle when the
 *        node is not in the copied project. */
OakNodeNode oakrender_project_copier_get_copy(
	OakRenderProjectCopier copier, OakNodeNode original);

/** @brief The copied project (borrowed handle; freeing it only releases
 *        the handle box). */
OakNodeProject oakrender_project_copier_get_copied_project(
	OakRenderProjectCopier copier);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_RENDER_COPIER_H
