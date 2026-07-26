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

#ifndef OAK_COREENGINE_H
#define OAK_COREENGINE_H

#include <olive/core/core.h>
#include <QFileInfoList>
#include <QList>
#include <QTimer>
#include <QTranslator>

#include <functional>

#include "node/project/footage/footage.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializedlayoutinfo.h"
#include "task/task.h"
#include "tool/tool.h"
#include "undo/undostack.h"

namespace olive
{

/**
 * @brief The UI-independent engine core of the Olive application
 *
 * EngineCore holds the global application state that does not depend on the
 * UI (no widgets, dialogs, panels or windows) and can therefore run headless
 * (e.g. in the render worker or in tests). The UI layer (olive::Core)
 * derives from this class and registers handlers / connects signals for
 * everything that requires user interaction (see the std::function handlers
 * below, modelled after Config::ErrorHandler).
 *
 * This class must only include engine-side headers (olive/core, node/, undo/,
 * task/, timeline/, codec/, common/, config/, tool/, audio/, render/). It must
 * never include widget/, dialog/, panel/, window/ or ui/ headers.
 */
class EngineCore : public QObject {
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
	 * @brief EngineCore Constructor
	 */
	EngineCore(const CoreParams &params);

	/**
	 * @brief EngineCore object accessible from anywhere in the code
	 *
	 * Use this to access engine functions. This assumes the object is only
	 * ever constructed once at the application entry point.
	 */
	static EngineCore *instance();

	static QString footage_file_dialog_filter();
	static QStringList allowed_footage_extensions();
	static bool is_footage_extension_allowed(const QString &path);

	const CoreParams &core_params() const
	{
		return core_params_;
	}

	/**
	 * @brief Start the engine
	 *
	 * Initializes everything that is independent of the UI: config, locale,
	 * meta types, and the global engine managers (NodeFactory, ColorManager,
	 * TaskManager, ConformManager, ProxyManager, RenderManager, FrameManager,
	 * ProjectSerializer). Also loads the recent projects list and starts the
	 * autorecovery timer.
	 */
	void start();

	/**
	 * @brief Stop the engine
	 *
	 * Frees the global engine managers and saves the config. In a UI build,
	 * call this after the UI services have been torn down.
	 */
	void stop();

	/**
	 * @brief Retrieve UndoStack object
	 */
	UndoStack *undo_stack();

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
	 * @brief Create a new sequence named appropriately for the given project
	 */
	static Sequence *create_new_sequence_for_project(const QString &format,
													 Project *project);
	static Sequence *create_new_sequence_for_project(Project *project)
	{
		return create_new_sequence_for_project(tr("Sequence %1"), project);
	}

	/**
	 * @brief Check each footage object for whether it still exists or has changed
	 *
	 * Missing footage is passed to the relink handler registered by the UI
	 * layer. Without a handler, the project is accepted as-is.
	 */
	bool validate_footage_in_loaded_project(Project *project,
										const QString &project_saved_url);

	/**
	 * @brief Changes the current language
	 */
	bool set_language(const QString &locale);

	/**
	 * @brief Show a message in the status bar
	 *
	 * The engine cannot show UI itself, so this only emits
	 * status_message_show(). The UI layer connects the signal to the main
	 * window's status bar.
	 */
	void show_status_bar_message(const QString &s, int timeout = 0);

	void clear_status_bar_message();

	bool is_magic_enabled() const
	{
		return magic_;
	}

	/**
	 * @brief Handler confirming that a file should be imported as an image sequence
	 *
	 * Registered by the UI layer (e.g. a QMessageBox-based prompt). Without
	 * a handler, the import is accepted.
	 */
	using ConfirmImageSequenceHandler =
		std::function<bool(const QString &filename)>;
	void set_confirm_image_sequence_handler(ConfirmImageSequenceHandler handler);

	/**
	 * @brief Handler offering to relink footage that could not be validated
	 *
	 * Registered by the UI layer (e.g. a FootageRelinkDialog). Should return
	 * false to reject the project. Without a handler, the project is accepted.
	 */
	using FootageRelinkHandler = std::function<bool(QVector<Footage *>)>;
	void set_relink_handler(FootageRelinkHandler handler);

	/**
	 * @brief Handler performing the actual project save for autorecovery
	 *
	 * Registered by the UI layer, since saving involves UI state (the main
	 * window layout). Without a handler, the actual file write is skipped.
	 */
	using SaveProjectHandler =
		std::function<void(const QString &override_filename)>;
	void set_save_project_handler(SaveProjectHandler handler);

