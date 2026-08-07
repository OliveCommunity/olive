/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#ifndef OAK_CORE_H
#define OAK_CORE_H

#include <QObject>
#include <olive/core/core.h>
#include "common/tooltypes.h"
#include "oakutil/qtutils.h"
#include "oakengine/app.h"
#include "oakengine/node.h"
#include "oakengine/project.h"
#include "oakengine/timeline.h"
#include "oakengine/undo.h"
#include "oakengine/init.h"
#include "oakengine/task.h"

#include "asyncengineevents.h"

namespace olive
{

using namespace core;

class MainWindow;

/**
 * @brief The main central Olive application instance_
 *
 * This is the UI-facing application controller. It holds an EngineCore
 * member for UI-independent engine state and adds the main window, dialogs
 * and other user interaction on top of it.
 *
 * EngineCore is NOT a base class — it is a member, so the MOC-generated
 * code for Core does not pull in EngineCore's Q_OBJECT symbols.
 *
 * The "public slots" are usually user-triggered actions and can be connected to UI elements (e.g. creating a folder,
 * opening the import dialog, etc.)
 */
class Core : public QObject {
	Q_OBJECT
public:
	/**
	 * @brief Core Constructor
	 *
	 * Creates the EngineCore engine instance and registers the UI handlers
	 * that the engine uses to request user interaction.
	 */
	Core(const OakEngineAppParams *params = nullptr);

	~Core();

	/**
	 * @brief The single async event dispatcher (issue 0b).
	 */
	static AsyncEngineEvents *async_events()
	{
		return AsyncEngineEvents::instance();
	}

	/**
	 * @brief Core object accessible from anywhere in the code
	 *
	 * Returns the application Core singleton (no EngineCore::instance() call).
	 */
	static Core *instance()
	{
		return instance_;
	}

	/**
	 * @brief Start Olive Core
	 *
	 * Main application launcher. Starts the engine first, then the GUI (if entering a GUI mode).
	 */
	void start();

	/**
	 * @brief Stop Olive Core
	 *
	 * Tears down the UI services first, then the engine, ready for the application to exit.
	 */
	void stop();

	/**
	 * @brief Retrieve main window instance_
	 *
	 * @return
	 *
	 * Pointer to the olive::MainWindow object, or nullptr if running in CLI mode.
	 */
	MainWindow *main_window();

	/**
	 * @brief Import a list of files
	 *
	 * FIXME: I kind of hate this, it needs a model to update correctly. Is there a way that Items can signal enough to
	 *        make passing references to the model unnecessary?
	 *
	 * @param urls
	 */
	void import_files(const QStringList &urls, OakEngineNode *parent);

	/**
	 * @brief Get the currently active project
	 *
	 * Uses the UI/Panel system to determine which Project was the last focused on and assumes this is the active Project
	 * that the user wishes to work on.
	 *
	 * @return
	 *
	 * The active Project file, or nullptr if the heuristic couldn't find one.
	 */
	OakEngineProject *get_active_project() const;
	OakEngineNode *get_selected_folder_in_active_project() const;

	/**
	 * @brief Show a dialog to the user to rename a set of nodes
	 */
	bool label_nodes(const QVector<OakEngineNode *> &nodes,
					void *parent = nullptr);

	/**
	 * @brief Opens a project from the recently opened list
	 */
	void open_project_from_recent_list(int index);

	/**
	 * @brief Closes a project
	 */
	bool close_project(bool auto_open_new, bool ignore_modified = false);

	/**
	 * @brief Runs a modal cache task on the currently active sequence
	 */
	void cache_active_sequence(bool in_out_only);

	void open_recovery_project(const QString &filename);

	void open_node_in_viewer(OakEngineNode *viewer);

	void open_export_dialog_for_viewer(OakEngineNode *viewer,
								   bool start_still_image);

	bool add_open_project_from_task(OakEngineTask *task, bool add_to_recents);
	bool add_recovery_project_from_task(OakEngineTask *task);

public slots:
	/**
	 * @brief Starts an open file dialog to load a project from file
	 */
	void open_project();

	/**
	 * @brief Saves the current project
	 */
	bool save_project();

	/**
	 * @brief Performs a "save as" on the current project
	 */
	bool save_project_as();

	void revert_project();

	/**
	 * @brief Show an About dialog
	 */
	void dialog_about_show();

	/**
	 * @brief Open the import footage dialog and import the files selected (runs ImportFiles())
	 */
	void dialog_import_show();

	/**
	 * @brief Show Preferences dialog
	 */
	void dialog_preferences_show(int start_tab = 0);

	/**
	 * @brief Show Project Properties dialog
	 */
	void dialog_project_properties_show();

	/**
	 * @brief Show Export dialog
	 */
	void dialog_export_show();

	/**
	 * @brief Create a new folder in the currently active project
	 */
	void create_new_folder();

	/**
	 * @brief Create a new sequence in the currently active project
	 */
	void create_new_sequence();

	void check_for_auto_recoveries();

