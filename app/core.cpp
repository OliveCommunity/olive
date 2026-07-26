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

#include "core.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>
#include "oakengine/audio.h"
#include "oakengine/disk.h"
#include "oakengine/plugin.h"
#include "oakengine/project.h"
#include "oakengine/task.h"
#include "oakengine/node.h"
#include "oakengine/undo.h"
#include "window/mainwindow/mainwindowundo.h"
#ifdef Q_OS_WINDOWS
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtPlatformHeaders/QWindowsWindowFunctions>
#endif
#endif

#include "dialog/task/task.h"
#include "common/filefunctions.h"
#include "common/xmlutils.h"
#include "common/configwrapper.h"
#include "dialog/about/about.h"
#include "dialog/autorecovery/autorecoverydialog.h"
#include "dialog/diskcache/diskcachedialog.h"
#include "dialog/export/export.h"
#include "dialog/footagerelink/footagerelinkdialog.h"

#include "dialog/otioproperties/otiopropertiesdialog.h"

#include "dialog/progress/pluginprogressdialogreporter.h"
#include "dialog/projectproperties/projectproperties.h"
#include "dialog/sequence/sequence.h"
#include "dialog/preferences/preferences.h"
#include "panel/panelmanager.h"
#include "panel/project/project.h"
#include "panel/timebased/timebased.h"
#include "panel/timeline/timeline.h"
#include "panel/viewer/viewer.h"
#include "pluginSupport/oliveplugininstance.h"
#include "pluginSupport/pluginprogressreporter.h"
#include "render/diskmanager.h"
#include "dialog/projectimport/projectimporterrordialog.h"
#include "ui/style/style.h"
#include "widget/menu/menushared.h"
#include "window/mainwindow/mainwindow.h"

