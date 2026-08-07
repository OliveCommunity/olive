/***

  Oak Video Editor - Non-Linear Video Editor
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

#include "task/project.h"

#include <vector>

#include "../src/export/export.h"
#include "../src/precache/precachetask.h"
#include "../src/project/import/import.h"
#include "../src/project/loadotio/loadotio.h"
#include "../src/project/saveotio/saveotio.h"
#include "../src/project/load/load.h"
#include "../src/project/save/save.h"
#include "taskhandle.h"

using namespace oaktask_capi;

namespace
{

olive::ProjectImportTask *import_impl(OakTaskTask *t)
{
	if (!t) {
		return nullptr;
	}
	return dynamic_cast<olive::ProjectImportTask *>(impl(t)->task);
}

olive::ProjectLoadBaseTask *load_impl(OakTaskTask *t)
{
	if (!t) {
		return nullptr;
	}
	return dynamic_cast<olive::ProjectLoadBaseTask *>(impl(t)->task);
}

} // namespace

OakTaskTask *oaktask_create_project_load(const char *filename)
{
	if (!filename) {
		return NULL;
	}
	try {
		return wrap(new olive::ProjectLoadTask(filename));
	} catch (...) {
		return NULL;
	}
}

OakNodeProject *oaktask_load_take_project(OakTaskTask *t)
{
	olive::ProjectLoadBaseTask *task = load_impl(t);
	if (!task) {
		return NULL;
	}
	return task->take_project();
}

OakTaskTask *oaktask_create_project_save(OakNodeProject *project,
										 const char *filename_or_NULL,
										 int use_compression)
{
	if (!project) {
		return NULL;
	}
	try {
		auto *task = new olive::ProjectSaveTask(project,
												use_compression != 0);
		if (filename_or_NULL) {
			task->set_override_filename(filename_or_NULL);
		}
		return wrap(task);
	} catch (...) {
		return NULL;
	}
}

OakTaskTask *oaktask_create_project_import(OakNodeFolder *folder,
										   OakNodeProject *project,
										   const char *const *urls,
										   int url_count)
{
	if (!folder || !project || (!urls && url_count > 0) || url_count < 0) {
		return NULL;
	}
	try {
		std::vector<std::string> filenames;
		filenames.reserve(size_t(url_count));
		for (int i = 0; i < url_count; i++) {
			if (!urls[i]) {
				return NULL;
			}
			filenames.emplace_back(urls[i]);
		}
		return wrap(
			new olive::ProjectImportTask(folder, project, filenames));
	} catch (...) {
		return NULL;
	}
}

OakUndoCommand *oaktask_import_take_command(OakTaskTask *t)
{
	olive::ProjectImportTask *task = import_impl(t);
	if (!task) {
		return NULL;
	}
	return task->take_command();
}

int oaktask_import_footage_count(OakTaskTask *t)
{
	olive::ProjectImportTask *task = import_impl(t);
	if (!task) {
		return OAKTASK_E_INVALID;
	}
	return int(task->get_imported_footage().size());
}

OakNodeFootage *oaktask_import_footage_at(OakTaskTask *t, int index)
{
	olive::ProjectImportTask *task = import_impl(t);
	if (!task || index < 0 ||
		index >= int(task->get_imported_footage().size())) {
		return NULL;
	}
	return task->get_imported_footage()[size_t(index)];
}

int oaktask_import_invalid_count(OakTaskTask *t)
{
	olive::ProjectImportTask *task = import_impl(t);
	if (!task) {
		return OAKTASK_E_INVALID;
	}
	return int(task->get_invalid_files().size());
}

int oaktask_import_invalid_at(OakTaskTask *t, int index, char *buf,
							  int buf_size)
{
	olive::ProjectImportTask *task = import_impl(t);
	if (!task) {
		return OAKTASK_E_INVALID;
	}
	if (index < 0 || index >= int(task->get_invalid_files().size())) {
		return OAKTASK_E_NOT_FOUND;
	}
	return copy_string(task->get_invalid_files()[size_t(index)], buf,
					 buf_size);
}

void oaktask_import_set_image_sequence_confirm_cb(
	oaktask_image_sequence_confirm_fn fn, void *userdata)
{
	if (!fn) {
		olive::ProjectImportTask::set_image_sequence_confirm_callback(
			nullptr);
		return;
	}
	olive::ProjectImportTask::set_image_sequence_confirm_callback(
		[fn, userdata](const std::string &filename) {
			return fn(filename.c_str(), userdata) != 0;
		});
}

OakTaskTask *oaktask_create_precache(OakNodeFootage *footage, int index,
									 OakNodeSequence *sequence)
{
	if (!footage || !sequence) {
		return NULL;
	}
	try {
		return wrap(new olive::PreCacheTask(footage, index, sequence));
	} catch (...) {
		return NULL;
	}
}

OakTaskTask *oaktask_create_export(OakNodeNode *viewer,
								   OakNodeColorManager *color_manager,
								   const oakcodec_encoding_params *params)
{
	if (!viewer || !params) {
		return NULL;
	}
	try {
		return wrap(new olive::ExportTask(viewer, color_manager, *params));
	} catch (...) {
		return NULL;
	}
}

OakTaskTask *oaktask_create_project_load_otio(const char *filename)
{
	if (!filename) {
		return NULL;
	}
	try {
		return wrap(new olive::LoadOTIOTask(filename));
	} catch (...) {
		return NULL;
	}
}

OakNodeProject *oaktask_load_otio_take_project(OakTaskTask *t)
{
	olive::ProjectLoadBaseTask *task = load_impl(t);
	if (!task) {
		return NULL;
	}
	return task->take_project();
}

OakTaskTask *oaktask_create_project_save_otio(OakNodeProject *project,
											  const char *filename)
{
	if (!project || !filename) {
		return NULL;
	}
	try {
		return wrap(new olive::SaveOTIOTask(project, filename));
	} catch (...) {
		return NULL;
	}
}

void oaktask_load_otio_set_confirm_cb(oaktask_otio_import_confirm_fn fn,
									  void *userdata)
{
	if (!fn) {
		olive::LoadOTIOTask::set_import_confirm_callback(nullptr);
		return;
	}
	olive::LoadOTIOTask::set_import_confirm_callback(
		[fn, userdata](const std::vector<std::string> &names) {
			std::vector<const char *> ptrs;
			ptrs.reserve(names.size());
			for (const std::string &name : names) {
				ptrs.push_back(name.c_str());
			}
			return fn(ptrs.data(), int(ptrs.size()), userdata) != 0;
		});
}
