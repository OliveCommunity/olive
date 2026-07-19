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

#include <olive/core/core.h>
#include <QFileInfoList>
#include <QList>
#include <QTimer>
#include <QTranslator>

#include "node/project/footage/footage.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "task/task.h"
#include "tool/tool.h"
#include "undo/undostack.h"

namespace olive
{

class MainWindow;

/**
 * @brief The main central Olive application instance_
 *
 * This runs both in GUI and CLI modes (and handles what to init based on that).
 * It also contains various global functions/variables for use throughout Olive.
 *
 * The "public slots" are usually user-triggered actions and can be connected to UI elements (e.g. creating a folder,
 * opening the import dialog, etc.)
 */
class Core : public QObject {
	Q_OBJECT
public:
	class CoreParams {
	public:
		CoreParams();

		enum RunMode { k_run_normal, k_headless_export, k_headless_pre_cache };

		bool fullscreen() const
		{
			return run_fullscreen_;
		}

		void set_fullscreen(bool e)
		{
			run_fullscreen_ = e;
		}

		RunMode run_mode() const
		{
			return mode_;
		}

		void set_run_mode(RunMode m)
		{
			mode_ = m;
		}

		const QString startup_project() const
		{
			return startup_project_;
		}

		void set_startup_project(const QString &p)
		{
			startup_project_ = p;
		}

		const QString &startup_language() const
		{
			return startup_language_;
		}

		void set_startup_language(const QString &s)
		{
			startup_language_ = s;
		}

		bool crash_on_startup() const
		{
			return crash_;
		}

		void set_crash_on_startup(bool e)
		{
			crash_ = true;
		}

	private:
		RunMode mode_;

		QString startup_project_;

		QString startup_language_;

		bool run_fullscreen_;

		bool crash_;
	};

	/**
   * @brief Core Constructor
   *
   * Currently empty
   */
	Core(const CoreParams &params);

	/**
   * @brief Core object accessible from anywhere in the code
   *
   * Use this to access Core functions.
   */
	static Core *instance();

	static QString footage_file_dialog_filter();
	static QStringList allowed_footage_extensions();
	static bool is_footage_extension_allowed(const QString &path);

	const CoreParams &core_params() const
	{
		return core_params_;
	}

	/**
   * @brief Start Olive Core
   *
   * Main application launcher. Parses command line arguments and constructs main window (if entering a GUI mode).
   */
	void start();

	/**
   * @brief Stop Olive Core
   *
   * Ends all threads and frees all memory ready for the application to exit.
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
   * @brief Retrieve UndoStack object
   */
	UndoStack *undo_stack();

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
   * @brief Get the currently active tool
   */
	const Tool::Item &tool() const;

	/**
   * @brief Get the currently selected object that the add tool should make (if the add tool is active)
   */
	const Tool::AddableObject &get_selected_addable_object() const;

	/**
   * @brief Get the currently selected node that the transition tool should make (if the transition tool is active)
   */
	const QString &get_selected_transition() const;

	/**
   * @brief Get current snapping value
   */
	const bool &snapping() const;

	/**
   * @brief Returns a list of the most recently opened/saved projects
   */
	const QStringList &get_recent_projects() const;

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
   * @brief Gets current timecode display mode
   */
	Timecode::Display get_timecode_display() const;

	/**
   * @brief Sets current timecode display mode
   */
	void set_timecode_display(Timecode::Display d);

	/**
   * @brief Set how frequently an autorecovery should be saved (if the project has changed, see SetProjectModified())
   */
	void set_autorecovery_interval(int minutes);

	static void copy_string_to_clipboard(const QString &s);

	static QString paste_string_from_clipboard();

	/**
   * @brief Recursively count files in a file/directory list
   */
	static int count_files_in_file_list(const QFileInfoList &filenames);

	/**
   * @brief Show a dialog to the user to rename a set of nodes
   */
	bool label_nodes(const QVector<Node *> &nodes,
					MultiUndoCommand *parent = nullptr);

	/**
   * @brief Create a new sequence named appropriately for the active project
   */
	static Sequence *create_new_sequence_for_project(const QString &format,
												 Project *project);
	static Sequence *create_new_sequence_for_project(Project *project)
	{
		return create_new_sequence_for_project(tr("Sequence %1"), project);
	}

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

	/**
   * @brief Check each footage object for whether it still exists or has changed
   */
	bool validate_footage_in_loaded_project(Project *project,
										const QString &project_saved_url);

	/**
   * @brief Changes the current language
   */
	bool set_language(const QString &locale);

	/**
   * @brief Show message in main window's status bar
   *
   * Shorthand for Core::instance()->main_window()->statusBar()->showMessage();
   */
	void show_status_bar_message(const QString &s, int timeout = 0);

	void clear_status_bar_message();

	void open_recovery_project(const QString &filename);

	void open_node_in_viewer(ViewerOutput *viewer);

	void open_export_dialog_for_viewer(ViewerOutput *viewer,
								   bool start_still_image);

	bool is_magic_enabled() const
	{
		return magic_;
	}

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
   * @brief Set the current application-wide tool
   *
   * @param tool
   */
	void set_tool(const Tool::Item &tool);