#include "widget/viewer/vieweroutpututils.h"
namespace olive
{

Core *Core::instance_ = nullptr;

Core::Core(const OakEngineAppParams *params)
	: QObject(nullptr)
	, main_window_(nullptr)
{
	instance_ = this;

	// The opaque C handles that cross the engine ABI boundary are used as
	// signal/slot parameters (and in queued connections / QSignalSpy), so they
	// must be registered with Qt's meta-type system at runtime. Element types
	// are registered before the container types that hold them.
	qRegisterMetaType<OakEngineNode *>();
	qRegisterMetaType<OakEngineKeyframe *>();
	qRegisterMetaType<OakEngineNodeDragger *>();
	qRegisterMetaType<QVector<OakEngineNode *>>();
	qRegisterMetaType<QVector<QPair<OakEngineNode *, OakEngineNode *>>>();

	// Create the engine core through the C ABI (backs the singleton)
	if (params) {
		oakengine_app_create(params);
	} else {
		static const OakEngineAppParams default_params = {0};
		oakengine_app_create(&default_params);
	}

	// Register the UI handlers that the engine uses to request user interaction
	// through the C ABI callback struct instead of engine_core_->set_*_handler().
	{
		OakEngineAppCallbacks cb = {};
		cb.userdata = this;
		cb.confirm_image_sequence = [](const char *filename, void *userdata) -> int {
			return static_cast<Core *>(userdata)->confirm_image_sequence(
					   QString::fromUtf8(filename))
				   ? 1
				   : 0;
		};
		cb.relink_footage = [](OakEngineFootage **footage, int count,
							   void *userdata) -> int {
			QVector<Footage *> fv;
			fv.reserve(count);
			for (int i = 0; i < count; i++) {
				fv.append(reinterpret_cast<Footage *>(footage[i]));
			}
			FootageRelinkDialog frd(fv,
									static_cast<Core *>(userdata)->main_window_);
			return frd.exec() != QDialog::Rejected ? 1 : 0;
		};
		cb.save_project = [](const char *override_filename, void *userdata) {
			static_cast<Core *>(userdata)->save_project_internal(
				override_filename ? QString::fromUtf8(override_filename) :
									QString());
		};
		cb.close_project = [](void *userdata) -> int {
			return static_cast<Core *>(userdata)->close_project(false) ? 1 : 0;
		};
		cb.load_layout = [](const void *layout, void *userdata) {
			static_cast<Core *>(userdata)->main_window_->load_layout(
				*static_cast<const SerializedLayoutInfo *>(layout));
		};
#ifdef USE_OTIO
		cb.otio_import = [](OakEngineSequence **sequences, int count,
							void *userdata) -> int {
			QList<Sequence *> sq;
			sq.reserve(count);
			for (int i = 0; i < count; i++) {
				sq.append(reinterpret_cast<Sequence *>(sequences[i]));
			}
			return static_cast<Core *>(userdata)->DialogImportOTIOShow(sq) ? 1 :
																			  0;
		};
#endif
		oakengine_app_set_callbacks(&cb);
	}

	// Disk cache settings dialog (engine -> UI)
	oakengine_disk_set_settings_handler(
		[](const char *folder_path, void *parent_window, void *userdata) {
			Q_UNUSED(userdata)
			DiskCacheDialog d(
				reinterpret_cast<DiskCacheFolder *>(
					oakengine_disk_get_open_folder(folder_path)),
				reinterpret_cast<QWidget *>(parent_window));
			d.exec();
		}, nullptr);

	// OFX plugin progress dialog (engine -> UI)
	oakengine_plugin_set_progress_reporter_factory(
		[](const char *message, const char *title, void *userdata) -> void * {
			Q_UNUSED(userdata)
			return new PluginProgressDialogReporter(
				QString::fromUtf8(message), QString::fromUtf8(title));
		},
		[](void *reporter, void *userdata) {
			Q_UNUSED(userdata)
			delete reinterpret_cast<PluginProgressDialogReporter *>(reporter);
		},
		[](void *reporter, void *userdata) -> int {
			Q_UNUSED(userdata)
			return reinterpret_cast<PluginProgressDialogReporter *>(reporter)->was_cancelled() ? 1 : 0;
		},
		[](void *reporter, double progress, void *userdata) {
			Q_UNUSED(userdata)
			reinterpret_cast<PluginProgressDialogReporter *>(reporter)->set_progress(progress);
		},
		nullptr);

	// OFX timeline suite: resolve the active viewer through the panels
	oakengine_plugin_set_active_viewer_provider(
		[](void *userdata) -> OakEngineNode * {
			Q_UNUSED(userdata)
			PanelManager *manager = PanelManager::instance();
			if (!manager) {
				return nullptr;
			}

			if (auto *time_panel =
					manager->most_recently_focused<TimeBasedPanel>()) {
				if (time_panel->get_connected_viewer()) {
					return reinterpret_cast<OakEngineNode *>(time_panel->get_connected_viewer());
				}
			}

			QList<TimelinePanel *> timelines =
				manager->get_panels_of_type<TimelinePanel>();
			for (TimelinePanel *panel : timelines) {
				if (panel && panel->get_connected_viewer()) {
					return reinterpret_cast<OakEngineNode *>(panel->get_connected_viewer());
				}
			}

			return nullptr;
		}, nullptr);
}

void Core::start()
{
	// Start the engine (config, locale, managers, autorecovery, recent projects)
	oakengine_app_start();

	//
	// Start application
	//

	switch (oakengine_app_run_mode()) {
	case OAKENGINE_APP_RUN_NORMAL:
		// Start GUI
		start_gui(oakengine_app_fullscreen() != 0);

		// If we have a startup
		QMetaObject::invokeMethod(this, "open_startup_project",
								  Qt::QueuedConnection);
		break;
	case OAKENGINE_APP_RUN_HEADLESS_EXPORT:
		qInfo() << "Headless export is not fully implemented yet";
		break;
	case OAKENGINE_APP_RUN_HEADLESS_PRE_CACHE:
		qInfo() << "Headless pre-cache is not fully implemented yet";
		break;
	}
}

void Core::stop()
{
	// Tear down the UI services first
	MenuShared::destroy_instance();

	PanelManager::destroy_instance();

	oakengine_audio_destroy_instance();

	oakengine_disk_destroy_instance();

	delete main_window_;
	main_window_ = nullptr;

	// Then tear down the engine
	oakengine_app_stop();
}

MainWindow *Core::main_window()
{
	return main_window_;
}

void Core::import_files(const QStringList &urls, Folder *parent)
{
	if (urls.isEmpty()) {
		QMessageBox::critical(main_window_, tr("Import error"),
							  tr("Nothing to import"));
		return;
	}

	QStringList filtered_urls;
	QStringList rejected_urls;
	filtered_urls.reserve(urls.size());

	for (const QString &url : urls) {
		if (is_footage_extension_allowed(url)) {
			filtered_urls.append(url);
		} else {
			rejected_urls.append(url);
		}
	}

	if (!rejected_urls.isEmpty()) {
		QMessageBox::warning(
			main_window_, tr("Unsupported media"),
			tr("Skipped %1 file(s) that are not allowed by the current media "
			   "type filter.")
				.arg(rejected_urls.size()));
	}

	if (filtered_urls.isEmpty()) {
		return;
	}

	QVector<QByteArray> url_ba;
	QVector<const char *> url_ptrs;
	url_ba.reserve(filtered_urls.size());
	url_ptrs.reserve(filtered_urls.size());
	for (const QString &url : filtered_urls) {
		url_ba.append(url.toUtf8());
		url_ptrs.append(url_ba.last().constData());
	}

	OakEngineTask *pim = oakengine_task_create_project_import(
		reinterpret_cast<OakEngineNode *>(parent),
		url_ptrs.data(), url_ptrs.size());

	if (oakengine_task_import_file_count(pim) == 0) {
		oakengine_task_free(pim);
		return;
	}

	TaskDialog *task_dialog =
		new TaskDialog(pim, tr("Importing..."), main_window());

	connect(task_dialog, &TaskDialog::task_succeeded, this,
			&Core::import_task_complete);

	task_dialog->open();
}

void Core::dialog_about_show()
{
	AboutDialog a(false, main_window_);
	a.exec();
}

void Core::dialog_import_show()
{
	// Open dialog for user to select files
	QStringList files =
		QFileDialog::getOpenFileNames(main_window_, tr("Import footage..."),
									  QString(), footage_file_dialog_filter());

	// Check if the user actually selected files to import
	if (!files.isEmpty()) {
		// Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
		ProjectPanel *active_project_panel =
			PanelManager::instance()->most_recently_focused<ProjectPanel>();
		Project *active_project;

		if (active_project_panel ==
				nullptr // Check that we found a Project panel
			|| (active_project = active_project_panel->project()) ==
				   nullptr) { // and that we could find an active Project
			QMessageBox::critical(main_window_, tr("Failed to import footage"),
								  tr("Failed to find active Project panel"));
			return;
		}

		// Get the selected folder in this panel
		Folder *folder = active_project_panel->get_selected_folder();

		import_files(files, folder);
	}
}

void Core::dialog_preferences_show(int start_tab)
{
	PreferencesDialog pd(main_window_, start_tab);
	pd.exec();
}

void Core::dialog_project_properties_show()
{
	Project *proj = get_active_project();

	if (proj) {
		ProjectPropertiesDialog ppd(proj, main_window_);
		ppd.exec();
	} else {
		QMessageBox::critical(
			main_window_, tr("No Active Project"),
			tr("No project is currently open to set the properties for"),
			QMessageBox::Ok);
	}
}

void Core::dialog_export_show()
{
	if (ViewerOutput *viewer = get_sequence_to_export()) {
		open_export_dialog_for_viewer(viewer, false);
	}
}

#ifdef USE_OTIO
bool Core::DialogImportOTIOShow(const QList<Sequence *> &sequences)
{
	Project *active_project = get_active_project();
	OTIOPropertiesDialog opd(sequences, active_project);
	return opd.exec() == QDialog::Accepted;
}
#endif

void Core::create_new_folder()
{
	// Locate the most recently focused Project panel (assume that's the panel the user wants to import into)
	ProjectPanel *active_project_panel =
		PanelManager::instance()->most_recently_focused<ProjectPanel>();
	Project *active_project;

	if (active_project_panel == nullptr // Check that we found a Project panel
		|| (active_project = active_project_panel->project()) ==
			   nullptr) { // and that we could find an active Project
		QMessageBox::critical(main_window_, tr("Failed to create new folder"),
							  tr("Failed to find active project"));
		return;
	}

	// Get the selected folder in this panel
	Folder *folder = active_project_panel->get_selected_folder();

	// Group the three facade edits into a single undo entry.
	oakengine_undo_group_begin(tr("Create New Folder").toUtf8().constData());

	// Create new folder via facade (creates and adds to project, undoable)
	OakEngineNode *new_folder_oak = oakengine_project_add_node(
		reinterpret_cast<OakEngineProject *>(active_project),
		"org.olivevideoeditor.Olive.folder");

	// Set a default name (undoable)
	oakengine_node_set_label(new_folder_oak,
							 tr("New Folder").toUtf8().constData());

	// Add to the selected folder (undoable)
	oakengine_folder_add_child(
		reinterpret_cast<OakEngineNode *>(folder),
		new_folder_oak);

	oakengine_undo_group_end();

	// Trigger an automatic rename so users can enter the folder name
	active_project_panel->edit(new_folder_oak);
}

void Core::create_new_sequence()
{
	Project *active_project = get_active_project();

	if (!active_project) {
		QMessageBox::critical(main_window_, tr("Failed to create new sequence"),
							  tr("Failed to find active project"));
		return;
	}

	// Create new sequence
	Sequence *new_sequence = create_new_sequence_for_project(active_project);

	SequenceDialog sd(new_sequence, SequenceDialog::k_new, main_window_);

	// Make sure SequenceDialog doesn't make an undo command for editing the sequence, since we make an undo command for
	// adding it later on
	sd.set_undoable(false);

	if (sd.exec() == QDialog::Accepted) {
		// Create an undoable command
		void *command = oakengine_undo_command_create_multi();

		oakengine_undo_command_multi_add_child(command,
		oakengine_node_add_to_project_command(
			reinterpret_cast<OakEngineProject *>(active_project),
			reinterpret_cast<OakEngineNode *>(new_sequence)));
		oakengine_folder_add_child(
			reinterpret_cast<OakEngineNode *>(get_selected_folder_in_active_project()),
			reinterpret_cast<OakEngineNode *>(new_sequence));
		oakengine_undo_command_multi_add_child(command, oakengine_node_set_position_command(reinterpret_cast<void *>(new_sequence), reinterpret_cast<void *>(new_sequence), 0.0, 0.0, 0));
		oakengine_undo_command_multi_add_child(command, make_open_sequence_command(new_sequence));

		// Create and connect default nodes to new sequence
		oakengine_sequence_add_default_nodes(
			reinterpret_cast<OakEngineSequence *>(new_sequence));

		oakengine_undo_push(command,
						   tr("Created New Sequence").toUtf8().constData());

	} else {
		// If the dialog was accepted, ownership goes to the AddItemCommand. But if we get here, just delete
		delete new_sequence;
	}
}

void Core::import_task_complete(OakEngineTask *task)
{
	void *command = static_cast<void *>(
		oakengine_task_import_get_command(task));

	int footage_count = oakengine_task_import_footage_count(task);
	QVector<Footage *> imported_footage;
	imported_footage.reserve(footage_count);
	for (int i = 0; i < footage_count; i++) {
		Footage *f = reinterpret_cast<Footage *>(
			oakengine_task_import_footage_at(task, i));
		imported_footage.append(f);

		// Look for multi-layer images
		int vid_count = oakengine_viewer_get_video_stream_count(
			reinterpret_cast<const OakEngineNode *>(f));
		int aud_count = oakengine_viewer_get_audio_stream_count(
			reinterpret_cast<const OakEngineNode *>(f));
		if (aud_count == 0 && vid_count > 1) {
			bool all_stills = true;

			for (int i = 0; i < vid_count; i++) {
				const VideoParams &vs = viewer_output_video_params(f, i);
				if (!(vs.video_type() == 1 &&
					  vs.enabled() == (i == 0))) {
					all_stills = false;
				}
			}

			if (all_stills) {
				QMessageBox d(main_window());

				d.setIcon(QMessageBox::Question);
				d.setWindowTitle(tr("Multi-Layer Image"));
				d.setText(
					tr("The file '%1' has multiple layers. Would you like these layers to be "
					   "separated across multiple tracks or merged into a single image?")
						.arg(f->filename()));

				auto multi_btn =
					d.addButton(tr("Multiple Layers"), QMessageBox::YesRole);
				auto single_btn =
					d.addButton(tr("Single Layer"), QMessageBox::NoRole);
				auto cancel_btn = d.addButton(QMessageBox::Cancel);

				d.exec();

				if (d.clickedButton() == multi_btn) {
					OakEngineFootage *fh = oakengine_footage_borrow(
						reinterpret_cast<OakEngineNode *>(f));
					for (int i = 0; i < vid_count; i++) {
						int enabled = oakengine_footage_get_stream_enabled(
							fh, OAKENGINE_TRACK_TYPE_VIDEO, i);
						oakengine_footage_set_stream_enabled(
							fh, OAKENGINE_TRACK_TYPE_VIDEO, i,
							enabled ? 0 : 1);
					}
					oakengine_footage_free(fh);
				} else if (d.clickedButton() == single_btn) {
					// Do nothing, footage will already be set up this way
				} else if (d.clickedButton() == cancel_btn) {
					// Cancel import
					oakengine_undo_command_free(command);
					return;
				}
			}
		}
	}

	int invalid_count = oakengine_task_import_invalid_files_count(task);
	if (invalid_count > 0) {
		QStringList invalid_files;
		for (int i = 0; i < invalid_count; i++) {
			int len = oakengine_task_import_invalid_file_at(
				task, i, nullptr, 0);
			if (len > 0) {
				QByteArray buf(len + 1, '\0');
				oakengine_task_import_invalid_file_at(
					task, i, buf.data(), buf.size());
				invalid_files.append(QString::fromUtf8(buf.constData()));
			}
		}
		ProjectImportErrorDialog d(invalid_files, main_window_);
		d.exec();
	}

	oakengine_undo_push(
		command,
		tr("Imported %1 File(s)").arg(imported_footage.size()).toUtf8().constData());

	main_window_->select_footage(imported_footage);
}

bool Core::confirm_image_sequence(const QString &filename)
{
	QMessageBox mb(main_window_);

	mb.setIcon(QMessageBox::Question);
	mb.setWindowTitle(tr("Possible image sequence detected"));
	mb.setText(tr("The file '%1' looks like it might be part of an image "
				  "sequence. Would you like to import it as such?")
				   .arg(filename));

	mb.addButton(QMessageBox::Yes);
	mb.addButton(QMessageBox::No);

	return (mb.exec() == QMessageBox::Yes);
}

bool Core::start_headless_export()
{
	QString startup_project;
	{
		int len = oakengine_app_startup_project(nullptr, 0);
		if (len > 0) {
			QByteArray buf(len + 1, '\0');
			oakengine_app_startup_project(buf.data(), buf.size());
			startup_project = QString::fromUtf8(buf.constData());
		}
	}

	if (startup_project.isEmpty()) {
		qCritical().noquote()
			<< tr("You must specify a project file to export");
		return false;
	}

	if (!QFileInfo::exists(startup_project)) {
		qCritical().noquote() << tr("Specified project does not exist");
		return false;
	}

	// Start a load task and try running it
	OakEngineTask *plm = oakengine_task_create_project_load(
		startup_project.toUtf8().constData());

	/*
  if (oakengine_cli_task_dialog_run(plm, nullptr)) {
    OakEngineProject *p = oakengine_task_save_get_project(plm); // FIXME: load task accessor
    QVector<Item*> items = p->get_items_of_type(Item::kSequence);

    // Check if this project contains sequences
    if (items.isEmpty()) {
      qCritical().noquote() << tr("Project contains no sequences, nothing to export");
      return false;
    }

    Sequence* sequence = nullptr;

    // Check if this project contains multiple sequences
    if (items.size() > 1) {
      qInfo().noquote() << tr("This project has multiple sequences. Which do you wish to export?");
      for (int i=0;i<items.size();i++) {
        std::cout << "[" << i << "] " << items.at(i)->GetLabel().toStdString();
      }

      QTextStream stream(stdin);
      QString sequence_read;
      int sequence_index = -1;
      QString quit_code = QStringLiteral("q");
      std::string prompt = tr("Enter number (or %1 to cancel): ").arg(quit_code).toStdString();
      forever {
        std::cout << prompt;

        stream.readLineInto(&sequence_read);

        if (!QString::compare(sequence_read, quit_code, Qt::CaseInsensitive)) {
          return false;
        }

        bool ok;
        sequence_index = sequence_read.toInt(&ok);

        if (ok && sequence_index >= 0 && sequence_index < items.size()) {
          break;
        } else {
          qCritical().noquote() << tr("Invalid sequence number");
        }
      }

      sequence = static_cast<Sequence*>(items.at(sequence_index));
    } else {
      sequence = static_cast<Sequence*>(items.first());
    }

    ExportParams params;
    ExportTask export_task(sequence->viewer_output(), p->color_manager(), params);
    CLITaskDialog export_dialog(&export_task);
    if (export_dialog.Run()) {
      qInfo().noquote() << tr("Export succeeded");
      return true;
    } else {
      qInfo().noquote() << tr("Export failed: %1").arg(export_task.GetError());
      return false;
    }
  } else {
    char err[512];
    err[0] = '\0';
    oakengine_task_error(plm, err, sizeof(err));
    qCritical().noquote() << tr("Project failed to load: %1").arg(QString::fromUtf8(err));
    return false;
  }
  */

	oakengine_task_free(plm);

	return false;
}

void Core::open_startup_project()
{
	QString startup_project;
	{
		int len = oakengine_app_startup_project(nullptr, 0);
		if (len > 0) {
			QByteArray buf(len + 1, '\0');
			oakengine_app_startup_project(buf.data(), buf.size());
			startup_project = QString::fromUtf8(buf.constData());
		}
	}
	bool startup_project_exists = !startup_project.isEmpty() &&
								  QFileInfo::exists(startup_project);

	// Load startup project
	if (!startup_project_exists && !startup_project.isEmpty()) {
		QMessageBox::warning(main_window_, tr("Failed to open startup file"),
							 tr("The project \"%1\" doesn't exist. "
								"A new project will be started instead.")
								 .arg(startup_project),
							 QMessageBox::Ok);
	}

	if (startup_project_exists) {
		// If a startup project was set and exists, open it now
		open_project_internal(startup_project);
	} else {
		// If no load project is set, create a new one on open
		create_new_project();
	}
}

void Core::start_gui(bool full_screen)
{
	// Set UI style
	StyleManager::init();

	// Set up shared menus
	MenuShared::create_instance();

	// Since we're starting GUI mode, create a PanelFocusManager (auto-deletes with QObject)
	PanelManager::create_instance();

	// Initialize audio service
	oakengine_audio_create_instance();

	// Initialize disk service
	oakengine_disk_create_instance();

	// Connect the PanelFocusManager to the application's focus change signal
	connect(qApp, &QApplication::focusChanged, PanelManager::instance(),
			&PanelManager::focus_changed);

	KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
	// Set KDDockWidgets flags
	auto &config = KDDockWidgets::Config::self();
	auto flags = config.flags();
	flags |= KDDockWidgets::Config::Flag_TabsHaveCloseButton;
	flags |= KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible;
	flags |= KDDockWidgets::Config::Flag_AlwaysShowTabs;
	flags |= KDDockWidgets::Config::Flag_AllowReorderTabs;
	config.setFlags(flags);
	config.setAbsoluteWidgetMinSize(QSize(1, 1));

	// Create main window and open it
	main_window_ = new MainWindow();

	// Route engine notifications to the UI
	connect(this, &Core::tool_changed, this, [this](const Tool::Item &) {});
	// Status-bar and lifecycle notifications are handled through the facade
	// (oakengine_app_show_status_message, oakengine_app_clear_status_message)
	// which the engine forwards through registered callbacks. The main window
	// status bar is updated separately during start_gui.
	main_window_->statusBar()->showMessage(QString());
	connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
			main_window_->statusBar(), &QStatusBar::clearMessage);

