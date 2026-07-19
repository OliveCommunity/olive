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
#include <QClipboard>
#include <QDebug>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyleFactory>
#include "window/mainwindow/mainwindowundo.h"
#ifdef Q_OS_WINDOWS
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtPlatformHeaders/QWindowsWindowFunctions>
#endif
#endif

#include "audio/audiomanager.h"
#include "cli/clitask/clitaskdialog.h"
#include "codec/conformmanager.h"
#include "codec/proxymanager.h"
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
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/nodeundo.h"
#include "node/project/serializer/serializer.h"
#include "panel/panelmanager.h"
#include "panel/project/project.h"
#include "panel/viewer/viewer.h"
#include "render/diskmanager.h"
#include "render/framemanager.h"
#include "render/rendermanager.h"
#ifdef USE_OTIO
#include "task/project/loadotio/loadotio.h"
#include "task/project/saveotio/saveotio.h"
#endif
#include "task/project/import/import.h"
#include "dialog/projectimport/projectimporterrordialog.h"
#include "task/project/load/load.h"
#include "task/project/save/save.h"
#include "task/taskmanager.h"
#include "ui/style/style.h"
#include "undo/undostack.h"
#include "widget/menu/menushared.h"
#include "window/mainwindow/mainwindow.h"

namespace
{

QStringList footage_video_extensions()
{
	return QStringList{
		QStringLiteral("mp4"),	QStringLiteral("mov"), QStringLiteral("m4v"),
		QStringLiteral("avi"),	QStringLiteral("mpg"), QStringLiteral("mpeg"),
		QStringLiteral("m2ts"), QStringLiteral("mts"), QStringLiteral("ts"),
		QStringLiteral("webm"), QStringLiteral("wmv"), QStringLiteral("flv"),
		QStringLiteral("3gp"),	QStringLiteral("3g2"), QStringLiteral("mxf")
	};
}

QStringList footage_audio_extensions()
{
	return QStringList{ QStringLiteral("wav"),	QStringLiteral("mp3"),
						QStringLiteral("flac"), QStringLiteral("aac"),
						QStringLiteral("ogg"),	QStringLiteral("opus"),
						QStringLiteral("m4a"),	QStringLiteral("alac"),
						QStringLiteral("aif"),	QStringLiteral("aiff"),
						QStringLiteral("aifc"), QStringLiteral("wma") };
}

QStringList footage_image_extensions()
{
	return QStringList{ QStringLiteral("png"),	QStringLiteral("jpg"),
						QStringLiteral("jpeg"), QStringLiteral("tif"),
						QStringLiteral("tiff"), QStringLiteral("bmp"),
						QStringLiteral("gif"),	QStringLiteral("exr"),
						QStringLiteral("dpx"),	QStringLiteral("webp") };
}

QString build_footage_filter_group(const QString &label,
								const QStringList &extensions)
{
	QStringList patterns;
	patterns.reserve(extensions.size());
	for (const QString &ext : extensions) {
		patterns.append(QStringLiteral("*.%1").arg(ext));
	}

	return QStringLiteral("%1 (%2)").arg(label,
										 patterns.join(QLatin1Char(' ')));
}

QString build_footage_file_dialog_filter()
{
	QStringList all = footage_video_extensions() + footage_audio_extensions() +
					  footage_image_extensions();
	all.removeDuplicates();

	QStringList groups;
	groups << build_footage_filter_group(QObject::tr("Common Media Files"), all);
	groups << build_footage_filter_group(QObject::tr("Video Files"),
									  footage_video_extensions());
	groups << build_footage_filter_group(QObject::tr("Audio Files"),
									  footage_audio_extensions());
	groups << build_footage_filter_group(QObject::tr("Image Files"),
									  footage_image_extensions());

	return groups.join(QStringLiteral(";;"));
}

} // namespace

