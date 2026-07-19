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
#include "window/mainwindow/mainwindowundo.h"
#ifdef Q_OS_WINDOWS
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtPlatformHeaders/QWindowsWindowFunctions>
#endif
#endif

#include "audio/audiomanager.h"
#include "cli/clitask/clitaskdialog.h"
#include "common/filefunctions.h"
#include "common/xmlutils.h"
#include "config/config.h"
#include "dialog/about/about.h"
#include "dialog/autorecovery/autorecoverydialog.h"
#include "dialog/export/export.h"
#include "dialog/footagerelink/footagerelinkdialog.h"
#ifdef USE_OTIO
#include "dialog/otioproperties/otiopropertiesdialog.h"
#endif
#include "dialog/projectproperties/projectproperties.h"
#include "dialog/sequence/sequence.h"
#include "dialog/task/task.h"
#include "dialog/preferences/preferences.h"
#include "node/nodeundo.h"
#include "panel/panelmanager.h"
#include "panel/project/project.h"
#include "panel/viewer/viewer.h"
#include "render/diskmanager.h"
#ifdef USE_OTIO
#include "task/project/loadotio/loadotio.h"
#include "task/project/saveotio/saveotio.h"
#endif
#include "task/project/import/import.h"
#include "dialog/projectimport/projectimporterrordialog.h"
#include "task/project/load/load.h"
#include "task/project/save/save.h"
#include "ui/style/style.h"
#include "widget/menu/menushared.h"
#include "window/mainwindow/mainwindow.h"

