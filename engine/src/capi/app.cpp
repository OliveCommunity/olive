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

#include "oakengine/app.h"

#include <cstdio>
#include <cstring>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QString>

#include "coreengine.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializedlayoutinfo.h"
#include "task/task.h"
#include "undo/undostack.h"

namespace
{

olive::Project *impl(OakEngineProject *h)
{
	return reinterpret_cast<olive::Project *>(h);
}

OakEngineProject *wrap(olive::Project *p)
{
	return reinterpret_cast<OakEngineProject *>(p);
}

OakEngineSequence *wrap_seq(olive::Sequence *s)
{
	return reinterpret_cast<OakEngineSequence *>(s);
}

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

// Registered callback set (all fields may be null).
OakEngineAppCallbacks g_callbacks = {};

// Whether oakengine_app_start() has run (and oakengine_app_stop() has not).
bool g_started = false;

// The EngineCore the notification signals are currently connected to.
olive::EngineCore *g_connected_core = nullptr;

olive::EngineCore *app_core()
{
	return olive::EngineCore::instance();
}

// The EngineCore constructor's UndoStack member creates QActions, which need
// QGuiApplication state (same reason as oakengine_init()).
void ensure_qcoreapplication()
{
	if (QCoreApplication::instance()) {
		return;
	}

	if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
		qputenv("QT_QPA_PLATFORM", "offscreen");
	}

	static int argc = 1;
	static char app_name[] = "oakengine";
	static char *argv[] = { app_name, nullptr };
	new QGuiApplication(argc, argv);

	QCoreApplication::setOrganizationName(QStringLiteral("oakvideoeditor.org"));
	QCoreApplication::setApplicationName(QStringLiteral("Oak Video Editor"));
}

