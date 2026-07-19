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

#include "serializer.h"

#include <QApplication>
#include <QFile>
#include <QXmlStreamReader>

#include "common/xmlutils.h"
#include "coreengine.h"
#include "node/group/group.h"
#include "serializer190219.h"
#include "serializer210528.h"
#include "serializer210907.h"
#include "serializer211228.h"
#include "serializer220403.h"
#include "serializer230220.h"

namespace olive
{

QVector<ProjectSerializer *> ProjectSerializer::instances;

void ProjectSerializer::initialize()
{
	// Make sure to order these from oldest to newest

	// FIXME: Implement this - yes it's a 0.1 project loader
	//instances_.append(new ProjectSerializer190219);

	instances.append(new ProjectSerializer210528);
	instances.append(new ProjectSerializer210907);
	instances.append(new ProjectSerializer211228);
	instances.append(new ProjectSerializer220403);
	instances.append(new ProjectSerializer230220);
}

void ProjectSerializer::destroy()
{
	qDeleteAll(instances);
	instances.clear();
}

ProjectSerializer::Result ProjectSerializer::load(Project *project,
												  const QString &filename,
												  LoadType load_type)
{
	QFile project_file(filename);

	if (project_file.open(QFile::ReadOnly)) {
		// Some project files are compressed, marked with "OVEC" at the beginning of the file. Check for
		// that signature now.
		std::unique_ptr<QXmlStreamReader> reader;
		if (check_compressed_id(&project_file)) {
			// File is compressed, decompress into memory
			QByteArray b;
			b = qUncompress(project_file.readAll());
			reader.reset(new QXmlStreamReader(b));
		} else {
			project_file.seek(0);
			reader.reset(new QXmlStreamReader(&project_file));
		}

		Result inner_result = load(project, reader.get(), load_type);

		project_file.close();

		if (inner_result.code() != k_success) {
			return inner_result;
		}

		if (reader->hasError()) {
			Result r(k_xml_error);
			r.set_details(reader->errorString());
			return r;
		} else {
			return inner_result;
		}
	} else {
		Result r(k_file_error);
		r.set_details(QStringLiteral("Unable to open '%1': %2")
						 .arg(filename, project_file.errorString()));
		return r;
	}
}

ProjectSerializer::Result ProjectSerializer::load(Project *project,
												  QXmlStreamReader *reader,
												  LoadType load_type)
{
	// Determine project version
	uint version = 0;
	Result res = k_unknown_version;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == QStringLiteral("olive") ||
			reader->name() == QStringLiteral("project")) { // 0.1 projects only

			XMLAttributeLoop(reader, attr)
			{
				if (attr.name() ==
					QStringLiteral("version")) { // 230220+ projects
					version = attr.value().toUInt();
				} else if (attr.name() ==
						   QStringLiteral("url")) { // 230220+ projects
					if (project) {
						project->set_saved_url(attr.value().toString());
					}
				}
			}

			while (xml_read_next_start_element(reader)) {
				if (reader->name() ==
					QStringLiteral("version")) { // projects <= 220403
					version = reader->readElementText().toUInt();
				} else if (reader->name() ==
						   QStringLiteral("url")) { // projects <= 220403
					if (project) {
						project->set_saved_url(reader->readElementText());
					} else {
						reader->skipCurrentElement();
					}
				} else {
					// Handle any other value with the serializer
					res = load_with_serializer_version(version, project, reader,
													load_type);
				}
			}
		} else {
			reader->skipCurrentElement();
		}
	}

	return res;
}

ProjectSerializer::Result ProjectSerializer::paste(LoadType load_type,
												   Project *project)
{
	QString clipboard = EngineCore::paste_string_from_clipboard();
	if (clipboard.isEmpty()) {
		return k_no_data;
	}

	QXmlStreamReader reader(clipboard);

	return ProjectSerializer::load(project, &reader, load_type);
}