	if (full_screen) {
		main_window_->showFullScreen();
	} else {
		main_window_->showMaximized();
	}

#ifdef Q_OS_WINDOWS
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	// Workaround for Qt bug where menus don't appear in full screen mode
	// See: https://doc.qt.io/qt-5/windows-issues.html
	QWindowsWindowFunctions::setHasBorderInFullScreen(
		main_window_->windowHandle(), true);
#endif
#endif
}

void Core::save_project_internal(const QString &override_filename)
{
	Project *open_proj_ = reinterpret_cast<Project *>(oakengine_app_open_project());

	// Get project filename via facade
	char fn_buf[512];
	oakengine_project_filename(
		reinterpret_cast<OakEngineProject *>(open_proj_),
		fn_buf, sizeof(fn_buf));
	QString fn = QString::fromUtf8(fn_buf);

	// Create save manager
	OakEngineTask *psm = nullptr;

	if (fn.endsWith(QStringLiteral(".otio"),
					   Qt::CaseInsensitive)) {
#ifdef USE_OTIO
		psm = oakengine_task_create_project_save_otio(
			reinterpret_cast<OakEngineProject *>(open_proj_));
#else
		QMessageBox::critical(
			main_window_, tr("Missing OpenTimelineIO Libraries"),
			tr("This build was compiled without OpenTimelineIO and therefore "
			   "cannot open OpenTimelineIO files."));
		return;
#endif
	} else {
		bool use_compression = !fn.endsWith(
			QStringLiteral(".ovexml"), Qt::CaseInsensitive);
		SerializedLayoutInfo layout = main_window_->save_layout();
		psm = oakengine_task_create_project_save(
			reinterpret_cast<OakEngineProject *>(open_proj_),
			use_compression ? 1 : 0,
			override_filename.isEmpty() ? nullptr :
				override_filename.toUtf8().constData(),
			&layout);
	}

	// We don't use a TaskDialog here because a model save dialog is annoying, particularly when
	// saving auto-recoveries that the user can't anticipate. Doing this in the main thread will
	// cause a brief (but often unnoticeable) pause in the GUI, which, while not ideal, is not that
	// different from what already happened (modal dialog preventing use of the GUI) and in many ways
	// less annoying (doesn't disrupt any current actions or pull focus from elsewhere).
	//
	// Ideally we could do this in a background thread and show progress in the status bar like
	// Microsoft Word, but that would be far more complex. If it becomes necessary in the future,
	// we will look into an approach like that.
	if (oakengine_task_start_sync(psm) == 1) {
		if (override_filename.isEmpty()) {
			project_save_succeeded(psm);
		}
	}

	oakengine_task_free(psm);
}

