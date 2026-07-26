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

#include "oakengine/plugin.h"

#include "coreengine.h"
#include "node/node.h"
#include "node/output/viewer/viewer.h"
#include "pluginSupport/olivehost.h"
#include "pluginSupport/oliveplugininstance.h"
#include "pluginSupport/pluginprogressreporter.h"

extern "C"
{

static oakengine_plugin_active_viewer_fn g_active_viewer_fn = nullptr;
static void *g_active_viewer_userdata = nullptr;

static oakengine_plugin_reporter_create_fn g_reporter_create = nullptr;
static oakengine_plugin_reporter_destroy_fn g_reporter_destroy = nullptr;
static oakengine_plugin_reporter_is_cancelled_fn g_reporter_is_cancelled = nullptr;
static oakengine_plugin_reporter_set_progress_fn g_reporter_set_progress = nullptr;
static void *g_reporter_userdata = nullptr;

int oakengine_plugin_set_active_viewer_provider(
    oakengine_plugin_active_viewer_fn fn, void *userdata)
{
    g_active_viewer_fn = fn;
    g_active_viewer_userdata = userdata;

    // Update the engine's viewer provider lambda.
    olive::plugin::set_active_viewer_provider(
        []() -> olive::ViewerOutput * {
            if (!g_active_viewer_fn) {
                return nullptr;
            }
            return reinterpret_cast<olive::ViewerOutput *>(
                g_active_viewer_fn(g_active_viewer_userdata));
        });
    return OAKENGINE_OK;
}

int oakengine_plugin_set_progress_reporter_factory(
    oakengine_plugin_reporter_create_fn create,
    oakengine_plugin_reporter_destroy_fn destroy,
    oakengine_plugin_reporter_is_cancelled_fn is_cancelled,
    oakengine_plugin_reporter_set_progress_fn set_progress,
    void *userdata)
{
    g_reporter_create = create;
    g_reporter_destroy = destroy;
    g_reporter_is_cancelled = is_cancelled;
    g_reporter_set_progress = set_progress;
    g_reporter_userdata = userdata;

    // Register factory with the engine.
    olive::plugin::set_plugin_progress_reporter_factory(
        [](const QString &message, const QString &title)
            -> olive::plugin::PluginProgressReporter * {
            if (!g_reporter_create) {
                return nullptr;
            }
            void *reporter = g_reporter_create(
                message.toUtf8().constData(),
                title.toUtf8().constData(),
                g_reporter_userdata);
            if (!reporter) {
                return nullptr;
            }
            // Create an adapter that wraps the C callbacks.
            class CAdapter : public olive::plugin::PluginProgressReporter {
            public:
                CAdapter(void *reporter,
                         oakengine_plugin_reporter_destroy_fn destroy,
                         oakengine_plugin_reporter_is_cancelled_fn is_cancelled,
                         oakengine_plugin_reporter_set_progress_fn set_progress,
                         void *userdata)
                    : PluginProgressReporter()
                    , reporter_(reporter)
                    , destroy_(destroy)
                    , set_progress_(set_progress)
                    , userdata_(userdata) {}
                ~CAdapter() override
                {
                    if (destroy_) {
                        destroy_(reporter_, userdata_);
                    }
                }
                void set_progress(double value) override
                {
                    if (set_progress_) {
                        set_progress_(reporter_, value, userdata_);
                    }
                }
                void show() override {}
                void close() override {}
            private:
                void *reporter_;
                oakengine_plugin_reporter_destroy_fn destroy_;
                oakengine_plugin_reporter_set_progress_fn set_progress_;
                void *userdata_;
            };
            return new CAdapter(reporter, g_reporter_destroy,
                                g_reporter_is_cancelled,
                                g_reporter_set_progress,
                                g_reporter_userdata);
        });
    return OAKENGINE_OK;
}

int oakengine_plugin_load_plugins(const char *path)
{
    if (!path) {
        return OAKENGINE_E_INVALID;
    }
    olive::plugin::load_plugins(QString::fromUtf8(path));
    return OAKENGINE_OK;
}

int oakengine_plugin_node_push_button_clicked(OakEngineNode *node,
                                               const char *button_id)
{
    if (!node || !button_id) {
        return OAKENGINE_E_INVALID;
    }
    auto *pn = dynamic_cast<olive::plugin::PluginNode *>(
        reinterpret_cast<olive::Node *>(node));
    if (!pn) {
        return OAKENGINE_E_INVALID;
    }
    pn->push_button_clicked(QString::fromUtf8(button_id));
    return OAKENGINE_OK;
}

} // extern "C"
