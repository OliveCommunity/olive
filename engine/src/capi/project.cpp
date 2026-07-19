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

#include "oakengine/project.h"

#include <cstdio>
#include <cstring>

#include <QByteArray>
#include <QFileInfo>
#include <QString>

#include "coreengine.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "node/project/serializer/serializer.h"
#include "undo/undostack.h"

namespace
{

olive::Project *impl(OakEngineProject *h)
{
	return reinterpret_cast<olive::Project *>(h);
}

const olive::Project *impl(const OakEngineProject *h)
{
	return reinterpret_cast<const olive::Project *>(h);
}

OakEngineProject *wrap(olive::Project *p)
{
	return reinterpret_cast<OakEngineProject *>(p);
}

OakEngineSequence *wrap_seq(olive::Sequence *s)
{
	return reinterpret_cast<OakEngineSequence *>(s);
}

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

// The footage node at `index` in iteration order over the graph, or nullptr.
olive::Footage *footage_at(const olive::Project *p, int index)
{
	if (index < 0) {
		return nullptr;
	}
	int i = 0;
	for (olive::Node *n : p->nodes()) {
		if (olive::Footage *f = dynamic_cast<olive::Footage *>(n)) {
			if (i == index) {
				return f;
			}
			i++;
		}
	}
	return nullptr;
}

// The sequence node at `index` in iteration order over the graph, or nullptr.
olive::Sequence *sequence_at(const olive::Project *p, int index)
{
	if (index < 0) {
		return nullptr;
	}
	int i = 0;
	for (olive::Node *n : p->nodes()) {
		if (olive::Sequence *s = dynamic_cast<olive::Sequence *>(n)) {
			if (i == index) {
				return s;
			}
			i++;
		}
	}
	return nullptr;
}

int node_count_of_type(const olive::Project *p, bool sequences)
{
	int count = 0;
	for (olive::Node *n : p->nodes()) {
		const bool match = sequences ?
			(dynamic_cast<olive::Sequence *>(n) != nullptr) :
			(dynamic_cast<olive::Footage *>(n) != nullptr);
		if (match) {
			count++;
		}
	}
	return count;
}

// Human-readable text for a failed project load, mirroring the messages in
// ProjectLoadTask::run() (task/project/load/load.cpp).
QString load_error_string(olive::ProjectSerializer::ResultCode code,
						  const QString &details, const QString &filename)
{
	switch (code) {
	case olive::ProjectSerializer::k_project_too_old:
		return QStringLiteral(
			"This project is from a version of Oak Video Editor that is no "
			"longer supported in this version.");
	case olive::ProjectSerializer::k_project_too_new:
		return QStringLiteral(
			"This project is from a newer version of Oak Video Editor and "
			"cannot be opened in this version.");
	case olive::ProjectSerializer::k_unknown_version:
		return QStringLiteral("Failed to determine project version.");
	case olive::ProjectSerializer::k_file_error:
		return QStringLiteral("Failed to read file \"%1\" for reading.")
			.arg(filename);
	case olive::ProjectSerializer::k_xml_error:
		return QStringLiteral(
				   "Failed to read XML document. File may be corrupt. Error was: %1")
			.arg(details);
	case olive::ProjectSerializer::k_no_data:
		return QStringLiteral("Failed to find any data to parse.");
	case olive::ProjectSerializer::k_success:
	case olive::ProjectSerializer::k_overwrite_error:
		break;
	}
	return QStringLiteral("Unknown error.");
}

} // namespace