ViewerOutput *Core::get_sequence_to_export()
{
	// First try the most recently focused time based window
	TimeBasedPanel *time_panel =
		PanelManager::instance()->most_recently_focused<TimeBasedPanel>();

	// If that fails try defaulting to the first timeline (i.e. if a project has just been loaded).
	if (!time_panel->get_connected_viewer()) {
		// Safe to assume there will always be one timeline.
		time_panel =
			PanelManager::instance()->get_panels_of_type<TimelinePanel>().first();
	}

	if (time_panel && time_panel->get_connected_viewer()) {
		if (time_panel->get_connected_viewer()->get_length() == 0) {
			QMessageBox::critical(
				main_window_, tr("Error"),
				tr("This Sequence is empty. There is nothing to export."),
				QMessageBox::Ok);
		} else {
			return time_panel->get_connected_viewer();
		}
	} else {
		QMessageBox::critical(
			main_window_, tr("Error"),
			tr("No valid sequence detected.\n\nMake sure a sequence is loaded and it has a connected Viewer node."),
			QMessageBox::Ok);
	}

	return nullptr;
}

bool Core::revert_project_internal(bool by_opening_existing)
{
	Project *cur_proj = reinterpret_cast<Project *>(oakengine_app_open_project());
	char fn_buf[512];
	oakengine_project_filename(
		reinterpret_cast<OakEngineProject *>(cur_proj),
		fn_buf, sizeof(fn_buf));
	QString cur_fn = QString::fromUtf8(fn_buf);

	char name_buf[256];
	oakengine_project_name(reinterpret_cast<OakEngineProject *>(cur_proj),
			       name_buf, sizeof(name_buf));
	QString cur_name = QString::fromUtf8(name_buf);

	if (cur_fn.isEmpty()) {
		QMessageBox::critical(
			main_window_, tr("Revert"),
			tr("This project has not yet been saved, therefore there is no last saved state to revert to."));
	} else {
		QString msg;

		if (by_opening_existing) {
			msg =
				tr("The project \"%1\" is already open. By re-opening it, the project will revert to "
				   "its last saved state. Any unsaved changes will be lost. Do you wish to continue?")
					.arg(cur_fn);
		} else {
			msg =
				tr("This will revert the project \"%1\" back to its last saved state. "
				   "All unsaved changes will be lost. Do you wish to continue?")
					.arg(cur_name);
		}

		if (QMessageBox::question(main_window_, tr("Revert"), msg,
								  QMessageBox::Ok | QMessageBox::Cancel) ==
			QMessageBox::Ok) {
			// Copy filename because CloseProject is going to delete `p`
			QString filename = cur_fn;

			// Close project without prompting to save it
			close_project(false, true);

			// NOTE: `open_project_` will be deleted now, so don't try accessing it

			// Re-open project at the filename
			open_project_internal(filename);

			return true;
		}
	}

	return false;
}