namespace olive
{

Core::Core(const CoreParams &params)
	: EngineCore(params)
	, main_window_(nullptr)
{
	// Register the UI handlers that the engine uses to request user interaction
	set_confirm_image_sequence_handler(
		[this](const QString &filename) {
			return confirm_image_sequence(filename);
		});

	set_relink_handler([this](QVector<Footage *> footage) {
		FootageRelinkDialog frd(footage, main_window_);
		return frd.exec() != QDialog::Rejected;
	});

	set_save_project_handler([this](const QString &override_filename) {
		save_project_internal(override_filename);
	});

	set_close_project_handler([this] { return close_project(false); });

	set_load_layout_handler([this](const MainWindowLayoutInfo &layout) {
		main_window_->load_layout(layout);
	});

#ifdef USE_OTIO
	set_otio_import_handler([this](const QList<Sequence *> &sequences) {
		return DialogImportOTIOShow(sequences);
	});
#endif
}

void Core::start()
{
	// Start the engine (config, locale, managers, autorecovery, recent projects)
	EngineCore::start();

	//
	// Start application
	//

	switch (core_params().run_mode()) {
	case CoreParams::k_run_normal:
		// Start GUI
		start_gui(core_params().fullscreen());

		// If we have a startup
		QMetaObject::invokeMethod(this, "open_startup_project",
								  Qt::QueuedConnection);
		break;
	case CoreParams::k_headless_export:
		qInfo() << "Headless export is not fully implemented yet";
		break;
	case CoreParams::k_headless_pre_cache:
		qInfo() << "Headless pre-cache is not fully implemented yet";
		break;
	}
}

void Core::stop()
{
	// Tear down the UI services first
	MenuShared::destroy_instance();

	PanelManager::destroy_instance();

	AudioManager::destroy_instance();

	DiskManager::destroy_instance();

	delete main_window_;
	main_window_ = nullptr;

	// Then tear down the engine
	EngineCore::stop();
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

	ProjectImportTask *pim = new ProjectImportTask(parent, filtered_urls);

	if (!pim->get_file_count()) {
		// No files to import
		delete pim;
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

	// Create new folder
	Folder *new_folder = new Folder();

	// Set a default name
	new_folder->set_label(tr("New Folder"));

	// Create an undoable command
	MultiUndoCommand *command = new MultiUndoCommand();

	command->add_child(new NodeAddCommand(active_project, new_folder));
	command->add_child(new FolderAddChild(folder, new_folder));

	Core::instance()->undo_stack()->push(command, tr("Created New Folder"));

	// Trigger an automatic rename so users can enter the folder name
	active_project_panel->edit(new_folder);
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
		MultiUndoCommand *command = new MultiUndoCommand();

		command->add_child(new NodeAddCommand(active_project, new_sequence));
		command->add_child(new FolderAddChild(
			get_selected_folder_in_active_project(), new_sequence));
		command->add_child(new NodeSetPositionCommand(
			new_sequence, new_sequence, Node::Position()));
		command->add_child(new OpenSequenceCommand(new_sequence));

		// Create and connect default nodes to new sequence
		new_sequence->add_default_nodes(command);

		Core::instance()->undo_stack()->push(command,
											 tr("Created New Sequence"));

	} else {
		// If the dialog was accepted, ownership goes to the AddItemCommand. But if we get here, just delete
		delete new_sequence;
	}
}

void Core::import_task_complete(Task *task)
{
	ProjectImportTask *import_task = static_cast<ProjectImportTask *>(task);

	MultiUndoCommand *command = import_task->get_command();

	foreach (Footage *f, import_task->get_imported_footage()) {
		// Look for multi-layer images
		if (f->get_audio_stream_count() == 0 && f->get_video_stream_count() > 1) {
			bool all_stills = true;

			for (int i = 0; i < f->get_video_stream_count(); i++) {
				const VideoParams &vs = f->get_video_params(i);
				if (!(vs.video_type() == VideoParams::k_video_type_still &&
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
					for (int i = 0; i < f->get_video_stream_count(); i++) {
						VideoParams vs = f->get_video_params(i);
						vs.set_enabled(!vs.enabled());
						f->set_video_params(vs, i);
					}
				} else if (d.clickedButton() == single_btn) {
					// Do nothing, footage will already be set up this way
				} else if (d.clickedButton() == cancel_btn) {
					// Cancel import
					delete command;
					return;
				}
			}
		}
	}

	if (import_task->has_invalid_files()) {
		ProjectImportErrorDialog d(import_task->get_invalid_files(),
								   main_window_);
		d.exec();
	}

	undo_stack()->push(
		command,
		tr("Imported %1 File(s)").arg(import_task->get_imported_footage().size()));

	main_window_->select_footage(import_task->get_imported_footage());
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
	const QString &startup_project = core_params().startup_project();

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
	ProjectLoadTask plm(startup_project);
	CLITaskDialog task_dialog(&plm);

	/*
  if (task_dialog.Run()) {
    std::unique_ptr<Project> p = std::unique_ptr<Project>(plm.GetLoadedProject());
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
    qCritical().noquote() << tr("Project failed to load: %1").arg(plm.GetError());
    return false;
  }
  */

	return false;
}

void Core::open_startup_project()
{
	const QString &startup_project = core_params().startup_project();
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
	AudioManager::create_instance();

	// Initialize disk service
	DiskManager::create_instance();

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
	connect(this, &EngineCore::status_message_show, main_window_->statusBar(),
			&QStatusBar::showMessage);
	connect(this, &EngineCore::status_message_clear, main_window_->statusBar(),
			&QStatusBar::clearMessage);
	connect(this, &EngineCore::cache_full_warning_requested, this,
			&Core::show_cache_full_warning);
	connect(this, &EngineCore::active_project_changed, this,
			&Core::on_active_project_changed);

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
	// Create save manager
	Task *psm;

	if (open_project_->filename().endsWith(QStringLiteral(".otio"),
										   Qt::CaseInsensitive)) {
#ifdef USE_OTIO
		psm = new SaveOTIOTask(open_project_);
#else
		QMessageBox::critical(
			main_window_, tr("Missing OpenTimelineIO Libraries"),
			tr("This build was compiled without OpenTimelineIO and therefore "
			   "cannot open OpenTimelineIO files."));
		return;
#endif
	} else {
		bool use_compression = !open_project_->filename().endsWith(
			QStringLiteral(".ovexml"), Qt::CaseInsensitive);
		psm = new ProjectSaveTask(open_project_, use_compression);
		static_cast<ProjectSaveTask *>(psm)->set_layout(
			main_window_->save_layout());

		if (!override_filename.isEmpty()) {
			// Set override filename if provided
			static_cast<ProjectSaveTask *>(psm)->set_override_filename(
				override_filename);
		}
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
	if (psm->start()) {
		if (override_filename.isEmpty()) {
			project_save_succeeded(psm);
		}
	}

	psm->deleteLater();
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
	if (open_project_->filename().isEmpty()) {
		QMessageBox::critical(
			main_window_, tr("Revert"),
			tr("This project has not yet been saved, therefore there is no last saved state to revert to."));
	} else {
		QString msg;

		if (by_opening_existing) {
			msg =
				tr("The project \"%1\" is already open. By re-opening it, the project will revert to "
				   "its last saved state. Any unsaved changes will be lost. Do you wish to continue?")
					.arg(open_project_->filename());
		} else {
			msg =
				tr("This will revert the project \"%1\" back to its last saved state. "
				   "All unsaved changes will be lost. Do you wish to continue?")
					.arg(open_project_->name());
		}

		if (QMessageBox::question(main_window_, tr("Revert"), msg,
								  QMessageBox::Ok | QMessageBox::Cancel) ==
			QMessageBox::Ok) {
			// Copy filename because CloseProject is going to delete `p`
			QString filename = open_project_->filename();

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

void Core::project_save_succeeded(Task *task)
{
	Project *p = static_cast<ProjectSaveTask *>(task)->get_project();

	on_project_saved(p);

	show_status_bar_message(tr("Saved to \"%1\" successfully").arg(p->filename()));
}

Project *Core::get_active_project() const
{
	return open_project_;
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
	if (open_project_->filename().isEmpty()) {
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
	QFile autorecovery_index(get_auto_recovery_index_filename());
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
			QFile::remove(get_auto_recovery_index_filename());
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
		// Keep the window's modified state in sync with the project. The
		// connection is removed automatically when the project is deleted.
		connect(p, &Project::modified_changed, main_window_,
				&QMainWindow::setWindowModified);
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

		open_project_->set_filename(fn);

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
	if (open_project_) {
		// Comparing QFileInfos will handle case insensitivity and both slash directions on platforms
		// where this is necessary (not naming any names *cough* Windows)
		if (QFileInfo(open_project_->filename()) == QFileInfo(filename)) {
			// This project is already open
			bool reverted = revert_project_internal(true);

			if (!reverted) {
				// Calling this will focus attention to the project that the user just tried to re-open
				add_open_project(open_project_);
			}

			// Don't do anything else
			return;
		}
	}

	Task *load_task;

	if (filename.endsWith(QStringLiteral(".otio"), Qt::CaseInsensitive)) {
		// Load OpenTimelineIO project
#ifdef USE_OTIO
		load_task = new LoadOTIOTask(filename);
#else
		QMessageBox::critical(
			main_window_, tr("Missing OpenTimelineIO Libraries"),
			tr("This build was compiled without OpenTimelineIO and therefore "
			   "cannot open OpenTimelineIO files."));
		return;
#endif
	} else {
		// Fallback to regular OVE project
		load_task = new ProjectLoadTask(filename);
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

bool Core::label_nodes(const QVector<Node *> &nodes, MultiUndoCommand *parent)
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
		NodeRenameCommand *rename_command = new NodeRenameCommand();

		foreach (Node *n, nodes) {
			rename_command->add_node(n, s);
		}

		if (parent) {
			parent->add_child(rename_command);
		} else {
			undo_stack()->push(rename_command,
							   tr("Renamed %1 Node(s)").arg(nodes.size()));
		}

		return true;
	}

	return false;
}

void Core::open_project_from_recent_list(int index)
{
	const QString &open_fn = get_recent_projects().at(index);

	if (QFileInfo::exists(open_fn)) {
		open_project_internal(open_fn);
	} else if (
		QMessageBox::information(
			main_window(), tr("Cannot open recent project"),
			tr("The project \"%1\" doesn't exist. Would you like to remove this file from the recent list?")
				.arg(open_fn),
			QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
		remove_recently_opened_project(index);
	}
}

bool Core::close_project(bool auto_open_new, bool ignore_modified)
{
	if (open_project_) {
		if (open_project_->is_modified() && !ignore_modified) {
			QMessageBox mb(main_window_);

			mb.setWindowModality(Qt::WindowModal);
			mb.setIcon(QMessageBox::Question);
			mb.setWindowTitle(tr("Unsaved Changes"));
			mb.setText(
				tr("The project '%1' has unsaved changes. Would you like to save them?")
					.arg(open_project_->name()));

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
		undo_stack()->clear();

		Project *tmp = open_project_;
		set_active_project(nullptr);
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

}