extern "C"
{

OakEngineProject *oakengine_project_create(void)
{
	// Not initialized on purpose: the project serializers require a fresh
	// project (root folder unset), so oakengine_project_new() and
	// oakengine_project_load() perform the one-time content setup.
	return wrap(new olive::Project());
}

void oakengine_project_free(OakEngineProject *self)
{
	delete impl(self);
}

int oakengine_project_new(OakEngineProject *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	if (impl(self)->root() != nullptr) {
		return OAKENGINE_E_STATE;
	}
	impl(self)->initialize();
	if (olive::EngineCore::instance()) {
		olive::EngineCore::instance()->undo_stack()->clear();
	}
	return OAKENGINE_OK;
}

int oakengine_project_load(OakEngineProject *self, const char *path,
						   char *err, int err_size)
{
	if (!self || !path) {
		return OAKENGINE_E_INVALID;
	}
	olive::Project *project = impl(self);
	if (project->root() != nullptr) {
		return OAKENGINE_E_STATE;
	}

	const QString filename = QString::fromUtf8(path);
	project->set_filename(filename);

	olive::ProjectSerializer::Result result = olive::ProjectSerializer::load(
		project, filename, olive::ProjectSerializer::k_project);
	if (result != olive::ProjectSerializer::k_success) {
		// The project may be partially loaded; the handle should be freed
		// (loading again is rejected above because root is set by then).
		string_to_buf(load_error_string(result.code(), result.get_details(),
										filename),
					  err, err_size);
		return OAKENGINE_E_FAILED;
	}

	// Validate footage like the application does: resolve files that moved
	// together with the project. Without a relink handler (none exists at
	// this layer) the project is accepted as-is.
	if (olive::EngineCore::instance()) {
		olive::EngineCore::instance()->validate_footage_in_loaded_project(
			project, project->get_saved_url());
		olive::EngineCore::instance()->undo_stack()->clear();
	}

	project->set_modified(false);
	if (err && err_size > 0) {
		err[0] = '\0';
	}
	return OAKENGINE_OK;
}

int oakengine_project_save(OakEngineProject *self, const char *path)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::Project *project = impl(self);

	QString filename = path ? QString::fromUtf8(path) : project->filename();
	if (filename.isEmpty()) {
		return OAKENGINE_E_INVALID;
	}

	olive::ProjectSerializer::SaveData data(olive::ProjectSerializer::k_project,
											project, filename);
	const bool compress = !filename.endsWith(QStringLiteral(".ovexml"),
											 Qt::CaseInsensitive);
	olive::ProjectSerializer::Result result =
		olive::ProjectSerializer::save(data, compress);

	switch (result.code()) {
	case olive::ProjectSerializer::k_success:
		project->set_filename(filename);
		project->set_modified(false);
		return OAKENGINE_OK;
	case olive::ProjectSerializer::k_overwrite_error:
		// The file could not be replaced and the project was written to a
		// temporary name instead; the engine counts this as a success.
		project->set_filename(result.get_details());
		project->set_modified(false);
		return OAKENGINE_OK;
	default:
		return OAKENGINE_E_FAILED;
	}
}

int oakengine_project_is_modified(const OakEngineProject *self)
{
	return self && impl(self)->is_modified() ? 1 : 0;
}

int oakengine_project_set_modified(OakEngineProject *self, int modified)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	impl(self)->set_modified(modified != 0);
	return OAKENGINE_OK;
}

int oakengine_project_name(const OakEngineProject *self, char *buf,
						   int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->name(), buf, buf_size);
}

int oakengine_project_filename(const OakEngineProject *self, char *buf,
							   int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->filename(), buf, buf_size);
}

int oakengine_project_footage_count(const OakEngineProject *self)
{
	return self ? node_count_of_type(impl(self), false) : 0;
}

int oakengine_project_footage_filename(const OakEngineProject *self, int index,
									   char *buf, int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	olive::Footage *f = footage_at(impl(self), index);
	if (!f) {
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(f->filename(), buf, buf_size);
}

int oakengine_project_footage_is_online(const OakEngineProject *self,
										int index)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Project *project = impl(self);
	olive::Footage *f = footage_at(project, index);
	if (!f) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const QString filename = f->filename();
	if (QFileInfo::exists(filename)) {
		return 1;
	}
	// Footage that moved together with the project file: resolve relative
	// paths against the project's directory (same rule as
	// EngineCore::validate_footage_in_loaded_project()).
	if (QFileInfo(filename).isRelative() && !project->filename().isEmpty()) {
		const QString resolved =
			QFileInfo(project->filename()).dir().filePath(filename);
		if (QFileInfo::exists(resolved)) {
			return 1;
		}
	}
	return 0;
}

int oakengine_project_can_undo(const OakEngineProject *self)
{
	if (!self || !olive::EngineCore::instance()) {
		return 0;
	}
	return olive::EngineCore::instance()->undo_stack()->can_undo() ? 1 : 0;
}

int oakengine_project_can_redo(const OakEngineProject *self)
{
	if (!self || !olive::EngineCore::instance()) {
		return 0;
	}
	return olive::EngineCore::instance()->undo_stack()->can_redo() ? 1 : 0;
}

int oakengine_project_undo(OakEngineProject *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	if (olive::EngineCore::instance()) {
		olive::EngineCore::instance()->undo_stack()->undo();
	}
	return OAKENGINE_OK;
}

int oakengine_project_redo(OakEngineProject *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	if (olive::EngineCore::instance()) {
		olive::EngineCore::instance()->undo_stack()->redo();
	}
	return OAKENGINE_OK;
}

int oakengine_project_sequence_count(const OakEngineProject *self)
{
	return self ? node_count_of_type(impl(self), true) : 0;
}

OakEngineSequence *oakengine_project_sequence_at(const OakEngineProject *self,
												 int index)
{
	if (!self) {
		return nullptr;
	}
	return wrap_seq(sequence_at(impl(self), index));
}

} // extern "C"