ProjectSerializer::Result ProjectSerializer::save(const SaveData &data,
												  bool compress)
{
	QString temp_save =
		FileFunctions::get_safe_temporary_filename(data.get_filename());

	QFile project_file(temp_save);

	if (project_file.open(QFile::WriteOnly)) {
		QByteArray b;
		QXmlStreamWriter writer(&b);

		Result inner_result = save(&writer, data);

		if (writer.hasError()) {
			Result r(k_xml_error);
			return r;
		}

		if (compress) {
			project_file.write("OVEC");
			project_file.write(qCompress(b));
		} else {
			project_file.write(b);
		}

		project_file.close();

		if (inner_result != k_success) {
			return inner_result;
		}

		// Save was successful, we can now rewrite the original file
		if (FileFunctions::rename_file_allow_overwrite(temp_save,
													data.get_filename())) {
			return k_success;
		} else {
			Result r(k_overwrite_error);
			r.set_details(temp_save);
			return r;
		}
	} else {
		Result r(k_file_error);
		r.set_details(temp_save);
		return r;
	}
}

ProjectSerializer::Result ProjectSerializer::save(QXmlStreamWriter *writer,
												  const SaveData &data)
{
	writer->setAutoFormatting(true);

	writer->writeStartDocument();

	writer->writeStartElement("olive");

	// By default, save as last serializer which, assuming the instances are ordered correctly,
	// will be the newest file format. But we may allow saving as older versions later on.
	ProjectSerializer *serializer = instances.last();

	// Version is stored in YYMMDD from whenever the project format was last changed
	// Allows easy integer math for checking project versions.
	writer->writeAttribute(QStringLiteral("version"),
						   QString::number(serializer->version()));

	if (!data.get_filename().isEmpty()) {
		writer->writeAttribute("url", data.get_filename());
	}

	serializer->save(writer, data, nullptr);

	writer->writeEndElement(); // olive

	writer->writeEndDocument();

	if (writer->hasError()) {
		return k_xml_error;
	}

	return k_success;
}

ProjectSerializer::Result ProjectSerializer::copy(const SaveData &data)
{
	QString copy_str;
	QXmlStreamWriter writer(&copy_str);

	ProjectSerializer::Result res = ProjectSerializer::save(&writer, data);

	if (res == k_success) {
		EngineCore::copy_string_to_clipboard(copy_str);
	}

	return res;
}

bool ProjectSerializer::check_compressed_id(QFile *file)
{
	QByteArray b = file->read(4);
	return !memcmp(b.data(), "OVEC", 4);
}

bool ProjectSerializer::is_cancelled() const
{
	return false;
}

ProjectSerializer::Result
ProjectSerializer::load_with_serializer_version(uint version, Project *project,
											 QXmlStreamReader *reader,
											 LoadType load_type)
{
	// Failed to find version in file
	if (version == 0) {
		return k_unknown_version;
	}

	// We should now have the version, if we have a serializer for it, use it to load the project
	ProjectSerializer *serializer = nullptr;

	foreach (ProjectSerializer *s, instances) {
		if (version == s->version()) {
			serializer = s;
			break;
		} else if (version < s->version()) {
			// Assuming the instance list is in order, if the project version is less than any version
			// we find, we must not support it anymore
			return k_project_too_old;
		}
	}

	if (serializer) {
		LoadData ld = serializer->load(project, reader, load_type, nullptr);
		Result r(k_success);
		if (reader->hasError()) {
			r = Result(k_xml_error);
			r.set_details(
				QCoreApplication::translate("Serializer", "%1 on line %2")
					.arg(reader->errorString(),
						 QString::number(reader->lineNumber())));
		}
		r.set_load_data(ld);
		return r;
	} else {
		// Reached the end of the list with no serializer, assume too new
		return k_project_too_new;
	}
}

void ProjectSerializer::SaveData::set_only_serialize_nodes_and_resolve_groups(
	QVector<Node *> nodes)
{
	// For any groups, add children
	for (int i = 0; i < nodes.size(); i++) {
		// If this is a group, add the child nodes too
		if (NodeGroup *g = dynamic_cast<NodeGroup *>(nodes.at(i))) {
			for (auto it = g->get_context_positions().cbegin();
				 it != g->get_context_positions().cend(); it++) {
				if (!nodes.contains(it.key())) {
					nodes.append(it.key());
				}
			}
		}
	}

	set_only_serialize_nodes(nodes);
}

}
