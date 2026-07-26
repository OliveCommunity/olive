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

#ifndef OAKENGINE_PLUGIN_H
#define OAKENGINE_PLUGIN_H

#include "export.h"
#include "init.h"
#include "node.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file plugin.h
 * @brief C ABI for plugin support (active viewer, progress reporter, loading)
 */

/* ---- Active viewer provider -------------------------------------------- */

/** @brief Returns the currently active viewer node (or NULL). */
typedef OakEngineNode *(*oakengine_plugin_active_viewer_fn)(void *userdata);

OAKENGINE_API int oakengine_plugin_set_active_viewer_provider(
    oakengine_plugin_active_viewer_fn fn, void *userdata);

/* ---- Progress reporter factory ----------------------------------------- */

typedef void *(*oakengine_plugin_reporter_create_fn)(
    const char *message, const char *title, void *userdata);
typedef void (*oakengine_plugin_reporter_destroy_fn)(
    void *reporter, void *userdata);
typedef int (*oakengine_plugin_reporter_is_cancelled_fn)(
    void *reporter, void *userdata);
typedef void (*oakengine_plugin_reporter_set_progress_fn)(
    void *reporter, double progress, void *userdata);

OAKENGINE_API int oakengine_plugin_set_progress_reporter_factory(
    oakengine_plugin_reporter_create_fn create,
    oakengine_plugin_reporter_destroy_fn destroy,
    oakengine_plugin_reporter_is_cancelled_fn is_cancelled,
    oakengine_plugin_reporter_set_progress_fn set_progress,
    void *userdata);

/* ---- Plugin loading and interaction ------------------------------------ */

OAKENGINE_API int oakengine_plugin_load_plugins(const char *path);

OAKENGINE_API int oakengine_plugin_node_push_button_clicked(
    OakEngineNode *node, const char *button_id);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_PLUGIN_H */
