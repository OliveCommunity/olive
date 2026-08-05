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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <zlib.h>

#include "filefunctions.h"
#include "xmlutils.h"
#include "coreengine.h"
#include "group/group.h"
#include "serializer190219.h"
#include "serializer210528.h"
#include "serializer210907.h"
#include "serializer211228.h"
#include "serializer220403.h"
#include "serializer230220.h"

namespace olive
{

std::vector<ProjectSerializer *> ProjectSerializer::instances;

namespace
{

// Qt qCompress()-compatible: 4-byte big-endian uncompressed size, followed
// by zlib deflate data. Must stay byte-compatible with existing .ove files.
std::string qcompress_compat(const std::string &in)
{
	uLongf bound = compressBound(in.size());
	std::string out;
	out.resize(4 + bound);
	out[0] = char((in.size() >> 24) & 0xFF);
	out[1] = char((in.size() >> 16) & 0xFF);
	out[2] = char((in.size() >> 8) & 0xFF);
	out[3] = char(in.size() & 0xFF);
	uLongf dest_len = bound;
	if (compress2(reinterpret_cast<Bytef *>(&out[4]), &dest_len,
				  reinterpret_cast<const Bytef *>(in.data()), in.size(),
				  Z_DEFAULT_COMPRESSION) != Z_OK) {
		return std::string();
	}
	out.resize(4 + dest_len);
	return out;
}

// Qt qUncompress()-compatible counterpart of qcompress_compat()
bool quncompress_compat(const std::string &in, std::string *out)
{
	if (in.size() < 4) {
		return false;
	}
	uLongf len = (uLongf(uint8_t(in[0])) << 24) |
				 (uLongf(uint8_t(in[1])) << 16) |
				 (uLongf(uint8_t(in[2])) << 8) | uLongf(uint8_t(in[3]));
	out->resize(len);
	if (uncompress(reinterpret_cast<Bytef *>(&(*out)[0]), &len,
				   reinterpret_cast<const Bytef *>(in.data() + 4),
				   in.size() - 4) != Z_OK) {
		return false;
	}
	out->resize(len);
	return true;
}

} // namespace

void ProjectSerializer::initialize()
{
	// Make sure to order these from oldest to newest

	// FIXME: Implement this - yes it's a 0.1 project loader
	//instances_.append(new ProjectSerializer190219);

	instances.push_back(new ProjectSerializer210528);
	instances.push_back(new ProjectSerializer210907);
	instances.push_back(new ProjectSerializer211228);
	instances.push_back(new ProjectSerializer220403);
	instances.push_back(new ProjectSerializer230220);
}

void ProjectSerializer::destroy()
{
	for (ProjectSerializer *s : instances) {
		delete s;
	}
	instances.clear();
}

ProjectSerializer::Result ProjectSerializer::load(Project *project,
												  const std::string &filename,
												  LoadType load_type)
{
	std::ifstream project_file(filename, std::ios::binary);

	if (project_file.is_open()) {
		std::stringstream ss;
		ss << project_file.rdbuf();
		std::string file_data = ss.str();
		project_file.close();

		// Some project files are compressed, marked with "OVEC" at the beginning of the file. Check for
		// that signature now.
		std::string xml_data;
		if (file_data.size() >= 4 &&
			!memcmp(file_data.data(), "OVEC", 4)) {
			// File is compressed, decompress into memory
			if (!quncompress_compat(file_data.substr(4), &xml_data)) {
				Result r(k_xml_error);
				r.set_details("Failed to decompress project file");
				return r;
			}
		} else {
			xml_data = std::move(file_data);
		}

		XmlStreamReader reader(xml_data);

		Result inner_result = load(project, &reader, load_type);

		if (inner_result.code() != k_success) {
			return inner_result;
		}

		if (reader.has_error()) {
			Result r(k_xml_error);
			r.set_details(reader.error_string());
			return r;
		} else {
			return inner_result;
		}
	} else {
		Result r(k_file_error);
		r.set_details("Unable to open '" + filename + "'");
		return r;
	}
}

ProjectSerializer::Result ProjectSerializer::load(Project *project,
												  XmlStreamReader *reader,
												  LoadType load_type)
{
	// Determine project version
	uint version = 0;
	Result res = k_unknown_version;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "olive" ||
			reader->name() == "project") { // 0.1 projects only

			for (const XmlStreamAttribute &attr : reader->attributes()) {
				if (attr.name == "version") { // 230220+ projects
					version = strtoul(attr.value.c_str(), nullptr, 10);
				} else if (attr.name == "url") { // 230220+ projects
					if (project) {
						project->set_saved_url(attr.value);
					}
				}
			}

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "version") { // projects <= 220403
					version = strtoul(reader->read_element_text().c_str(),
									  nullptr, 10);
				} else if (reader->name() == "url") { // projects <= 220403
					if (project) {
						project->set_saved_url(reader->read_element_text());
					} else {
						reader->skip_current_element();
					}
				} else {
					// Handle any other value with the serializer
					res = load_with_serializer_version(version, project, reader,
													load_type);
				}
			}
		} else {
			reader->skip_current_element();
		}
	}

	return res;
}