void Core::project_save_succeeded(OakEngineTask *task)
{
	Project *p = reinterpret_cast<Project *>(
		oakengine_task_save_get_project(task));

	oakengine_app_on_project_saved(reinterpret_cast<OakEngineProject *>(p));

	char fn_buf[512];
	oakengine_project_filename(reinterpret_cast<OakEngineProject *>(p),
				   fn_buf, sizeof(fn_buf));
	show_status_bar_message(tr("Saved to \"%1\" successfully").arg(fn_buf));
}

Project *Core::get_active_project() const
{
	return reinterpret_cast<Project *>(oakengine_app_open_project());
}

Folder *Core::get_selected_folder_in_active_project() const
{
	ProjectPanel *active_project_panel =
		PanelManager::instance()->most_recently_focused<ProjectPanel>();

	if (active_project_panel) {
		return active_project_panel->get_selected_folder();
	} else {
		return nullptr;
	}
}

QString Core::get_project_filter(bool include_any_filter)
{
	static const QVector<QPair<QString, QString>> filters = {
		// Standard compressed Oak project
		{ tr("Oak Project"), QStringLiteral("ove") },

		// Uncompressed XML Oak project
		{ tr("Oak Project (Uncompressed XML)"), QStringLiteral("ovexml") },

	// OpenTimelineIO project, if available
#ifdef USE_OTIO
		{ tr("OpenTimelineIO"), QStringLiteral("otio") }
#endif
	};

	QStringList filter_strings;
	filter_strings.reserve(filters.size() + 1);

	if (include_any_filter) {
		QStringList combined;
		for (auto it = filters.cbegin(); it != filters.cend(); it++) {
			combined.append(QStringLiteral("*.%1").arg(it->second));
		}
		filter_strings.append(QStringLiteral("%1 (%2)").arg(
			tr("All Supported Projects"), combined.join(' ')));
	}

	for (auto it = filters.cbegin(); it != filters.cend(); it++) {
		filter_strings.append(QStringLiteral("%1 (*.%2)").arg(it->first, it->second));
	}

	return filter_strings.join(QStringLiteral(";;"));
}

