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

#ifndef OAK_EDITOR_PLUGIN_INSTANCE_H
#define OAK_EDITOR_PLUGIN_INSTANCE_H

#include <stdint.h>

#include "node/node.h"
#include "plugin/error.h"
#include "render/renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reference-counted handle to an OFX plugin instance
 *        (olive::plugin::OlivePluginInstance).
 *
 * Ownership/count semantics follow include/common/handle.h: create
 * returns count 1, addref/release adjust it, release destroys at zero.
 */
typedef struct OakPluginInstance {
	void *ctx;
	void (*addref)(void *ctx);
	void (*release)(void *ctx);
	uint32_t abi_version; /**< OAKPLUGIN_ABI_VERSION. */
} OakPluginInstance;

/**
 * @brief Create an instance of a discovered plugin (filter context).
 *        Returns an empty handle (ctx == NULL) for unknown ids/failure.
 */
OakPluginInstance oakplugin_instance_create(const char *plugin_id);

/** @brief Release one reference. NULL/empty no-op; clears ctx. */
void oakplugin_instance_free(OakPluginInstance *instance);

/**
 * @brief Set/get a parameter as an oaknode_value POD (type rules from
 *        node/node.h). String-typed params use
 *        oakplugin_instance_set_param_string()/get_param_string().
 */
int oakplugin_instance_set_param(OakPluginInstance instance,
								 const char *param_id,
								 const oaknode_value *value);
int oakplugin_instance_get_param(OakPluginInstance instance,
								 const char *param_id, oaknode_value *out);
int oakplugin_instance_set_param_string(OakPluginInstance instance,
										const char *param_id,
										const char *value);
int oakplugin_instance_get_param_string(OakPluginInstance instance,
										const char *param_id, char *buf,
										int buf_size);

/**
 * @brief Render one frame through the instance (renderAction).
 *
 * `src` may be an empty handle for generator plugins. Textures stay
 * owned by the caller (borrowed for the call).
 */
int oakplugin_instance_render(OakPluginInstance instance,
							  OakRenderTexture dst, OakRenderTexture src,
							  double time_seconds);

/**
 * @brief Progress callback for long renders (async return channel,
 *        01 §4 exception). Return non-zero to abort processing.
 */
typedef int (*oakplugin_progress_fn)(double progress, void *userdata);
int oakplugin_instance_set_progress_cb(OakPluginInstance instance,
									   oakplugin_progress_fn fn,
									   void *userdata);

/** @brief Cancel any in-progress render/progress reporting. */
int oakplugin_instance_cancel(OakPluginInstance instance);

/** @brief Alive-count for leak assertions in tests. */
int oakplugin_debug_alive_count(void);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_PLUGIN_INSTANCE_H