	/**
	 * @brief Handler closing the currently open project
	 *
	 * Registered by the UI layer, which may prompt the user to save first.
	 * Should return false if the close was cancelled. Without a handler, the
	 * project is closed silently.
	 */
	using CloseProjectHandler = std::function<bool()>;
	void set_close_project_handler(CloseProjectHandler handler);

	/**
	 * @brief Handler applying a loaded main window layout after a project load
	 */
	using LoadLayoutHandler =
		std::function<void(const SerializedLayoutInfo &layout)>;
	void set_load_layout_handler(LoadLayoutHandler handler);

	/**
	 * @brief Update engine state after a project was successfully saved
	 *
	 * Pushes the project to the top of the recent list, clears its modified
	 * flag and removes it from the unrecovered list.
	 */
	void on_project_saved(Project *p);

	/**
	 * @brief Removes a project from the recently opened list (e.g. if it no longer exists)
	 */
	void remove_recently_opened_project(int index);

	/**
	 * @brief Currently open project (may be nullptr)
	 *
	 * Read accessor for the C ABI facade (oakengine_app_open_project());
	 * the UI layer used to read the protected member directly.
	 */
	Project *open_project() const
	{
		return open_project_;
	}

#ifdef USE_OTIO
	/**
	 * @brief Handler showing the OTIO import options dialog
	 *
	 * Registered by the UI layer. Without a handler, the import is accepted.
	 */
	using OtioImportHandler =
		std::function<bool(const QList<Sequence *> &sequences)>;
	void set_otio_import_handler(OtioImportHandler handler);
#endif

public slots:
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
	 *
	 * The currently open project is closed through the close handler, so the
	 * UI layer may prompt to save first.
	 */
	void create_new_project();

	void request_pixel_sampling_in_viewers(bool e);

	/**
	 * @brief Warn that the disk cache is full (at most once)
	 *
	 * Only emits cache_full_warning_requested(); the UI layer shows the
	 * actual dialog.
	 */
	void warn_cache_full();

	void set_magic(bool e)
	{
		magic_ = e;
	}

	/**
	 * @brief Invokable forwarder for the image sequence confirmation
	 *
	 * Import tasks run on worker threads and call this slot through
	 * QMetaObject::invokeMethod with Qt::BlockingQueuedConnection. It runs
	 * on the main thread and forwards to the registered handler; without a
	 * handler, the import is accepted.
	 */
	bool confirm_image_sequence(const QString &filename);

#ifdef USE_OTIO
	/**
	 * @brief Invokable forwarder for the OTIO import dialog
	 *
	 * Same threading pattern as confirm_image_sequence().
	 */
	bool show_otio_import_dialog(const QList<Sequence *> &sequences);
#endif

	void add_recovery_project_from_task(Task *task);

public:
	/**
	 * @brief Adds a project to the "open projects" list
	 *
	 * (Public for the C ABI facade; was protected while olive::Core derived
	 * from this class.)
	 */
	void add_open_project(olive::Project *p, bool add_to_recents = false);

	bool add_open_project_from_task(Task *task, bool add_to_recents);

	void set_active_project(Project *p);

	/**
	 * @brief Returns the filename of the autorecovery index
	 */
	static QString get_auto_recovery_index_filename();

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

	/**
	 * @brief Request showing a message in the main window's status bar
	 */
	void status_message_show(const QString &message, int timeout);

	/**
	 * @brief Request clearing the main window's status bar
	 */
	void status_message_clear();

	/**
	 * @brief Request showing the "disk cache full" warning to the user
	 */
	void cache_full_warning_requested();

	/**
	 * @brief Signal emitted when the active (open) project changed
	 *
	 * The UI layer uses this to set the project on the main window.
	 */
	void active_project_changed(Project *p);

protected:
	/**
	 * @brief Currently open project
	 *
	 * Protected so the UI layer (olive::Core) can read it.
	 */
	Project *open_project_;

private:
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

	void save_unrecovered_list();

	void save_recent_projects_list();

	/**
	 * @brief Close the open project without any user prompt
	 *
	 * Fallback for the close handler when no UI layer is present.
	 */
	bool close_open_project_without_prompt();

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
	 * @brief Static singleton engine instance_
	 */
	static EngineCore *instance_;

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

	ConfirmImageSequenceHandler confirm_image_sequence_handler_;

	FootageRelinkHandler relink_handler_;

	SaveProjectHandler save_project_handler_;

	CloseProjectHandler close_project_handler_;

	LoadLayoutHandler load_layout_handler_;

#ifdef USE_OTIO
	OtioImportHandler otio_import_handler_;
#endif

private slots:
	void save_autorecovery();
};

}

#endif // OAK_COREENGINE_H
