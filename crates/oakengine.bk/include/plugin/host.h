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

#ifndef OAK_EDITOR_PLUGIN_HOST_H
#define OAK_EDITOR_PLUGIN_HOST_H

#include "plugin/error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the OFX host (olive::plugin::load_plugins() with the
 *        default search paths). Idempotent.
 */
int oakplugin_host_init(void);

/** @brief Shut the host down (persistent messages cleared). */
void oakplugin_host_shutdown(void);

/** @brief Scan additional bundle directories. */
int oakplugin_host_scan(const char *const *bundle_dirs, int dir_count);

/** @brief Number of discovered plugins (>= 0), or a negative error. */
int oakplugin_host_plugin_count(void);

/** @brief Plugin identifier at index (two-stage string getter). */
int oakplugin_host_plugin_id_at(int index, char *buf, int buf_size);

/** @brief Plugin label for an identifier (two-stage; currently the
 *        identifier itself). OAKPLUGIN_E_NOT_FOUND for unknown ids. */
int oakplugin_host_plugin_label(const char *plugin_id, char *buf,
								int buf_size);

/**
 * @brief UI message handler for OFX host messages (question replies use
 *        OAKPLUGIN_MESSAGE_ANSWER_YES/NO). Without a handler, messages
 *        are logged and questions get "no".
 */
#define OAKPLUGIN_MESSAGE_ANSWER_NO 0
#define OAKPLUGIN_MESSAGE_ANSWER_YES 1
typedef int (*oakplugin_message_fn)(const char *type, const char *message,
									void *userdata);
void oakplugin_host_set_message_handler(oakplugin_message_fn fn,
										void *userdata);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_PLUGIN_HOST_H