	void browse_auto_recoveries();

public:
	// The following methods are ordinary member functions, NOT slots. They are
	// deliberately kept out of the `public slots:` section; none of them are
	// connect() targets: every connection involving Core uses the new-style
	// member-function syntax, which works with plain methods.

	/**
	 * @brief Show OTIO import dialog
	 */
#ifdef USE_OTIO
	bool DialogImportOTIOShow(const QList<OakEngineSequence *> &sequences);
#endif

	// ---- Facade-wrapping methods (shadow EngineCore to avoid symbol refs) ----

	Tool::Item tool() const;
	void set_tool(const Tool::Item &tool);

	bool snapping() const;
	void set_snapping(const bool &b);

	Timecode::Display get_timecode_display() const;
	void set_timecode_display(Timecode::Display d);

	void show_status_bar_message(const QString &s, int timeout = 0);
	void clear_status_bar_message();

	static QString footage_file_dialog_filter();
	static bool is_footage_extension_allowed(const QString &path);

	void create_new_project();
	OakEngineSequence *create_new_sequence_for_project(const QString &format,
													 OakEngineProject *project);
	static OakEngineSequence *create_new_sequence_for_project(OakEngineProject *project);

	void clear_open_recent_list();
	void set_use_proxy_media(bool enabled);

	void request_pixel_sampling_in_viewers(bool e);

	Tool::AddableObject get_selected_addable_object() const;
	void set_selected_addable_object(const Tool::AddableObject &obj);
	void set_selected_transition_object(const QString &obj);

	static void copy_string_to_clipboard(const QString &s);

	void set_magic(bool e);

	// Recent project list accessors (replaces EngineCore::get_recent_projects())
	int get_recent_project_count() const;
	QString get_recent_project_at(int index) const;

	// Facade-wrapping methods (delegate through the C ABI)

	bool set_language(const QString &locale);
	void set_autorecovery_interval(int minutes);

	void on_project_saved(OakEngineProject *p);
	static QString get_auto_recovery_index_filename();
	void add_open_project(OakEngineProject *p, bool add_to_recents = false);
	void remove_recently_opened_project(int index);
	void set_active_project(OakEngineProject *p);
	QString get_selected_transition() const;

signals:
	// Forwarding signals (shadow EngineCore signals so connect() resolves here)
	void tool_changed(const Tool::Item &tool);
	void addable_object_changed(Tool::AddableObject o);
	void snapping_changed(const bool &b);
	void timecode_display_changed(Timecode::Display d);
	void open_recent_list_changed();
	void color_picker_enabled(bool e);
	void color_picker_color_emitted(const Color &reference, const Color &display);

	/**
	 * @brief App-internal re-broadcast of the engine undo-stack index change
	 * (issue 7 of the EventBridge elimination plan). Widgets connect to this
	 * instead of raw oakengine_event_subscribe callbacks on
	 * OAKENGINE_EVENT_UNDO_INDEX_CHANGED. The argument is the new stack index.
	 */
	void undo_index_changed(int index);

	/**
	 * @brief App-internal re-broadcast of the active project's modified-flag
	 * change (issue 8 of the EventBridge elimination plan). The main window
	 * drives setWindowModified from this instead of a raw
	 * oakengine_event_subscribe callback on
	 * OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED.
	 */
	void project_modified_changed(bool modified);

private:
	/**
	 * @brief Get the file filter than can be used with QFileDialog to open and save compatible projects
	 */
	static QString get_project_filter(bool include_any_filter);

	/**
	 * @brief Start GUI portion of Olive
	 *
	 * Starts services and objects required for the GUI of Olive. It's guaranteed that running without this function will
	 * create an application instance_ that is completely valid minus the UI (e.g. for CLI modes).
	 */
	void start_gui(bool full_screen);

	/**
	 * @brief Internal function for saving a project to a file
	 */
	void save_project_internal(const QString &override_filename = QString());

	/**
	 * @brief Retrieves the currently most active sequence for exporting
	 */
	OakEngineNode *get_sequence_to_export();

	bool revert_project_internal(bool by_opening_existing);

	/**
	 * @brief Shows the "disk cache full" warning (connected to EngineCore::cache_full_warning_requested)
	 */
	void show_cache_full_warning();

	/**
	 * @brief Applies a new active project to the main window (connected to EngineCore::active_project_changed)
	 */
	void on_active_project_changed(OakEngineProject *p);

	/**
	 * @brief Internal main window object
	 */
	MainWindow *main_window_;

	/**
	 * @brief Cached Core* singleton
	 */
	static Core *instance_;

private slots:
	void project_save_succeeded(OakEngineTask *task);

	bool add_open_project_from_task_and_add_to_recents(OakEngineTask *task)
	{
		return instance()->add_open_project_from_task(task, true);
	}

	void import_task_complete(OakEngineTask *task);

	bool confirm_image_sequence(const QString &filename);

	bool start_headless_export();

	void open_startup_project();

	/**
	 * @brief Internal project open
	 */
	void open_project_internal(const QString &filename,
							 bool recovery_project = false);

	void import_single_file(const QString &f);
};

}

#endif // OAK_CORE_H