bool Core::save_project()
{
	Project *saved_proj = reinterpret_cast<Project *>(oakengine_app_open_project());

	char fn_buf[512];
	oakengine_project_filename(
		reinterpret_cast<OakEngineProject *>(saved_proj),
		fn_buf, sizeof(fn_buf));
	if (fn_buf[0] == '\0') {
		return save_project_as();
	} else {
		save_project_internal();
		return true;
	}
}

void Core::open_recovery_project(const QString &filename)
{
	open_project_internal(filename, true);
}

void Core::open_node_in_viewer(ViewerOutput *viewer)
{
	main_window_->open_node_in_viewer(viewer);
}

void Core::open_export_dialog_for_viewer(ViewerOutput *viewer,
									 bool start_still_image)
{
	ExportDialog *ed =
		new ExportDialog(viewer, start_still_image, main_window_);
	connect(ed, &ExportDialog::finished, ed, &ExportDialog::deleteLater);
	ed->open();
	connect(ed, &ExportDialog::request_import_file, this,
			&Core::import_single_file);
}

void Core::check_for_auto_recoveries()
{
	QString autorecovery_index_path;
	{
		int len = oakengine_app_auto_recovery_index_filename(nullptr, 0);
		if (len > 0) {
			QByteArray buf(len + 1, '\0');
			oakengine_app_auto_recovery_index_filename(buf.data(), buf.size());
			autorecovery_index_path = QString::fromUtf8(buf.constData());
		}
	}
	QFile autorecovery_index(autorecovery_index_path);
	if (autorecovery_index.exists()) {
		// Uh-oh, we have auto-recoveries to prompt
		if (autorecovery_index.open(QFile::ReadOnly)) {
			QStringList recovery_filenames =
				QString::fromUtf8(autorecovery_index.readAll()).split('\n');

			AutoRecoveryDialog ard(
				tr("The following projects had unsaved changes when Oak Video Editor "
				   "forcefully quit. Would you like to load them?"),
				recovery_filenames, true, main_window_);
			ard.exec();

			autorecovery_index.close();

			// Delete recovery index since we don't need it anymore
			QFile::remove(autorecovery_index_path);
		} else {
			QMessageBox::critical(
				main_window_, tr("Auto-Recovery Error"),
				tr("Found auto-recoveries but failed to load the auto-recovery index. "
				   "Auto-recover projects will have to be opened manually.\n\n"
				   "Your recoverable projects are still available at: %1")
					.arg(FileFunctions::get_auto_recovery_root()));
		}
	}
}

void Core::browse_auto_recoveries()
{
	// List all auto-recovery entries
	AutoRecoveryDialog ard(
		tr("The following project versions have been auto-saved:"),
		QDir(FileFunctions::get_auto_recovery_root())
			.entryList(QDir::Dirs | QDir::NoDotAndDotDot),
		false, main_window_);
	ard.exec();
}

void Core::show_cache_full_warning()
{
	QMessageBox::warning(
		main_window_, tr("Disk Cache Full"),
		tr("The disk cache is currently full and Oak Video Editor is having to delete old "
		   "frames to keep it within the limits set in the Disk preferences. This "
		   "will result in SIGNIFICANTLY reduced cache performance.\n\n"
		   "To remedy this, please do one of the following:\n\n"
		   "1. Manually clear the disk cache in Disk preferences.\n"
		   "2. Increase the maximum disk cache size in Disk preferences.\n"
		   "3. Reduce usage of the disk cache (e.g. disable auto-cache or only cache specific sections of your sequence)."));
}

void Core::on_active_project_changed(Project *p)
{
	main_window_->set_project(p);

	if (p) {
		auto *ph = reinterpret_cast<OakEngineProject *>(p);
		// Keep the window's modified state in sync via event subscription
		// (connection is removed automatically when the project is deleted).
		oakengine_event_subscribe(ph, OAKENGINE_EVENT_PROJECT_MODIFIED_CHANGED,
			[](const oakengine_event *event, void *userdata) {
				QMainWindow *mw = static_cast<QMainWindow *>(userdata);
				mw->setWindowModified(event->a != 0);
			},
			main_window_);
	}
}

bool Core::save_project_as()
{
	QFileDialog fd(main_window_, tr("Save Project As"));

	fd.setAcceptMode(QFileDialog::AcceptSave);
	fd.setNameFilter(get_project_filter(false));

	if (fd.exec() == QDialog::Accepted) {
		QString fn = fd.selectedFiles().first();

		// Somewhat hacky method of extracting the extension from the name filter
		const QString &name_filter = fd.selectedNameFilter();
		int ext_index = name_filter.indexOf(QStringLiteral("(*.")) + 3;
		QString extension =
			name_filter.mid(ext_index, name_filter.size() - ext_index - 1);

		fn = FileFunctions::ensure_filename_extension(fn, extension);

		oakengine_project_set_filename(
			reinterpret_cast<OakEngineProject *>(static_cast<QObject *>(reinterpret_cast<Project *>(oakengine_app_open_project()))),
			fn.toUtf8().constData());

		save_project_internal();

		return true;
	}

	return false;
}

void Core::revert_project()
{
	revert_project_internal(false);
}

