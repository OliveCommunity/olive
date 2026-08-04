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

#include "coreengine.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QStandardPaths>
#include <QTextStream>

#include "audio/audiovisualwaveform.h"
#include "codec/conformmanager.h"
#include "codec/decoder.h"
#include "codec/proxymanager.h"
#include "common/filefunctions.h"
#include "config/config.h"
#include "node/color/colormanager/colormanager.h"
#include "node/factory.h"
#include "node/project/serializer/serializer.h"
#include "render/framemanager.h"
#include "render/rendermanager.h"
#include "task/project/load/loadbasetask.h"
#include "task/taskmanager.h"

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

EngineCore *EngineCore::instance_ = nullptr;

EngineCore::EngineCore(const CoreParams &params)
	: open_project_(nullptr)
	, tool_(Tool::k_pointer)
	, addable_object_(Tool::k_addable_empty)
	, snapping_(true)
	, core_params_(params)
	, magic_(false)
	, pixel_sampling_users_(0)
	, shown_cache_full_warning_(false)
{
	// Store reference to this object, making the assumption that EngineCore will only ever be made in
	// main(). This will obviously break if not.
	instance_ = this;

	translator_ = new QTranslator(this);
}

EngineCore *EngineCore::instance()
{
	return instance_;
}

QString EngineCore::footage_file_dialog_filter()
{
	return build_footage_file_dialog_filter();
}

QStringList EngineCore::allowed_footage_extensions()
{
	QStringList all = footage_video_extensions() + footage_audio_extensions() +
					  footage_image_extensions();
	all.removeDuplicates();
	return all;
}

bool EngineCore::is_footage_extension_allowed(const QString &path)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	if (ext.isEmpty()) {
		return false;
	}

	return allowed_footage_extensions().contains(ext);
}

void EngineCore::declare_types_for_qt()
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
	qRegisterMetaType<olive::SerializedLayoutInfo>();
	qRegisterMetaType<olive::RenderTicketPtr>();
}

void EngineCore::start()
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

	qInfo() << "Using Qt version:" << qVersion();

	// Start autorecovery timer using the config value as its interval
	set_autorecovery_interval(OAK_CONFIG("AutorecoveryInterval").toInt());
	connect(&autorecovery_timer_, &QTimer::timeout, this,
			&EngineCore::save_autorecovery);
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

void EngineCore::stop()
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

	TaskManager::destroy_instance();

	NodeFactory::destroy();
}

UndoStack *EngineCore::undo_stack()
{
	return &undo_stack_;
}

const Tool::Item &EngineCore::tool() const
{
	return tool_;
}

const Tool::AddableObject &EngineCore::get_selected_addable_object() const
{
	return addable_object_;
}

const QString &EngineCore::get_selected_transition() const
{
	return selected_transition_;
}

void EngineCore::set_selected_addable_object(const Tool::AddableObject &obj)
{
	addable_object_ = obj;
	emit addable_object_changed(addable_object_);
}

void EngineCore::set_selected_transition_object(const QString &obj)
{
	selected_transition_ = obj;
}

void EngineCore::clear_open_recent_list()
{
	recent_projects_.clear();
	save_recent_projects_list();
	emit open_recent_list_changed();
}

void EngineCore::create_new_project()
{
	// If we already have an empty/new project, switch to it
	bool closed = close_project_handler_ ? close_project_handler_() :
										   close_open_project_without_prompt();
	if (closed) {
		Project *p = new Project();
		p->initialize();
		add_open_project(p);
	}
}

const bool &EngineCore::snapping() const
{
	return snapping_;
}

const QStringList &EngineCore::get_recent_projects() const
{
	return recent_projects_;
}

void EngineCore::set_tool(const Tool::Item &tool)
{
	tool_ = tool;

	emit tool_changed(tool_);
}

void EngineCore::set_snapping(const bool &b)
{
	snapping_ = b;

	emit snapping_changed(snapping_);
}

void EngineCore::set_use_proxy_media(bool enabled)
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

void EngineCore::add_open_project(Project *p, bool add_to_recents)
{
	// Ensure project is not open at the moment
	if (open_project_ == p) {
		return;
	}

	// If we currently have an empty project, close it first
	if (open_project_) {
		if (close_project_handler_) {
			// The return value is intentionally ignored, preserving the
			// historical behavior of this function
			close_project_handler_();
		} else {
			close_open_project_without_prompt();
		}
	}

	set_active_project(p);

	if (!p->filename().isEmpty() && add_to_recents) {
		push_recently_opened_project(p->filename());
	}
}

bool EngineCore::add_open_project_from_task(Task *task, bool add_to_recents)
{
	ProjectLoadBaseTask *load_task = static_cast<ProjectLoadBaseTask *>(task);

	if (!load_task->is_cancelled()) {
		Project *project = load_task->get_loaded_project();

		if (validate_footage_in_loaded_project(project, project->get_saved_url())) {
			add_open_project(project, add_to_recents);

			if (load_layout_handler_) {
				load_layout_handler_(load_task->get_loaded_layout());
			}

			return true;
		} else {
			delete project;
			create_new_project();
		}
	}

	return false;
}

void EngineCore::set_active_project(Project *p)
{
	open_project_ = p;
	RenderManager::instance()->set_project(p);

	// The UI layer sets the project on the main window and tracks its
	// modified state through this signal
	emit active_project_changed(p);
}

bool EngineCore::confirm_image_sequence(const QString &filename)
{
	if (confirm_image_sequence_handler_) {
		return confirm_image_sequence_handler_(filename);
	}

	// Without a UI handler (headless), accept the image sequence
	return true;
}