	/**
   * @brief Set the current snapping setting
   */
	void set_snapping(const bool &b);

	/**
   * @brief Globally enable or disable decoding from proxy media
   *
   * When disabled, all footage decodes from its original media regardless of
   * each footage's individual proxy setting. The per-footage settings are
   * preserved and take effect again when this is re-enabled.
   */
	void set_use_proxy_media(bool enabled);

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

	/**
   * @brief Set the currently selected object that the add tool should make
   */
	void set_selected_addable_object(const Tool::AddableObject &obj);

	/**
   * @brief Set the currently selected object that the add tool should make
   */
	void set_selected_transition_object(const QString &obj);

	/**
   * @brief Clears the list of recently opened/saved projects
   */
	void clear_open_recent_list();

	/**
   * @brief Creates a new empty project and opens it
   */
	void create_new_project();

	void check_for_auto_recoveries();

	void browse_auto_recoveries();

	void request_pixel_sampling_in_viewers(bool e);

	void warn_cache_full();

	void set_magic(bool e)
	{
		magic_ = e;
	}

signals:
	/**
   * @brief Signal emitted when the tool is changed from somewhere
   */
	void tool_changed(const Tool::Item &tool);

	/**
   * @brief Signal emitted when addable object changes through SetSelectedAddableObject
   */
	void addable_object_changed(Tool::AddableObject o);

	/**
   * @brief Signal emitted when the snapping setting is changed
   */
	void snapping_changed(const bool &b);

	/**
   * @brief Signal emitted when the default timecode display mode changed
   */
	void timecode_display_changed(Timecode::Display d);

	/**
   * @brief Signal emitted when a change is made to the open recent list
   */
	void open_recent_list_changed();

	/**
   * @brief Enable mouse color sampling functionality on all viewers
   *
   * This can be slow, so we only turn it on when we need it.
   */
	void color_picker_enabled(bool e);

	/**
   * @brief A viewer with color picked enabled has emitted a color
   */
	void color_picker_color_emitted(const Color &reference, const Color &display);

private:
	/**
   * @brief Get the file filter than can be used with QFileDialog to open and save compatible projects
   */
	static QString get_project_filter(bool include_any_filter);

	/**
   * @brief Returns the filename where the recently opened/saved projects should be stored
   */
	static QString get_recent_projects_file_path();

	/**
   * @brief Called only on startup to set the locale
   */
	void set_startup_locale();

	/**
   * @brief Adds a filename to the top of the recently opened projects list (or moves it if it already exists)
   */
	void push_recently_opened_project(const QString &s);

	/**
   * @brief Declare custom types/classes for Qt's signal/slot system
   *
   * Qt's signal/slot system requires types to be declared. In the interest of doing this only at startup, we contain
   * them all in a function here.
   */
	void declare_types_for_qt();

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

	static QString get_auto_recovery_index_filename();

	void save_unrecovered_list();

	bool revert_project_internal(bool by_opening_existing);

	void save_recent_projects_list();

	/**
   * @brief Adds a project to the "open projects" list
   */
	void add_open_project(olive::Project *p, bool add_to_recents = false);

	bool add_open_project_from_task(Task *task, bool add_to_recents);

	void set_active_project(Project *p);

	/**
   * @brief Internal main window object
   */
	MainWindow *main_window_;

	/**
   * @brief List of currently open projects
   */
	Project *open_project_;

	/**
   * @brief Currently active tool
   */
	Tool::Item tool_;

	/**
   * @brief Currently active addable object
   */
	Tool::AddableObject addable_object_;

	/**
   * @brief Currently selected transition
   */
	QString selected_transition_;

	/**
   * @brief Current snapping setting
   */
	bool snapping_;

	/**
   * @brief Internal timer for saving autorecovery files
   */
	QTimer autorecovery_timer_;

	/**
   * @brief Application-wide undo stack instance_
   */
	UndoStack undo_stack_;

	/**
   * @brief List of most recently opened/saved projects
   */
	QStringList recent_projects_;

	/**
   * @brief Parameters set up in main() determining how the program should run
   */
	CoreParams core_params_;

	/**
   * @brief Static singleton core instance_
   */
	static Core *instance_;

	/**
   * @brief Internal translator
   */
	QTranslator *translator_;

	/**
   * @brief List of projects that are unsaved but have autorecovery projects
   */
	QVector<QUuid> autorecovered_projects_;

	/**
   * @brief Do something debug related
   */
	bool magic_;

	/**
   * @brief How many widgets currently need pixel sampling access
   */
	int pixel_sampling_users_;

	bool shown_cache_full_warning_;

private slots:
	void save_autorecovery();

	void project_save_succeeded(Task *task);

	bool add_open_project_from_task_and_add_to_recents(Task *task)
	{
		return add_open_project_from_task(task, true);
	}

	void import_task_complete(Task *task);

	bool confirm_image_sequence(const QString &filename);

	void project_was_modified(bool e);

	bool start_headless_export();

	void open_startup_project();

	void add_recovery_project_from_task(Task *task);

	/**
   * @brief Internal project open
   */
	void open_project_internal(const QString &filename,
							 bool recovery_project = false);

	void import_single_file(const QString &f);
};

}

#endif // OAK_CORE_H