void Core::open_project_internal(const QString &filename, bool recovery_project)
{
	Project *open_proj = reinterpret_cast<Project *>(oakengine_app_open_project());
	if (open_proj) {
		char fn_buf[512];
		oakengine_project_filename(
			reinterpret_cast<OakEngineProject *>(open_proj),
			fn_buf, sizeof(fn_buf));
		// Comparing QFileInfos will handle case insensitivity and both slash directions on platforms
		// where this is necessary (not naming any names *cough* Windows)
		if (QFileInfo(fn_buf) == QFileInfo(filename)) {
			// This project is already open
			bool reverted = revert_project_internal(true);

			if (!reverted) {
				// Calling this will focus attention to the project that the user just tried to re-open
				oakengine_app_add_open_project_vp(open_proj, 0);
			}

			// Don't do anything else
			return;
		}
	}

	OakEngineTask *load_task = nullptr;

	if (filename.endsWith(QStringLiteral(".otio"), Qt::CaseInsensitive)) {
		// Load OpenTimelineIO project
#ifdef USE_OTIO
		load_task = oakengine_task_create_project_load_otio(
			filename.toUtf8().constData());
#else
		QMessageBox::critical(
			main_window_, tr("Missing OpenTimelineIO Libraries"),
			tr("This build was compiled without OpenTimelineIO and therefore "
			   "cannot open OpenTimelineIO files."));
		return;
#endif
	} else {
		// Fallback to regular OVE project
		load_task = oakengine_task_create_project_load(
			filename.toUtf8().constData());
	}

	TaskDialog *task_dialog =
		new TaskDialog(load_task, tr("Load Project"), main_window());

	if (recovery_project) {
		connect(task_dialog, &TaskDialog::task_succeeded, this,
				&Core::add_recovery_project_from_task);
	} else {
		connect(task_dialog, &TaskDialog::task_succeeded, this,
				&Core::add_open_project_from_task_and_add_to_recents);
	}

	task_dialog->open();
}

void Core::import_single_file(const QString &f)
{
	if (Project *p = get_active_project()) {
		import_files({ f }, p->root());
	}
}

bool Core::label_nodes(const QVector<Node *> &nodes, void *parent)
{
	if (nodes.isEmpty()) {
		return false;
	}

	bool ok;

	QString start_label = nodes.first()->get_label();

	for (int i = 1; i < nodes.size(); i++) {
		if (nodes.at(i)->get_label() != start_label) {
			// Not all the nodes share the same name, so we'll start with a blank one
			start_label.clear();
			break;
		}
	}

	QString s = QInputDialog::getText(main_window_, tr("Label Node"),
									  tr("Set node label"), QLineEdit::Normal,
									  start_label, &ok);

	if (ok) {
		QVector<OakEngineNode *> oak_nodes;
		oak_nodes.reserve(nodes.size());
		foreach (Node *n, nodes) {
			oak_nodes.append(reinterpret_cast<OakEngineNode *>(n));
		}
		oakengine_node_rename_many(oak_nodes.data(), oak_nodes.size(),
								s.toUtf8().constData(), parent);

		return true;
	}

	return false;
}