#ifdef USE_OTIO
bool EngineCore::show_otio_import_dialog(const QList<Sequence *> &sequences)
{
	if (otio_import_handler_) {
		return otio_import_handler_(sequences);
	}

	// Without a UI handler (headless), accept the import
	return true;
}
#endif

void EngineCore::add_recovery_project_from_task(Task *task)
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

QString EngineCore::get_auto_recovery_index_filename()
{
	return QDir(QStandardPaths::writableLocation(
					QStandardPaths::AppLocalDataLocation))
		.filePath(QStringLiteral("unrecovered"));
}

void EngineCore::save_unrecovered_list()
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

void EngineCore::save_recent_projects_list()
{
	// Save recently opened projects
	QFile recent_projects_file(get_recent_projects_file_path());
	if (recent_projects_file.open(QFile::WriteOnly | QFile::Text)) {
		recent_projects_file.write(recent_projects_.join('\n').toUtf8());
		recent_projects_file.close();
	}
}

void EngineCore::save_autorecovery()
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

				// The actual save goes through the UI layer since it
				// involves UI state (the main window layout)
				if (save_project_handler_) {
					save_project_handler_(this_autorecovery_path);
				}

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
					if (realname_file.open(QFile::WriteOnly)) {
						realname_file.write(
							open_project_->pretty_filename().toUtf8());
						realname_file.close();
					}
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
				// The engine cannot show dialogs, report through the
				// application's registered error handler instead
				Config::report_error(
					tr("Auto-Recovery Error"),
					tr("Failed to save auto-recovery to \"%1\". "
					   "Oak Video Editor may not have permission to this directory.")
						.arg(project_autorecovery_dir.absolutePath()));
			}
		}

		// Save index
		save_unrecovered_list();
	}
}

Timecode::Display EngineCore::get_timecode_display() const
{
	return static_cast<Timecode::Display>(
		OAK_CONFIG("TimecodeDisplay").toInt());
}

void EngineCore::set_timecode_display(Timecode::Display d)
{
	OAK_CONFIG("TimecodeDisplay") = d;

	emit timecode_display_changed(d);
}

void EngineCore::set_autorecovery_interval(int minutes)
{
	// Convert minutes to milliseconds
	autorecovery_timer_.setInterval(minutes * 60000);
}

void EngineCore::copy_string_to_clipboard(const QString &s)
{
	QGuiApplication::clipboard()->setText(s);
}

QString EngineCore::paste_string_from_clipboard()
{
	return QGuiApplication::clipboard()->text();
}

QString EngineCore::get_recent_projects_file_path()
{
	return QDir(FileFunctions::get_configuration_location())
		.filePath(QStringLiteral("recent"));
}

void EngineCore::set_startup_locale()
{
	// Set language
	if (!core_params_.startup_language().isEmpty()) {
		if (translator_->load(core_params_.startup_language()) &&
			QCoreApplication::installTranslator(translator_)) {
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

void EngineCore::show_status_bar_message(const QString &s, int timeout)
{
	emit status_message_show(s, timeout);
}

void EngineCore::clear_status_bar_message()
{
	emit status_message_clear();
}

void EngineCore::request_pixel_sampling_in_viewers(bool e)
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

void EngineCore::warn_cache_full()
{
	if (!shown_cache_full_warning_) {
		shown_cache_full_warning_ = true;

		emit cache_full_warning_requested();
	}
}

void EngineCore::push_recently_opened_project(const QString &s)
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

void EngineCore::on_project_saved(Project *p)
{
	push_recently_opened_project(p->filename());

	p->set_modified(false);

	autorecovered_projects_.removeOne(p->get_uuid());
	save_unrecovered_list();
}

void EngineCore::remove_recently_opened_project(int index)
{
	recent_projects_.removeAt(index);

	save_recent_projects_list();

	emit open_recent_list_changed();
}

int EngineCore::count_files_in_file_list(const QFileInfoList &filenames)
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

Sequence *EngineCore::create_new_sequence_for_project(const QString &format,
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

bool EngineCore::validate_footage_in_loaded_project(Project *project,
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
		// Let the UI layer offer to relink the missing footage
		if (relink_handler_ && !relink_handler_(footage_we_couldnt_validate)) {
			return false;
		}
	}

	return true;
}

bool EngineCore::set_language(const QString &locale)
{
	QCoreApplication::removeTranslator(translator_);

	QString resource_path = QStringLiteral(":/ts/%1").arg(locale);
	if (translator_->load(resource_path) &&
		QCoreApplication::installTranslator(translator_)) {
		return true;
	}

	return false;
}

bool EngineCore::close_open_project_without_prompt()
{
	if (open_project_) {
		// For safety, the undo stack is cleared so no commands try to affect a freed project
		undo_stack_.clear();

		Project *tmp = open_project_;
		set_active_project(nullptr);
		delete tmp;
	}

	return true;
}

void EngineCore::set_confirm_image_sequence_handler(
	ConfirmImageSequenceHandler handler)
{
	confirm_image_sequence_handler_ = std::move(handler);
}

void EngineCore::set_relink_handler(FootageRelinkHandler handler)
{
	relink_handler_ = std::move(handler);
}

void EngineCore::set_save_project_handler(SaveProjectHandler handler)
{
	save_project_handler_ = std::move(handler);
}

void EngineCore::set_close_project_handler(CloseProjectHandler handler)
{
	close_project_handler_ = std::move(handler);
}

void EngineCore::set_load_layout_handler(LoadLayoutHandler handler)
{
	load_layout_handler_ = std::move(handler);
}

#ifdef USE_OTIO
void EngineCore::set_otio_import_handler(OtioImportHandler handler)
{
	otio_import_handler_ = std::move(handler);
}
#endif

EngineCore::CoreParams::CoreParams()
	: mode_(k_run_normal)
	, run_fullscreen_(false)
	, crash_(false)
{
}

}