// Forward engine signals to the registered C callbacks. Connected once per
// EngineCore instance; dropped events are fine while no callback is set.
void connect_notifications(olive::EngineCore *core)
{
	if (!core || g_connected_core == core) {
		return;
	}
	g_connected_core = core;

	QObject::connect(core, &olive::EngineCore::status_message_show, core,
					 [](const QString &message, int timeout) {
						 if (g_callbacks.status_message_show) {
							 g_callbacks.status_message_show(
								 message.toUtf8().constData(), timeout,
								 g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::status_message_clear, core,
					 [] {
						 if (g_callbacks.status_message_clear) {
							 g_callbacks.status_message_clear(
								 g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::cache_full_warning_requested,
					 core, [] {
						 if (g_callbacks.cache_full_warning) {
							 g_callbacks.cache_full_warning(
								 g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::active_project_changed, core,
					 [](olive::Project *p) {
						 if (g_callbacks.active_project_changed) {
							 g_callbacks.active_project_changed(
								 wrap(p), g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::tool_changed, core,
					 [](const olive::Tool::Item &tool) {
						 if (g_callbacks.tool_changed) {
							 g_callbacks.tool_changed(int(tool),
													  g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::addable_object_changed, core,
					 [](olive::Tool::AddableObject o) {
						 if (g_callbacks.addable_object_changed) {
							 g_callbacks.addable_object_changed(
								 int(o), g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::snapping_changed, core,
					 [](const bool &b) {
						 if (g_callbacks.snapping_changed) {
							 g_callbacks.snapping_changed(b ? 1 : 0,
														  g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::timecode_display_changed, core,
					 [](olive::core::Timecode::Display d) {
						 if (g_callbacks.timecode_display_changed) {
							 g_callbacks.timecode_display_changed(
								 int(d), g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::open_recent_list_changed, core,
					 [] {
						 if (g_callbacks.open_recent_list_changed) {
							 g_callbacks.open_recent_list_changed(
								 g_callbacks.userdata);
						 }
					 });
	QObject::connect(core, &olive::EngineCore::color_picker_enabled, core,
					 [](bool e) {
						 if (g_callbacks.color_picker_enabled) {
							 g_callbacks.color_picker_enabled(
								 e ? 1 : 0, g_callbacks.userdata);
						 }
					 });
}

// Translate the C handler callbacks into the std::function handlers
// EngineCore calls when it needs user interaction.
void install_handlers(olive::EngineCore *core)
{
	if (g_callbacks.confirm_image_sequence) {
		core->set_confirm_image_sequence_handler([](const QString &filename) {
			return g_callbacks.confirm_image_sequence(
					   filename.toUtf8().constData(),
					   g_callbacks.userdata) != 0;
		});
	} else {
		core->set_confirm_image_sequence_handler(nullptr);
	}

	if (g_callbacks.relink_footage) {
		core->set_relink_handler([](QVector<olive::Footage *> footage) {
			return g_callbacks.relink_footage(
					   reinterpret_cast<OakEngineFootage **>(footage.data()),
					   int(footage.size()), g_callbacks.userdata) != 0;
		});
	} else {
		core->set_relink_handler(nullptr);
	}

	if (g_callbacks.save_project) {
		core->set_save_project_handler([](const QString &override_filename) {
			g_callbacks.save_project(override_filename.toUtf8().constData(),
									 g_callbacks.userdata);
		});
	} else {
		core->set_save_project_handler(nullptr);
	}

	if (g_callbacks.close_project) {
		core->set_close_project_handler([] {
			return g_callbacks.close_project(g_callbacks.userdata) != 0;
		});
	} else {
		core->set_close_project_handler(nullptr);
	}

	if (g_callbacks.load_layout) {
		core->set_load_layout_handler(
			[](const olive::SerializedLayoutInfo &layout) {
				g_callbacks.load_layout(&layout, g_callbacks.userdata);
			});
	} else {
		core->set_load_layout_handler(nullptr);
	}

#ifdef USE_OTIO
	if (g_callbacks.otio_import) {
		core->set_otio_import_handler(
			[](const QList<olive::Sequence *> &sequences) {
				QVector<OakEngineSequence *> handles;
				handles.reserve(sequences.size());
				for (olive::Sequence *s : sequences) {
					handles.append(wrap_seq(s));
				}
				return g_callbacks.otio_import(handles.data(),
											   int(handles.size()),
											   g_callbacks.userdata) != 0;
			});
	} else {
		core->set_otio_import_handler(nullptr);
	}
#endif
}

} // namespace

extern "C"
{

int oakengine_app_create(const OakEngineAppParams *params)
{
	if (app_core()) {
		return OAKENGINE_E_STATE;
	}

	ensure_qcoreapplication();

	olive::EngineCore::CoreParams core_params;
	if (params) {
		switch (params->run_mode) {
		case OAKENGINE_APP_RUN_HEADLESS_EXPORT:
			core_params.set_run_mode(
				olive::EngineCore::CoreParams::k_headless_export);
			break;
		case OAKENGINE_APP_RUN_HEADLESS_PRE_CACHE:
			core_params.set_run_mode(
				olive::EngineCore::CoreParams::k_headless_pre_cache);
			break;
		default:
			core_params.set_run_mode(
				olive::EngineCore::CoreParams::k_run_normal);
			break;
		}
		core_params.set_fullscreen(params->fullscreen != 0);
		if (params->startup_project) {
			core_params.set_startup_project(
				QString::fromUtf8(params->startup_project));
		}
		if (params->startup_language) {
			core_params.set_startup_language(
				QString::fromUtf8(params->startup_language));
		}
		if (params->crash_on_startup) {
			core_params.set_crash_on_startup(true);
		}
	}

	// Never deleted: backs the process-wide EngineCore singleton (same
	// lifetime rule as the oakengine_init() shell).
	new olive::EngineCore(core_params);

	return OAKENGINE_OK;
}

int oakengine_app_start(void)
{
	if (!app_core() || g_started) {
		return OAKENGINE_E_STATE;
	}

	app_core()->start();
	g_started = true;
	return OAKENGINE_OK;
}

int oakengine_app_stop(void)
{
	if (!app_core() || !g_started) {
		return OAKENGINE_E_STATE;
	}

	app_core()->stop();
	g_started = false;
	return OAKENGINE_OK;
}

int oakengine_app_set_callbacks(const OakEngineAppCallbacks *callbacks)
{
	if (callbacks) {
		g_callbacks = *callbacks;
	} else {
		g_callbacks = OakEngineAppCallbacks{};
	}

	if (olive::EngineCore *core = app_core()) {
		connect_notifications(core);
		install_handlers(core);
	}

	return OAKENGINE_OK;
}

int oakengine_app_run_mode(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	switch (app_core()->core_params().run_mode()) {
	case olive::EngineCore::CoreParams::k_headless_export:
		return OAKENGINE_APP_RUN_HEADLESS_EXPORT;
	case olive::EngineCore::CoreParams::k_headless_pre_cache:
		return OAKENGINE_APP_RUN_HEADLESS_PRE_CACHE;
	default:
		return OAKENGINE_APP_RUN_NORMAL;
	}
}

int oakengine_app_fullscreen(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return app_core()->core_params().fullscreen() ? 1 : 0;
}

int oakengine_app_startup_project(char *buf, int buf_size)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(app_core()->core_params().startup_project(), buf,
						 buf_size);
}

void *oakengine_app_undo_stack(void)
{
	if (!app_core()) {
		return nullptr;
	}
	return app_core()->undo_stack();
}

int oakengine_app_tool(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return int(app_core()->tool());
}

int oakengine_app_set_tool(int tool)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	if (tool < 0 || tool >= int(olive::Tool::k_count)) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_tool(static_cast<olive::Tool::Item>(tool));
	return OAKENGINE_OK;
}

int oakengine_app_addable_object(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return int(app_core()->get_selected_addable_object());
}

int oakengine_app_set_addable_object(int object)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	if (object < 0 || object >= int(olive::Tool::k_addable_count)) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_selected_addable_object(
		static_cast<olive::Tool::AddableObject>(object));
	return OAKENGINE_OK;
}

int oakengine_app_selected_transition(char *buf, int buf_size)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(app_core()->get_selected_transition(), buf, buf_size);
}

int oakengine_app_set_selected_transition(const char *id)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_selected_transition_object(
		id ? QString::fromUtf8(id) : QString());
	return OAKENGINE_OK;
}

int oakengine_app_snapping(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return app_core()->snapping() ? 1 : 0;
}

int oakengine_app_set_snapping(int enabled)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_snapping(enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_app_timecode_display(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return int(app_core()->get_timecode_display());
}

int oakengine_app_set_timecode_display(int display)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	if (display < 0 ||
		display > int(olive::core::Timecode::k_milliseconds)) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_timecode_display(
		static_cast<olive::core::Timecode::Display>(display));
	return OAKENGINE_OK;
}

int oakengine_app_recent_projects_count(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return int(app_core()->get_recent_projects().size());
}

int oakengine_app_recent_project_at(int index, char *buf, int buf_size)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	const QStringList &recent = app_core()->get_recent_projects();
	if (index < 0 || index >= recent.size()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(recent.at(index), buf, buf_size);
}

int oakengine_app_remove_recent_project(int index)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	if (index < 0 || index >= app_core()->get_recent_projects().size()) {
		return OAKENGINE_E_NOT_FOUND;
	}

	app_core()->remove_recently_opened_project(index);
	return OAKENGINE_OK;
}

int oakengine_app_clear_recent_projects(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->clear_open_recent_list();
	return OAKENGINE_OK;
}

int oakengine_app_show_status_message(const char *message, int timeout)
{
	if (!app_core() || !message) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->show_status_bar_message(QString::fromUtf8(message), timeout);
	return OAKENGINE_OK;
}

int oakengine_app_clear_status_message(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->clear_status_bar_message();
	return OAKENGINE_OK;
}

int oakengine_app_set_language(const char *locale)
{
	if (!app_core() || !locale) {
		return OAKENGINE_E_INVALID;
	}

	return app_core()->set_language(QString::fromUtf8(locale)) ? 1 : 0;
}

int oakengine_app_set_autorecovery_interval(int minutes)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_autorecovery_interval(minutes);
	return OAKENGINE_OK;
}

int oakengine_app_set_use_proxy_media(int enabled)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_use_proxy_media(enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_app_request_pixel_sampling(int enable)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->request_pixel_sampling_in_viewers(enable != 0);
	return OAKENGINE_OK;
}

int oakengine_app_set_magic(int enabled)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_magic(enabled != 0);
	return OAKENGINE_OK;
}

int oakengine_app_is_magic_enabled(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return app_core()->is_magic_enabled() ? 1 : 0;
}

int oakengine_app_copy_to_clipboard(const char *text)
{
	if (!app_core() || !text) {
		return OAKENGINE_E_INVALID;
	}

	olive::EngineCore::copy_string_to_clipboard(QString::fromUtf8(text));
	return OAKENGINE_OK;
}

int oakengine_app_paste_from_clipboard(char *buf, int buf_size)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(olive::EngineCore::paste_string_from_clipboard(), buf,
						 buf_size);
}

int oakengine_app_footage_file_dialog_filter(char *buf, int buf_size)
{
	return string_to_buf(olive::EngineCore::footage_file_dialog_filter(), buf,
						 buf_size);
}

int oakengine_app_is_footage_extension_allowed(const char *path)
{
	if (!path) {
		return OAKENGINE_E_INVALID;
	}
	return olive::EngineCore::is_footage_extension_allowed(
			   QString::fromUtf8(path)) ?
		1 :
		0;
}

OakEngineSequence *oakengine_app_create_sequence(OakEngineProject *project,
												 const char *name_format)
{
	if (!app_core() || !project) {
		return nullptr;
	}

	const QString format = name_format ?
		QString::fromUtf8(name_format) :
		QStringLiteral("Sequence %1");
	return wrap_seq(olive::EngineCore::create_new_sequence_for_project(
		format, impl(project)));
}

int oakengine_app_auto_recovery_index_filename(char *buf, int buf_size)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(olive::EngineCore::get_auto_recovery_index_filename(),
						 buf, buf_size);
}

OakEngineProject *oakengine_app_open_project(void)
{
	if (!app_core()) {
		return nullptr;
	}
	return wrap(app_core()->open_project());
}

int oakengine_app_create_new_project(void)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->create_new_project();
	return OAKENGINE_OK;
}

int oakengine_app_add_open_project(OakEngineProject *project,
								   int add_to_recents)
{
	if (!app_core() || !project) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->add_open_project(impl(project), add_to_recents != 0);
	return OAKENGINE_OK;
}

int oakengine_app_add_open_project_from_task(void *task, int add_to_recents)
{
	if (!app_core() || !task) {
		return OAKENGINE_E_INVALID;
	}

	return app_core()->add_open_project_from_task(
			   static_cast<olive::Task *>(task), add_to_recents != 0) ?
		1 :
		0;
}

int oakengine_app_add_recovery_project_from_task(void *task)
{
	if (!app_core() || !task) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->add_recovery_project_from_task(static_cast<olive::Task *>(task));
	return OAKENGINE_OK;
}

int oakengine_app_on_project_saved(OakEngineProject *project)
{
	if (!app_core() || !project) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->on_project_saved(impl(project));
	return OAKENGINE_OK;
}

int oakengine_app_set_active_project(OakEngineProject *project)
{
	if (!app_core()) {
		return OAKENGINE_E_INVALID;
	}

	app_core()->set_active_project(impl(project));
	return OAKENGINE_OK;
}

// ---- Individual handler setter convenience wrappers ----

int oakengine_app_set_confirm_image_sequence_handler(
    int (*fn)(const char *filename, void *userdata), void *userdata)
{
	OakEngineAppCallbacks cb = g_callbacks;
	cb.userdata = userdata;
	cb.confirm_image_sequence = fn;
	return oakengine_app_set_callbacks(&cb);
}

int oakengine_app_set_relink_handler(
    int (*fn)(OakEngineFootage **footage, int count, void *userdata),
    void *userdata)
{
	OakEngineAppCallbacks cb = g_callbacks;
	cb.userdata = userdata;
	cb.relink_footage = fn;
	return oakengine_app_set_callbacks(&cb);
}

int oakengine_app_set_save_project_handler(
    void (*fn)(const char *override_filename, void *userdata), void *userdata)
{
	OakEngineAppCallbacks cb = g_callbacks;
	cb.userdata = userdata;
	cb.save_project = fn;
	return oakengine_app_set_callbacks(&cb);
}

int oakengine_app_set_close_project_handler(
    int (*fn)(void *userdata), void *userdata)
{
	OakEngineAppCallbacks cb = g_callbacks;
	cb.userdata = userdata;
	cb.close_project = fn;
	return oakengine_app_set_callbacks(&cb);
}

int oakengine_app_set_load_layout_handler(
    void (*fn)(const void *layout, void *userdata), void *userdata)
{
	OakEngineAppCallbacks cb = g_callbacks;
	cb.userdata = userdata;
	cb.load_layout = fn;
	return oakengine_app_set_callbacks(&cb);
}

// ---- void*-based convenience overloads ----

int oakengine_app_get_auto_recovery_index_filename(char *buf, int buf_size)
{
	return oakengine_app_auto_recovery_index_filename(buf, buf_size);
}

int oakengine_app_remove_recently_opened_project(int index)
{
	return oakengine_app_remove_recent_project(index);
}

int oakengine_app_on_project_saved_vp(void *project)
{
	return oakengine_app_on_project_saved(
		reinterpret_cast<OakEngineProject *>(project));
}

int oakengine_app_set_active_project_vp(void *project)
{
	return oakengine_app_set_active_project(
		reinterpret_cast<OakEngineProject *>(project));
}

int oakengine_app_add_open_project_vp(void *project, int add_to_recents)
{
	return oakengine_app_add_open_project(
		reinterpret_cast<OakEngineProject *>(project), add_to_recents);
}

} // extern "C"