namespace olive
{

Core *Core::instance_ = nullptr;

Core::Core(const CoreParams &params)
	: main_window_(nullptr)
	, open_project_(nullptr)
	, tool_(Tool::k_pointer)
	, addable_object_(Tool::k_addable_empty)
	, snapping_(true)
	, core_params_(params)
	, magic_(false)
	, pixel_sampling_users_(0)
	, shown_cache_full_warning_(false)
{
	// Store reference to this object, making the assumption that Core will only ever be made in
	// main(). This will obviously break if not.
	instance_ = this;

	translator_ = new QTranslator(this);
}

Core *Core::instance()
{
	return instance_;
}

QString Core::footage_file_dialog_filter()
{
	return build_footage_file_dialog_filter();
}

QStringList Core::allowed_footage_extensions()
{
	QStringList all = footage_video_extensions() + footage_audio_extensions() +
					  footage_image_extensions();
	all.removeDuplicates();
	return all;
}

bool Core::is_footage_extension_allowed(const QString &path)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	if (ext.isEmpty()) {
		return false;
	}

	return allowed_footage_extensions().contains(ext);
}

void Core::declare_types_for_qt()
{
	qRegisterMetaType<olive::core::Rational>();
	qRegisterMetaType<NodeValue>();
	qRegisterMetaType<NodeValueTable>();
	qRegisterMetaType<NodeValueDatabase>();
	qRegisterMetaType<FramePtr>();
	qRegisterMetaType<SampleBuffer>();
	qRegisterMetaType<AudioParams>();
	qRegisterMetaType<NodeKeyframe::Type>();
	qRegisterMetaType<Decoder::RetrieveState>();
	qRegisterMetaType<olive::core::TimeRange>();
	qRegisterMetaType<olive::core::Color>();
	qRegisterMetaType<olive::AudioVisualWaveform>();
	qRegisterMetaType<olive::VideoParams>();
	qRegisterMetaType<olive::VideoParams::Interlacing>();
	qRegisterMetaType<olive::MainWindowLayoutInfo>();
	qRegisterMetaType<olive::RenderTicketPtr>();
}

