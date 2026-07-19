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

#include "coreengine.h"

namespace olive
{

class MainWindow;

/**
 * @brief The main central Olive application instance_
 *
 * This is the UI-facing derivation of EngineCore. It runs both in GUI and
 * CLI modes (and handles what to init based on that). All UI-independent
 * engine state lives in the base class EngineCore; this class adds the main
 * window, dialogs and other user interaction on top of it.
 *
 * The "public slots" are usually user-triggered actions and can be connected to UI elements (e.g. creating a folder,
 * opening the import dialog, etc.)
 */
class Core : public EngineCore {
	Q_OBJECT
public:
	/**
	 * @brief Core Constructor
	 *
	 * Registers the UI handlers that EngineCore uses to request user
	 * interaction.
	 */
	Core(const CoreParams &params);

	/**
	 * @brief Core object accessible from anywhere in the code
	 *
	 * Use this to access Core functions. This is simply EngineCore::instance()
	 * cast to Core, which is safe because the application entry point (main())
	 * always constructs a Core.
	 */
	static Core *instance()
	{
		return static_cast<Core *>(EngineCore::instance());
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
	void import_files(const QStringList &urls, Folder *parent);

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
	Project *get_active_project() const;
	Folder *get_selected_folder_in_active_project() const;

	/**
	 * @brief Show a dialog to the user to rename a set of nodes
	 */
	bool label_nodes(const QVector<Node *> &nodes,
					MultiUndoCommand *parent = nullptr);

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

	void open_node_in_viewer(ViewerOutput *viewer);

	void open_export_dialog_for_viewer(ViewerOutput *viewer,
								   bool start_still_image);

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
	 * @brief Show OTIO import dialog
	 */
#ifdef USE_OTIO
	bool DialogImportOTIOShow(const QList<Sequence *> &sequences);
#endif

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
	ViewerOutput *get_sequence_to_export();

	bool revert_project_internal(bool by_opening_existing);

	/**
	 * @brief Shows the "disk cache full" warning (connected to EngineCore::cache_full_warning_requested)
	 */
	void show_cache_full_warning();

	/**
	 * @brief Applies a new active project to the main window (connected to EngineCore::active_project_changed)
	 */
	void on_active_project_changed(Project *p);

	/**
	 * @brief Internal main window object
	 */
	MainWindow *main_window_;

private slots:
	void project_save_succeeded(Task *task);

	bool add_open_project_from_task_and_add_to_recents(Task *task)
	{
		return add_open_project_from_task(task, true);
	}

	void import_task_complete(Task *task);

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
