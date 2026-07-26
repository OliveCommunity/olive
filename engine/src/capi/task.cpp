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

#include "oakengine/task.h"

#include <cstring>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "node/project.h"
#include "node/project/folder/folder.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializedlayoutinfo.h"
#include "oakengine/exporter.h"
#include "oakengine/footage.h"
#include "task/project/import/import.h"
#include "task/project/load/load.h"
#include "task/project/save/save.h"
#include "cli/clitask/clitaskdialog.h"
#include "task/task.h"
#include "task/taskmanager.h"

#ifdef USE_OTIO
#include "task/project/loadotio/loadotio.h"
#include "task/project/saveotio/saveotio.h"
#endif

namespace
{

olive::Task *impl(OakEngineTask *h)
{
	return reinterpret_cast<olive::Task *>(h);
}

OakEngineTask *wrap(olive::Task *t)
{
	return reinterpret_cast<OakEngineTask *>(t);
}

// buf/size string writer (same convention as capi/project.cpp).
int write_string(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf8 = s.toUtf8();
	const int len = int(utf8.size());
	if (buf && buf_size > 0) {
		const int n = qMin(len, buf_size - 1);
		std::memcpy(buf, utf8.constData(), size_t(n));
		buf[n] = '\0';
	}
	return len;
}

/**
 * @brief Proxy-generation task driven by the footage C ABI facade
 *
 * Moved verbatim from the application's FacadeProxyTask
 * (app/widget/projectexplorer/projectexplorer.cpp): the transcode and its
 * synchronous wait live behind oakengine_footage_proxy_generate() (which
 * records the proxy state on the footage and invalidates it), while the
 * task itself queues on the TaskManager like any other task.
 */
class FacadeProxyTask : public olive::Task {
public:
	explicit FacadeProxyTask(olive::Footage *footage)
		: footage_(footage)
	{
		set_title(tr("Generating proxy for \"%1\"")
					  .arg(footage->get_label_or_name()));
	}

protected:
	virtual bool run() override
	{
		OakEngineFootage *handle = oakengine_footage_borrow(
			reinterpret_cast<OakEngineNode *>(footage_));
		const int rc = oakengine_footage_proxy_generate(handle);
		oakengine_footage_free(handle);
		if (rc != OAKENGINE_OK) {
			char err[512];
			err[0] = '\0';
			oakengine_footage_last_error(err, sizeof(err));
			set_error(err[0] ? QString::fromUtf8(err) :
							   tr("Proxy generation failed"));
			return false;
		}
		return true;
	}

private:
	olive::Footage *footage_;
};

/**
 * @brief Export task driven by the export C ABI facade
 *
 * Moved verbatim from the application's FacadeExportTask
 * (app/dialog/export/export.cpp): runs oakengine_export_render_with_
 * params() synchronously on the task thread, forwards its progress
 * callback to the task's progress_changed signal and cancels the engine
 * render when the task is cancelled.
 */
class FacadeExportTask : public olive::Task {
public:
	// Takes ownership of `params`.
	FacadeExportTask(olive::Sequence *sequence,
					 OakEngineEncodingParams *params)
		: sequence_(reinterpret_cast<OakEngineSequence *>(sequence))
		, params_(params)
	{
		set_title(tr("Exporting \"%1\"").arg(sequence->get_label()));
	}

	~FacadeExportTask() override
	{
		oakengine_encoding_params_destroy(params_);
	}

protected:
	virtual bool run() override
	{
		oakengine_export_set_progress_callback(
			&FacadeExportTask::forward_progress, this);
		const int rc = oakengine_export_render_with_params(sequence_, params_);
		oakengine_export_set_progress_callback(nullptr, nullptr);

		if (rc == OAKENGINE_E_CANCELLED) {
			// Mirror the engine render's cancelled state on the task.
			cancel();
			return false;
		}
		if (rc != OAKENGINE_OK) {
			char err[1024];
			err[0] = '\0';
			oakengine_export_last_error(err, sizeof(err));
			set_error(err[0] ? QString::fromUtf8(err) :
							   QStringLiteral("Export failed"));
			return false;
		}
		return true;
	}