void Core::start()
{
	// Load application config
	Config::load();

	// Set locale based on either startup arg, config, or auto-detect
	set_startup_locale();

	// Declare custom types for Qt signal/slot system
	declare_types_for_qt();

	// Set up node factory/library
	NodeFactory::initialize();

	// Set up color manager's default config
	ColorManager::set_up_default_config();

	// Initialize task manager
	TaskManager::create_instance();

	// Initialize ConformManager
	ConformManager::create_instance();

	// Initialize ProxyManager
	ProxyManager::create_instance();

	// Initialize RenderManager
	RenderManager::create_instance();

	// Initialize FrameManager
	FrameManager::create_instance();

	// Initialize project serializers
	ProjectSerializer::initialize();

	//
	// Start application
	//

	qInfo() << "Using Qt version:" << qVersion();

	switch (core_params_.run_mode()) {
	case CoreParams::k_run_normal:
		// Start GUI
		start_gui(core_params_.fullscreen());

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

	// Manual crash triggering
	if (core_params_.crash_on_startup()) {
		const int interval = 5000;
		qInfo() << "Manual crash was triggered. Application will crash in"
				<< interval << "ms";
		QTimer *crash_timer = new QTimer(this);
		crash_timer->setInterval(interval);
		connect(crash_timer, &QTimer::timeout, this, [] { abort(); });
		crash_timer->start();
	}
}

void Core::stop()
{
	// Assume all projects have closed gracefully and no auto-recovery is necessary
	autorecovered_projects_.clear();
	save_unrecovered_list();

	// Save Config
	Config::save();

	ProjectSerializer::destroy();

	ConformManager::destroy_instance();

	ProxyManager::destroy_instance();

	FrameManager::destroy_instance();

	RenderManager::destroy_instance();

	MenuShared::destroy_instance();

	TaskManager::destroy_instance();

	PanelManager::destroy_instance();

	AudioManager::destroy_instance();

	DiskManager::destroy_instance();

	NodeFactory::destroy();

	delete main_window_;
	main_window_ = nullptr;
}

MainWindow *Core::main_window()
{
	return main_window_;
}

UndoStack *Core::undo_stack()
{
	return &undo_stack_;
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

const Tool::Item &Core::tool() const
{
	return tool_;
}

const Tool::AddableObject &Core::get_selected_addable_object() const
{
	return addable_object_;
}

const QString &Core::get_selected_transition() const
{
	return selected_transition_;
}

void Core::set_selected_addable_object(const Tool::AddableObject &obj)
{
	addable_object_ = obj;
	emit addable_object_changed(addable_object_);
}

void Core::set_selected_transition_object(const QString &obj)
{
	selected_transition_ = obj;
}

void Core::clear_open_recent_list()
{
	recent_projects_.clear();
	save_recent_projects_list();
	emit open_recent_list_changed();
}

void Core::create_new_project()
{
	// If we already have an empty/new project, switch to it
	if (close_project(false)) {
		Project *p = new Project();
		p->initialize();
		add_open_project(p);
	}
}

const bool &Core::snapping() const
{
	return snapping_;
}

const QStringList &Core::get_recent_projects() const
{
	return recent_projects_;
}

void Core::set_tool(const Tool::Item &tool)
{
	tool_ = tool;

	emit tool_changed(tool_);
}

void Core::set_snapping(const bool &b)
{
	snapping_ = b;

	emit snapping_changed(snapping_);
}

void Core::set_use_proxy_media(bool enabled)
{
	Config::current()[QStringLiteral("UseProxyMedia")] = enabled;

	// Invalidate all footage so viewers re-evaluate with the new proxy state
	if (open_project_) {
		for (Node *n : open_project_->nodes()) {
			if (Footage *footage = dynamic_cast<Footage *>(n)) {
				footage->invalidate_all(Footage::k_filename_input);
			}
		}
	}
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
	Project *active_project = GetActiveProject();
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

void Core::add_open_project(Project *p, bool add_to_recents)
{
	// Ensure project is not open at the moment
	if (open_project_ == p) {
		return;
	}

	// If we currently have an empty project, close it first
	if (open_project_) {
		close_project(false);
	}

	set_active_project(p);

	if (!p->filename().isEmpty() && add_to_recents) {
		push_recently_opened_project(p->filename());
	}
}

bool Core::add_open_project_from_task(Task *task, bool add_to_recents)
{
	ProjectLoadBaseTask *load_task = static_cast<ProjectLoadBaseTask *>(task);

	if (!load_task->is_cancelled()) {
		Project *project = load_task->get_loaded_project();

		if (validate_footage_in_loaded_project(project, project->get_saved_url())) {
			add_open_project(project, add_to_recents);
			main_window_->load_layout(load_task->get_loaded_layout());

			return true;
		} else {
			delete project;
			create_new_project();
		}
	}

	return false;
}

void Core::set_active_project(Project *p)
{
	if (open_project_) {
		disconnect(open_project_, &Project::modified_changed, this,
				   &Core::project_was_modified);
	}

	open_project_ = p;
	RenderManager::instance()->set_project(p);
	main_window_->set_project(p);

	if (open_project_) {
		connect(open_project_, &Project::modified_changed, this,
				&Core::project_was_modified);
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

	undo_stack_.push(
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

void Core::project_was_modified(bool e)
{
	main_window_->setWindowModified(e);
}

bool Core::start_headless_export()
{
	const QString &startup_project = core_params_.startup_project();

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
	const QString &startup_project = core_params_.startup_project();
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

void Core::add_recovery_project_from_task(Task *task)
{
	if (add_open_project_from_task(task, false)) {
		ProjectLoadBaseTask *load_task =
			static_cast<ProjectLoadBaseTask *>(task);

		Project *project = load_task->get_loaded_project();

		// Clearing the filename will force the user to re-save it somewhere else
		project->set_filename(QString());

		// Forcing a UUID regeneration will prevent it from saving auto-recoveries in the same place
		// the original project did
		project->regenerate_uuid();

		// Setting modified will ensure that the program doesn't close and lose the project without
		// prompting the user first
		project->set_modified(true);
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

	// Start autorecovery timer using the config value as its interval
	set_autorecovery_interval(OAK_CONFIG("AutorecoveryInterval").toInt());
	connect(&autorecovery_timer_, &QTimer::timeout, this,
			&Core::save_autorecovery);
	autorecovery_timer_.start();

	// Load recently opened projects list
	{
		QFile recent_projects_file(get_recent_projects_file_path());
		if (recent_projects_file.open(QFile::ReadOnly | QFile::Text)) {
			QString r = QString::fromUtf8(recent_projects_file.readAll());
			if (!r.isEmpty()) {
				recent_projects_ = r.split('\n');
			}
			recent_projects_file.close();
		}

		emit open_recent_list_changed();
	}
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

QString Core::get_auto_recovery_index_filename()
{
	return QDir(QStandardPaths::writableLocation(
					QStandardPaths::AppLocalDataLocation))
		.filePath(QStringLiteral("unrecovered"));
}

void Core::save_unrecovered_list()
{
	QFile autorecovery_index(get_auto_recovery_index_filename());

	if (autorecovered_projects_.isEmpty()) {
		// Recovery list is empty, delete file if exists
		if (autorecovery_index.exists()) {
			autorecovery_index.remove();
		}
	} else if (autorecovery_index.open(QFile::WriteOnly)) {
		// Overwrite recovery list with current list
		QTextStream ts(&autorecovery_index);

		bool first = true;
		foreach (const QUuid &uuid, autorecovered_projects_) {
			if (first) {
				first = false;
			} else {
				ts << QStringLiteral("\n");
			}
			ts << uuid.toString();
		}

		autorecovery_index.close();
	} else {
		qWarning() << "Failed to save unrecovered list";
	}
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

void Core::save_recent_projects_list()
{
	// Save recently opened projects
	QFile recent_projects_file(get_recent_projects_file_path());
	if (recent_projects_file.open(QFile::WriteOnly | QFile::Text)) {
		recent_projects_file.write(recent_projects_.join('\n').toUtf8());
		recent_projects_file.close();
	}
}

void Core::save_autorecovery()
{
	if (OAK_CONFIG("AutorecoveryEnabled").toBool()) {
		if (open_project_ && !open_project_->has_autorecovery_been_saved()) {
			QDir project_autorecovery_dir(
				QDir(FileFunctions::get_auto_recovery_root())
					.filePath(open_project_->get_uuid().toString()));
			if (FileFunctions::directory_is_valid(project_autorecovery_dir)) {
				QString this_autorecovery_path =
					project_autorecovery_dir.filePath(
						QStringLiteral("%1.ove").arg(QString::number(
							QDateTime::currentSecsSinceEpoch())));

				save_project_internal(this_autorecovery_path);

				open_project_->set_autorecovery_saved(true);

				// Keep track of projects that where the "newest" save is the recovery project
				if (!autorecovered_projects_.contains(
						open_project_->get_uuid())) {
					autorecovered_projects_.append(open_project_->get_uuid());
				}

				qDebug() << "Saved auto-recovery to:" << this_autorecovery_path;

				// Write human-readable real name so it's not just a UUID
				{
					QFile realname_file(project_autorecovery_dir.filePath(
						QStringLiteral("realname.txt")));
					realname_file.open(QFile::WriteOnly);
					realname_file.write(
						open_project_->pretty_filename().toUtf8());
					realname_file.close();
				}

				int64_t max_recoveries_per_file =
					OAK_CONFIG("AutorecoveryMaximum").toLongLong();

				// Since we write an extra file, increment total allowed files by 1
				max_recoveries_per_file++;

				// Delete old entries
				QStringList recovery_files = project_autorecovery_dir.entryList(
					QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
				while (recovery_files.size() > max_recoveries_per_file) {
					bool deleted = false;
					for (int i = 0; i < recovery_files.size(); i++) {
						const QString &f = recovery_files.at(i);

						if (f.endsWith(QStringLiteral(".ove"),
									   Qt::CaseInsensitive)) {
							QString delete_full_path =
								project_autorecovery_dir.filePath(f);
							qDebug()
								<< "Deleted old recovery:" << delete_full_path;
							QFile::remove(delete_full_path);
							recovery_files.removeAt(i);
							deleted = true;
							break;
						}
					}

					if (!deleted) {
						// For some reason none of the files were deletable. Break so we don't end up in
						// an infinite loop.
						break;
					}
				}
			} else {
				QMessageBox::critical(
					main_window_, tr("Auto-Recovery Error"),
					tr("Failed to save auto-recovery to \"%1\". "
					   "Oak Video Editor may not have permission to this directory.")
						.arg(project_autorecovery_dir.absolutePath()));
			}
		}

		// Save index
		save_unrecovered_list();
	}
}

void Core::project_save_succeeded(Task *task)
{
	Project *p = static_cast<ProjectSaveTask *>(task)->get_project();

	push_recently_opened_project(p->filename());

	p->set_modified(false);

	autorecovered_projects_.removeOne(p->get_uuid());
	save_unrecovered_list();

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

Timecode::Display Core::get_timecode_display() const
{
	return static_cast<Timecode::Display>(
		OAK_CONFIG("TimecodeDisplay").toInt());
}

void Core::set_timecode_display(Timecode::Display d)
{
	OAK_CONFIG("TimecodeDisplay") = d;

	emit timecode_display_changed(d);
}

void Core::set_autorecovery_interval(int minutes)
{
	// Convert minutes to milliseconds
	autorecovery_timer_.setInterval(minutes * 60000);
}

void Core::copy_string_to_clipboard(const QString &s)
{
	QGuiApplication::clipboard()->setText(s);
}

QString Core::paste_string_from_clipboard()
{
	return QGuiApplication::clipboard()->text();
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

QString Core::get_recent_projects_file_path()
{
	return QDir(FileFunctions::get_configuration_location())
		.filePath(QStringLiteral("recent"));
}

void Core::set_startup_locale()
{
	// Set language
	if (!core_params_.startup_language().isEmpty()) {
		if (translator_->load(core_params_.startup_language()) &&
			QApplication::installTranslator(translator_)) {
			return;
		} else {
			qWarning()
				<< "Failed to load translation file. Falling back to defaults.";
		}
	}

	QString use_locale = OAK_CONFIG("Language").toString();

	if (use_locale.isEmpty()) {
		// No configured locale, auto-detect the system's locale
		use_locale = QLocale::system().name();
	}

	if (!set_language(use_locale)) {
		qWarning() << "Trying to use locale" << use_locale
				   << "but couldn't find a translation for it";
	}
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

void Core::show_status_bar_message(const QString &s, int timeout)
{
	// The main window only exists after StartGUI(); in tests and other
	// contexts that construct Core without a window, do nothing.
	if (main_window_) {
		main_window_->statusBar()->showMessage(s, timeout);
	}
}

void Core::clear_status_bar_message()
{
	main_window_->statusBar()->clearMessage();
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

void Core::request_pixel_sampling_in_viewers(bool e)
{
	if (e) {
		if (pixel_sampling_users_ == 0) {
			// Signal to start pixel sampling
			emit color_picker_enabled(true);
		}

		pixel_sampling_users_++;
	} else {
		pixel_sampling_users_--;

		if (pixel_sampling_users_ == 0) {
			// Signal to end pixel sampling
			emit color_picker_enabled(false);
		}
	}
}

void Core::warn_cache_full()
{
	if (!shown_cache_full_warning_ && main_window_) {
		shown_cache_full_warning_ = true;

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

void Core::push_recently_opened_project(const QString &s)
{
	if (s.isEmpty()) {
		return;
	}

	int existing_index = recent_projects_.indexOf(s);

	if (existing_index >= 0) {
		recent_projects_.move(existing_index, 0);
	} else {
		recent_projects_.prepend(s);

		const int k_maximum_recent_projects = 10;
		while (recent_projects_.size() > k_maximum_recent_projects) {
			recent_projects_.removeLast();
		}
	}

	save_recent_projects_list();

	emit open_recent_list_changed();
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

int Core::count_files_in_file_list(const QFileInfoList &filenames)
{
	int file_count = 0;

	foreach (const QFileInfo &f, filenames) {
		// For some reason QDir::NoDotAndDotDot	doesn't work with entryInfoList, so we have to check manually
		if (f.fileName() == "." || f.fileName() == "..") {
			continue;
		} else if (f.isDir()) {
			QFileInfoList info_list =
				QDir(f.absoluteFilePath()).entryInfoList();

			file_count += count_files_in_file_list(info_list);
		} else {
			file_count++;
		}
	}

	return file_count;
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
			undo_stack_.push(rename_command,
							 tr("Renamed %1 Node(s)").arg(nodes.size()));
		}

		return true;
	}

	return false;
}

Sequence *Core::create_new_sequence_for_project(const QString &format,
											Project *project)
{
	Sequence *new_sequence = new Sequence();

	// Get default name for this sequence (in the format "Sequence N", the first that doesn't exist)
	int sequence_number = 1;
	QString sequence_name;
	do {
		sequence_name = format.arg(sequence_number);
		sequence_number++;
	} while (project->root()->child_exists_with_name(sequence_name));
	new_sequence->set_label(sequence_name);

	return new_sequence;
}

void Core::open_project_from_recent_list(int index)
{
	const QString &open_fn = recent_projects_.at(index);

	if (QFileInfo::exists(open_fn)) {
		open_project_internal(open_fn);
	} else if (
		QMessageBox::information(
			main_window(), tr("Cannot open recent project"),
			tr("The project \"%1\" doesn't exist. Would you like to remove this file from the recent list?")
				.arg(open_fn),
			QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
		recent_projects_.removeAt(index);

		save_recent_projects_list();

		emit open_recent_list_changed();
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
		undo_stack_.clear();

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

QString strip_windows_drive_letter(QString s)
{
	// HACK: On Windows, absolute paths are saved with a drive letter (e.g. "C:\video.mp4"). Below,
	//       we use Qt's relative path system to resolve when an entire project may be in a different
	//       folder, but the files are all in the same place relatively to the project. Unfortunately,
	//       Qt chooses not to understand paths from Windows on non-Windows platforms, which causes
	//       this to break when a project is moving from Windows to non-Windows. To resolve that, if
	//       we're on a non-Windows platform and we detect a Windows path (i.e. a path with a drive
	//       letter at the start), we strip it off. We also convert any back-slashes to forward-slashes
	//       because on Windows they are interchangeable and on non-Windows they are not.
#ifndef Q_OS_WINDOWS
	if (s.size() >= 2) {
		if (s.at(0).isLetter() && s.at(1) == ':') {
			s = s.mid(2);
			s.replace('\\', '/');
		}
	}
#endif

	return s;
}

bool Core::validate_footage_in_loaded_project(Project *project,
										  const QString &project_saved_url)
{
	QVector<Footage *> footage_we_couldnt_validate;

	for (Node *n : project->nodes()) {
		if (Footage *footage = dynamic_cast<Footage *>(n)) {
			QString footage_fn = strip_windows_drive_letter(footage->filename());
			QString project_fn = strip_windows_drive_letter(project_saved_url);

			if (!QFileInfo::exists(footage_fn) &&
				!project_saved_url.isEmpty()) {
				// If the footage doesn't exist, it might have moved with the project
				const QString &project_current_url = project->filename();

				if (project_current_url != project_fn) {
					// Project has definitely moved, try to resolve relative paths
					QDir saved_dir(QFileInfo(project_fn).dir());
					QDir true_dir(QFileInfo(project_current_url).dir());

					QString relative_filename =
						saved_dir.relativeFilePath(footage_fn);
					QString transformed_abs_filename =
						true_dir.filePath(relative_filename);

					if (QFileInfo::exists(transformed_abs_filename)) {
						// Use this file instead
						qInfo() << "Resolved" << footage_fn << "relatively to"
								<< transformed_abs_filename;
						footage->set_filename(transformed_abs_filename);
					}
				}
			}

			if (QFileInfo::exists(footage->filename())) {
				// Assume valid
				footage->set_valid();
			} else {
				footage_we_couldnt_validate.append(footage);
			}
		}
	}

	if (!footage_we_couldnt_validate.isEmpty()) {
		FootageRelinkDialog frd(footage_we_couldnt_validate, main_window_);
		if (frd.exec() == QDialog::Rejected) {
			return false;
		}
	}

	return true;
}

bool Core::set_language(const QString &locale)
{
	QApplication::removeTranslator(translator_);

	QString resource_path = QStringLiteral(":/ts/%1").arg(locale);
	if (translator_->load(resource_path) &&
		QApplication::installTranslator(translator_)) {
		return true;
	}

	return false;
}

void Core::open_project()
{
	QString file = QFileDialog::getOpenFileName(
		main_window_, tr("Open Project"), QString(), get_project_filter(true));

	if (!file.isEmpty()) {
		open_project_internal(file);
	}
}

Core::CoreParams::CoreParams()
	: mode_(k_run_normal)
	, run_fullscreen_(false)
	, crash_(false)
{
}

}