ProjectSerializer::Result ProjectSerializer::paste(LoadType load_type,
												   Project *project)
{
	// ADAPT(M9): engine/coreengine.h is still Qt-based; convert at the
	// boundary until it is de-Qt'd.
	std::string clipboard =
		EngineCore::paste_string_from_clipboard();
	if (clipboard.empty()) {
		return k_no_data;
	}

	XmlStreamReader reader(clipboard);

	return ProjectSerializer::load(project, &reader, load_type);
}

ProjectSerializer::Result ProjectSerializer::save(const SaveData &data,
												  bool compress)
{
	std::string temp_save =
		FileFunctions::get_safe_temporary_filename(data.get_filename());

	XmlStreamWriter writer;

	Result inner_result = save(&writer, data);

	if (inner_result != k_success) {
		return inner_result;
	}

	std::ofstream project_file(temp_save,
							   std::ios::binary | std::ios::trunc);

	if (project_file.is_open()) {
		if (compress) {
			project_file.write("OVEC", 4);
			std::string compressed = qcompress_compat(writer.output());
			project_file.write(compressed.data(), compressed.size());
		} else {
			project_file << writer.output();
		}

		project_file.close();

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

ProjectSerializer::Result ProjectSerializer::save(XmlStreamWriter *writer,
												  const SaveData &data)
{
	// NOTE(de-Qt): no writeStartDocument()/auto-formatting; the output is
	// compact XML without a declaration. Element/attribute names and order
	// are unchanged.

	writer->write_start_element("olive");

	// By default, save as last serializer which, assuming the instances are ordered correctly,
	// will be the newest file format. But we may allow saving as older versions later on.
	ProjectSerializer *serializer = instances.back();

	// Version is stored in YYMMDD from whenever the project format was last changed
	// Allows easy integer math for checking project versions.
	writer->write_attribute("version", std::to_string(serializer->version()));

	if (!data.get_filename().empty()) {
		writer->write_attribute("url", data.get_filename());
	}

	serializer->save(writer, data, nullptr);

	writer->write_end_element(); // olive

	writer->write_end_document();

	return k_success;
}

ProjectSerializer::Result ProjectSerializer::copy(const SaveData &data)
{
	XmlStreamWriter writer;

	ProjectSerializer::Result res = ProjectSerializer::save(&writer, data);

	if (res == k_success) {
		// ADAPT(M9): engine/coreengine.h is still Qt-based
		EngineCore::copy_string_to_clipboard(
			writer.output());
	}

	return res;
}

bool ProjectSerializer::check_compressed_id(const std::string &filename)
{
	std::ifstream file(filename, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}
	char b[4] = { 0, 0, 0, 0 };
	file.read(b, 4);
	return !memcmp(b, "OVEC", 4);
}

bool ProjectSerializer::is_cancelled() const
{
	return false;
}

ProjectSerializer::Result
ProjectSerializer::load_with_serializer_version(uint version, Project *project,
											 XmlStreamReader *reader,
											 LoadType load_type)
{
	// Failed to find version in file
	if (version == 0) {
		return k_unknown_version;
	}

	// We should now have the version, if we have a serializer for it, use it to load the project
	ProjectSerializer *serializer = nullptr;

	for (ProjectSerializer *s : instances) {
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
		if (reader->has_error()) {
			r = Result(k_xml_error);
			// NOTE(de-Qt): no line number available from XmlStreamReader
			r.set_details(reader->error_string());
		}
		r.set_load_data(ld);
		return r;
	} else {
		// Reached the end of the list with no serializer, assume too new
		return k_project_too_new;
	}
}

void ProjectSerializer::SaveData::set_only_serialize_nodes_and_resolve_groups(
	std::vector<Node *> nodes)
{
	// For any groups, add children
	for (int i = 0; i < int(nodes.size()); i++) {
		// If this is a group, add the child nodes too
		if (NodeGroup *g = dynamic_cast<NodeGroup *>(nodes.at(i))) {
			for (auto it = g->get_context_positions().cbegin();
				 it != g->get_context_positions().cend(); it++) {
				if (std::find(nodes.begin(), nodes.end(), it->first) ==
					nodes.end()) {
					nodes.push_back(it->first);
				}
			}
		}
	}

	set_only_serialize_nodes(nodes);
}

}