	virtual void CancelEvent() override
	{
		oakengine_export_cancel();
	}

private:
	static void forward_progress(double fraction, void *userdata)
	{
		static_cast<FacadeExportTask *>(userdata)->emit_progress(fraction);
	}

	void emit_progress(double fraction)
	{
		emit progress_changed(fraction);
	}

	OakEngineSequence *sequence_;
	OakEngineEncodingParams *params_;
};

olive::ProjectImportTask *as_import(olive::Task *t)
{
	return dynamic_cast<olive::ProjectImportTask *>(t);
}

olive::ProjectSaveTask *as_save(olive::Task *t)
{
	return dynamic_cast<olive::ProjectSaveTask *>(t);
}

} // namespace

/* ---- Global task manager ------------------------------------------------- */

extern "C" void *oakengine_task_manager_handle(void)
{
	return olive::TaskManager::instance();
}

extern "C" int oakengine_task_manager_count(void)
{
	olive::TaskManager *m = olive::TaskManager::instance();
	return m ? m->get_task_count() : OAKENGINE_E_INVALID;
}

extern "C" OakEngineTask *oakengine_task_manager_first(void)
{
	olive::TaskManager *m = olive::TaskManager::instance();
	if (!m || m->get_task_count() == 0) {
		return nullptr;
	}
	return wrap(m->get_first_task());
}

extern "C" int oakengine_task_manager_add(OakEngineTask *task)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	olive::TaskManager *m = olive::TaskManager::instance();
	if (!m) {
		return OAKENGINE_E_STATE;
	}
	m->add_task(impl(task));
	return OAKENGINE_OK;
}

extern "C" int oakengine_task_manager_cancel(OakEngineTask *task)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	olive::TaskManager *m = olive::TaskManager::instance();
	if (!m) {
		return OAKENGINE_E_STATE;
	}
	m->cancel_task(impl(task));
	return OAKENGINE_OK;
}

/* ---- Task accessors ------------------------------------------------------ */

extern "C" int oakengine_task_title(OakEngineTask *task, char *buf,
									int buf_size)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	return write_string(impl(task)->get_title(), buf, buf_size);
}

extern "C" int oakengine_task_error(OakEngineTask *task, char *buf,
									int buf_size)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	return write_string(impl(task)->get_error(), buf, buf_size);
}

extern "C" int64_t oakengine_task_start_time(OakEngineTask *task)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	return impl(task)->get_start_time();
}

extern "C" int oakengine_task_is_cancelled(OakEngineTask *task)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	return impl(task)->is_cancelled() ? 1 : 0;
}

extern "C" int oakengine_task_cancel(OakEngineTask *task)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	impl(task)->Cancel();
	return OAKENGINE_OK;
}

extern "C" int oakengine_task_start_sync(OakEngineTask *task)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	return impl(task)->start() ? 1 : 0;
}

extern "C" int oakengine_task_free(OakEngineTask *task)
{
	if (!task) {
		return OAKENGINE_E_INVALID;
	}
	delete impl(task);
	return OAKENGINE_OK;
}

/* ---- Task creators -------------------------------------------------------- */

extern "C" OakEngineTask *
oakengine_task_create_project_load(const char *filename)
{
	if (!filename) {
		return nullptr;
	}
	return wrap(new olive::ProjectLoadTask(QString::fromUtf8(filename)));
}

extern "C" OakEngineTask *
oakengine_task_create_project_load_otio(const char *filename)
{
	if (!filename) {
		return nullptr;
	}
#ifdef USE_OTIO
	return wrap(new olive::LoadOTIOTask(QString::fromUtf8(filename)));
#else
	return nullptr;
#endif
}

extern "C" OakEngineTask *oakengine_task_create_project_save(
	OakEngineProject *project, int use_compression,
	const char *override_filename, const void *layout)
{
	auto *p = reinterpret_cast<olive::Project *>(project);
	if (!p) {
		return nullptr;
	}
	auto *task =
		new olive::ProjectSaveTask(p, use_compression != 0);
	if (layout) {
		task->set_layout(
			*static_cast<const olive::SerializedLayoutInfo *>(layout));
	}
	if (override_filename) {
		task->set_override_filename(QString::fromUtf8(override_filename));
	}
	return wrap(task);
}

