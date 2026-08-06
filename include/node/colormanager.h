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

#ifndef OAK_EDITOR_NODE_COLORMANAGER_H
#define OAK_EDITOR_NODE_COLORMANAGER_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include "common/colortransform.h"
#include "node/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a color manager (olive::ColorManager).
 *
 * Unlike node handles this one IS a wrapper allocation (ColorManager is
 * not a Node); release with oaknode_colormanager_free().
 */
typedef struct OakNodeColorManager OakNodeColorManager;

/**
 * @brief Opaque borrowed handle to a project (olive::Project).
 *
 * Owned by the project family; re-declared here so this header is
 * self-contained.
 */
typedef struct OakNodeProject OakNodeProject;

/**
 * @brief Create a color manager bound to `project` (borrowed).
 *
 * The manager is created without a config; call
 * oaknode_colormanager_initialize() (or set a config filename and
 * oaknode_colormanager_update_config_from_filename()) before using the
 * config-dependent queries.
 *
 * @return Manager handle, or NULL on NULL project / allocation failure.
 */
OakNodeColorManager *oaknode_colormanager_init(OakNodeProject *project);

/**
 * @brief Destroy a color manager. No-op on NULL.
 */
void oaknode_colormanager_free(OakNodeColorManager *manager);

/**
 * @brief Load the built-in default OCIO config and set the default input
 * colorspace (olive::ColorManager::init()).
 *
 * @return OAKNODE_OK, OAKNODE_E_INVALID or OAKNODE_E_FAILED (the OCIO
 * config could not be created).
 */
int oaknode_colormanager_initialize(OakNodeColorManager *manager);

/**
 * @brief (Re)build the process-wide default OCIO config
 * (olive::ColorManager::set_up_default_config()).
 *
 * @return OAKNODE_OK or OAKNODE_E_FAILED.
 */
int oaknode_colormanager_set_up_default_config(void);

/**
 * @brief Config filename stored on the project. Two-stage string getter:
 * returns the required buffer size in bytes including NUL; pass
 * buf == NULL or a too-small buffer to query the size.
 */
int oaknode_colormanager_get_config_filename(OakNodeColorManager *manager,
											 char *buf, int buf_size);
int oaknode_colormanager_set_config_filename(OakNodeColorManager *manager,
											 const char *filename);

/**
 * @brief Reload the OCIO config from the stored filename. Missing/invalid
 * files are tolerated (the previous config is kept), matching
 * olive::ColorManager::update_config_from_filename().
 */
int oaknode_colormanager_update_config_from_filename(
	OakNodeColorManager *manager);

/**
 * @brief Default input colorspace. Two-stage string accessor.
 */
int oaknode_colormanager_get_default_input_color_space(
	OakNodeColorManager *manager, char *buf, int buf_size);
int oaknode_colormanager_set_default_input_color_space(
	OakNodeColorManager *manager, const char *colorspace);

/**
 * @brief Reference (working) colorspace. Two-stage string getter.
 */
int oaknode_colormanager_get_reference_color_space(
	OakNodeColorManager *manager, char *buf, int buf_size);

/**
 * @brief Return `colorspace` when the active config lists it, otherwise the
 * default input colorspace. Two-stage string getter. Requires a config
 * (OAKNODE_E_STATE when none is loaded).
 */
int oaknode_colormanager_get_compliant_color_space(
	OakNodeColorManager *manager, const char *colorspace, char *buf,
	int buf_size);

/**
 * @brief Map FFmpeg color primaries/transfer codes to a colorspace of the
 * active config. Two-stage string getter; an empty result (required size
 * 1) means "unknown tags, use the default". Requires a config
 * (OAKNODE_E_STATE when none is loaded).
 */
int oaknode_colormanager_get_colorspace_for_ffmpeg_tags(
	OakNodeColorManager *manager, int primaries, int trc, char *buf,
	int buf_size);

/**
 * @brief Config listings. Count + per-index two-stage string getters.
 * All require a loaded config (OAKNODE_E_STATE otherwise); index out of
 * range yields OAKNODE_E_NOT_FOUND.
 */
int oaknode_colormanager_get_display_count(OakNodeColorManager *manager,
										   int *count);
int oaknode_colormanager_get_display_at(OakNodeColorManager *manager,
										int index, char *buf, int buf_size);
int oaknode_colormanager_get_default_display(OakNodeColorManager *manager,
											 char *buf, int buf_size);
int oaknode_colormanager_get_view_count(OakNodeColorManager *manager,
										const char *display, int *count);
int oaknode_colormanager_get_view_at(OakNodeColorManager *manager,
									 const char *display, int index, char *buf,
									 int buf_size);
int oaknode_colormanager_get_default_view(OakNodeColorManager *manager,
										  const char *display, char *buf,
										  int buf_size);
int oaknode_colormanager_get_look_count(OakNodeColorManager *manager,
										int *count);
int oaknode_colormanager_get_look_at(OakNodeColorManager *manager, int index,
									 char *buf, int buf_size);
int oaknode_colormanager_get_colorspace_count(OakNodeColorManager *manager,
											  int *count);
int oaknode_colormanager_get_colorspace_at(OakNodeColorManager *manager,
										   int index, char *buf,
										   int buf_size);

/**
 * @brief Default luma coefficients of the active config into rgb[3].
 * Requires a loaded config (OAKNODE_E_STATE otherwise).
 */
int oaknode_colormanager_get_default_luma_coefs(OakNodeColorManager *manager,
												double rgb[3]);

/**
 * @brief Return a copy of `transform` whose display/view/look (or output
 * colorspace) is clamped to what the active config offers
 * (olive::ColorManager::get_compliant_color_space(ColorTransform, bool)).
 *
 * `out` receives a NEW by-value handle owned by the caller (reference
 * count 1, release with oakcommon_colortransform_free()). Requires a
 * loaded config (OAKNODE_E_STATE otherwise).
 */
int oaknode_colormanager_get_compliant_color_transform(
	OakNodeColorManager *manager, OakColorTransform transform,
	int force_display, OakColorTransform *out);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_NODE_COLORMANAGER_H