void Core::open_project_from_recent_list(int index)
{
	int rp_len = oakengine_app_recent_project_at(index, nullptr, 0);
	if (rp_len <= 0) return;
	QByteArray rp_buf(rp_len + 1, '\0');
	oakengine_app_recent_project_at(index, rp_buf.data(), rp_buf.size());
	const QString open_fn = QString::fromUtf8(rp_buf.constData());

	if (QFileInfo::exists(open_fn)) {
		open_project_internal(open_fn);
	} else if (
		QMessageBox::information(
			main_window(), tr("Cannot open recent project"),
			tr("The project \"%1\" doesn't exist. Would you like to remove this file from the recent list?")
				.arg(open_fn),
			QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
		oakengine_app_remove_recently_opened_project(index);
	}
}

bool Core::close_project(bool auto_open_new, bool ignore_modified)
{
	Project *close_proj = reinterpret_cast<Project *>(oakengine_app_open_project());
	if (close_proj) {
		char name_buf[256];
		oakengine_project_name(
			reinterpret_cast<OakEngineProject *>(close_proj),
			name_buf, sizeof(name_buf));
		if (close_proj->is_modified() && !ignore_modified) {
			QMessageBox mb(main_window_);

			mb.setWindowModality(Qt::WindowModal);
			mb.setIcon(QMessageBox::Question);
			mb.setWindowTitle(tr("Unsaved Changes"));
			mb.setText(
				tr("The project '%1' has unsaved changes. Would you like to save them?")
					.arg(name_buf));

			QPushButton *yes_btn =
				mb.addButton(tr("Save"), QMessageBox::YesRole);

			mb.addButton(tr("Don't Save"), QMessageBox::NoRole);

			QPushButton *cancel_btn = mb.addButton(QMessageBox::Cancel);

			mb.exec();

			if (mb.clickedButton() == cancel_btn) {
				// Stop closing projects if the user clicked cancel
				return false;
			}

			if (mb.clickedButton() == yes_btn && !save_project()) {
				// The save failed, stop closing projects
				return false;
			}
		}

		// For safety, the undo stack is cleared so no commands try to affect a freed project
		oakengine_undo_clear();

		Project *tmp = reinterpret_cast<Project *>(oakengine_app_open_project());
		oakengine_app_set_active_project_vp(nullptr);
		delete tmp;
	}

	// Ensure a project is always active
	if (auto_open_new) {
		create_new_project();
	}

	return true;
}

void Core::cache_active_sequence(bool in_out_only)
{
	TimeBasedPanel *p =
		PanelManager::instance()->most_recently_focused<TimeBasedPanel>();

	if (p && p->get_connected_viewer()) {
		// Hacky but works for now

		// Find Viewer attached to this TimeBasedPanel
		QList<ViewerPanel *> all_viewers =
			PanelManager::instance()->get_panels_of_type<ViewerPanel>();

		ViewerPanel *found_panel = nullptr;

		foreach (ViewerPanel *viewer, all_viewers) {
			if (viewer->get_connected_viewer() == p->get_connected_viewer()) {
				found_panel = viewer;
				break;
			}
		}

		if (found_panel) {
			if (in_out_only) {
				found_panel->cache_sequence_in_out();
			} else {
				found_panel->cache_entire_sequence();
			}
		} else {
			QMessageBox::critical(
				main_window_, tr("Failed to cache sequence"),
				tr("No active viewer found with this sequence."),
				QMessageBox::Ok);
		}
	}
}

void Core::open_project()
{
	QString file = QFileDialog::getOpenFileName(
		main_window_, tr("Open Project"), QString(), get_project_filter(true));

	if (!file.isEmpty()) {
		open_project_internal(file);
	}
}

// ---- Facade-wrapping method implementations ----

UndoStack *Core::undo_stack() const
{
	return reinterpret_cast<UndoStack *>(oakengine_undo_handle());
}

Tool::Item Core::tool() const
{
	return static_cast<Tool::Item>(oakengine_app_tool());
}

void Core::set_tool(const Tool::Item &tool)
{
	oakengine_app_set_tool(static_cast<int>(tool));
	emit tool_changed(tool);
}

bool Core::snapping() const
{
	return oakengine_app_snapping() != 0;
}

void Core::set_snapping(const bool &b)
{
	oakengine_app_set_snapping(b ? 1 : 0);
	emit snapping_changed(b);
}

Timecode::Display Core::get_timecode_display() const
{
	return static_cast<Timecode::Display>(oakengine_app_timecode_display());
}

void Core::set_timecode_display(Timecode::Display d)
{
	oakengine_app_set_timecode_display(static_cast<int>(d));
	emit timecode_display_changed(d);
}

void Core::show_status_bar_message(const QString &s, int timeout)
{
	oakengine_app_show_status_message(s.toUtf8().constData(), timeout);
}

void Core::clear_status_bar_message()
{
	oakengine_app_clear_status_message();
}

QString Core::footage_file_dialog_filter()
{
	// Use buf/size convention to query the filter
	int len = oakengine_app_footage_file_dialog_filter(nullptr, 0);
	if (len <= 0) {
		return QString();
	}
	QByteArray buf(len + 1, '\0');
	oakengine_app_footage_file_dialog_filter(buf.data(), buf.size());
	return QString::fromUtf8(buf.constData());
}

bool Core::is_footage_extension_allowed(const QString &path)
{
	return oakengine_app_is_footage_extension_allowed(
			   path.toUtf8().constData()) == 1;
}

void Core::create_new_project()
{
	oakengine_app_create_new_project();
}

Sequence *Core::create_new_sequence_for_project(const QString &format,
											 Project *project)
{
	return reinterpret_cast<Sequence *>(
		oakengine_app_create_sequence(
			reinterpret_cast<OakEngineProject *>(project),
			format.toUtf8().constData()));
}

Sequence *Core::create_new_sequence_for_project(Project *project)
{
	return instance()->create_new_sequence_for_project(QStringLiteral("Sequence %1"), project);
}

void Core::clear_open_recent_list()
{
	oakengine_app_clear_recent_projects();
	emit open_recent_list_changed();
}

void Core::set_use_proxy_media(bool enabled)
{
	oakengine_app_set_use_proxy_media(enabled ? 1 : 0);
}

void Core::request_pixel_sampling_in_viewers(bool e)
{
	oakengine_app_request_pixel_sampling(e ? 1 : 0);
	emit color_picker_enabled(e);
}

Tool::AddableObject Core::get_selected_addable_object() const
{
	return static_cast<Tool::AddableObject>(oakengine_app_addable_object());
}

void Core::set_selected_addable_object(const Tool::AddableObject &obj)
{
	oakengine_app_set_addable_object(static_cast<int>(obj));
	emit addable_object_changed(obj);
}

void Core::set_selected_transition_object(const QString &obj)
{
	oakengine_app_set_selected_transition(obj.toUtf8().constData());
}

void Core::copy_string_to_clipboard(const QString &s)
{
	oakengine_app_copy_to_clipboard(s.toUtf8().constData());
}

void Core::set_magic(bool e)
{
	oakengine_app_set_magic(e ? 1 : 0);
}

bool Core::add_open_project_from_task(OakEngineTask *task, bool add_to_recents)
{
	return oakengine_app_add_open_project_from_task(task, add_to_recents ? 1 : 0) == 1;
}

bool Core::add_recovery_project_from_task(OakEngineTask *task)
{
	return oakengine_app_add_recovery_project_from_task(task) == 1;
}

int Core::get_recent_project_count() const
{
	return oakengine_app_recent_projects_count();
}

QString Core::get_recent_project_at(int index) const
{
	int len = oakengine_app_recent_project_at(index, nullptr, 0);
	if (len <= 0) {
		return QString();
	}
	QByteArray buf(len + 1, '\0');
	oakengine_app_recent_project_at(index, buf.data(), buf.size());
	return QString::fromUtf8(buf.constData());
}

// ---- EngineCore forwarding methods (delegate through C ABI) ----

bool Core::set_language(const QString &locale)
{
	return oakengine_app_set_language(locale.toUtf8().constData()) > 0;
}

void Core::set_autorecovery_interval(int minutes)
{
	oakengine_app_set_autorecovery_interval(minutes);
}

void Core::on_project_saved(Project *p)
{
	oakengine_app_on_project_saved(reinterpret_cast<OakEngineProject *>(p));
}

QString Core::get_auto_recovery_index_filename()
{
	int len = oakengine_app_auto_recovery_index_filename(nullptr, 0);
	if (len <= 0) return QString();
	QByteArray buf(len + 1, '\0');
	oakengine_app_auto_recovery_index_filename(buf.data(), buf.size());
	return QString::fromUtf8(buf.constData());
}

void Core::add_open_project(olive::Project *p, bool add_to_recents)
{
	oakengine_app_add_open_project(reinterpret_cast<OakEngineProject *>(p),
								 add_to_recents ? 1 : 0);
}

void Core::remove_recently_opened_project(int index)
{
	oakengine_app_remove_recently_opened_project(index);
}

void Core::set_active_project(Project *p)
{
	oakengine_app_set_active_project(reinterpret_cast<OakEngineProject *>(p));
}

QString Core::get_selected_transition() const
{
	int len = oakengine_app_selected_transition(nullptr, 0);
	if (len <= 0) return QString();
	QByteArray buf(len + 1, '\0');
	oakengine_app_selected_transition(buf.data(), buf.size());
	return QString::fromUtf8(buf.constData());
}

} // namespace olive