extern "C" OakEngineTask *
oakengine_task_create_project_save_otio(OakEngineProject *project)
{
	auto *p = reinterpret_cast<olive::Project *>(project);
	if (!p) {
		return nullptr;
	}
#ifdef USE_OTIO
	return wrap(new olive::SaveOTIOTask(p));
#else
	return nullptr;
#endif
}

extern "C" OakEngineTask *oakengine_task_create_project_import(
	OakEngineNode *folder, const char **urls, int url_count)
{
	auto *f = dynamic_cast<olive::Folder *>(
		reinterpret_cast<olive::Node *>(folder));
	if (!f || !urls || url_count <= 0) {
		return nullptr;
	}
	QStringList list;
	list.reserve(url_count);
	for (int i = 0; i < url_count; i++) {
		if (!urls[i]) {
			return nullptr;
		}
		list.append(QString::fromUtf8(urls[i]));
	}
	return wrap(new olive::ProjectImportTask(f, list));
}

extern "C" OakEngineTask *
oakengine_task_create_proxy(OakEngineNode *footage)
{
	auto *f = dynamic_cast<olive::Footage *>(
		reinterpret_cast<olive::Node *>(footage));
	if (!f) {
		return nullptr;
	}
	return wrap(new FacadeProxyTask(f));
}

extern "C" OakEngineTask *oakengine_task_create_export(
	OakEngineSequence *sequence, OakEngineEncodingParams *params)
{
	auto *s = dynamic_cast<olive::Sequence *>(
		reinterpret_cast<olive::Node *>(sequence));
	if (!s || !params) {
		return nullptr;
	}
	return wrap(new FacadeExportTask(s, params));
}

/* ---- Import task results -------------------------------------------------- */

extern "C" int oakengine_task_import_file_count(OakEngineTask *task)
{
	olive::ProjectImportTask *t = task ? as_import(impl(task)) : nullptr;
	return t ? t->get_file_count() : OAKENGINE_E_INVALID;
}

extern "C" void *oakengine_task_import_get_command(OakEngineTask *task)
{
	olive::ProjectImportTask *t = task ? as_import(impl(task)) : nullptr;
	return t ? static_cast<void *>(t->take_command()) : nullptr;
}

extern "C" int oakengine_task_import_footage_count(OakEngineTask *task)
{
	olive::ProjectImportTask *t = task ? as_import(impl(task)) : nullptr;
	return t ? t->get_imported_footage().size() : OAKENGINE_E_INVALID;
}

extern "C" OakEngineNode *
oakengine_task_import_footage_at(OakEngineTask *task, int index)
{
	olive::ProjectImportTask *t = task ? as_import(impl(task)) : nullptr;
	if (!t || index < 0 || index >= t->get_imported_footage().size()) {
		return nullptr;
	}
	return reinterpret_cast<OakEngineNode *>(
		t->get_imported_footage().at(index));
}

extern "C" int oakengine_task_import_invalid_files_count(OakEngineTask *task)
{
	olive::ProjectImportTask *t = task ? as_import(impl(task)) : nullptr;
	return t ? t->get_invalid_files().size() : OAKENGINE_E_INVALID;
}

extern "C" int oakengine_task_import_invalid_file_at(OakEngineTask *task,
													 int index, char *buf,
													 int buf_size)
{
	olive::ProjectImportTask *t = task ? as_import(impl(task)) : nullptr;
	if (!t || index < 0 || index >= t->get_invalid_files().size()) {
		return OAKENGINE_E_INVALID;
	}
	return write_string(t->get_invalid_files().at(index), buf, buf_size);
}

/* ---- Save task results ---------------------------------------------------- */

extern "C" OakEngineProject *
oakengine_task_save_get_project(OakEngineTask *task)
{
	olive::ProjectSaveTask *t = task ? as_save(impl(task)) : nullptr;
	return t ? reinterpret_cast<OakEngineProject *>(t->get_project()) :
			   nullptr;
}

extern "C" int oakengine_cli_task_dialog_run(OakEngineTask *task,
										   void *parent_or_NULL)
{
	if (!task) {
		return 0;
	}
	olive::CLITaskDialog dlg(impl(task),
						 static_cast<QObject *>(parent_or_NULL));
	return dlg.run() ? 1 : 0;
}
