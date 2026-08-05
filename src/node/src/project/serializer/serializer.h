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

#ifndef OAK_PROJECTSERIALIZER_H
#define OAK_PROJECTSERIALIZER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "define.h"
#include "project.h"
#include "project/serializer/serializedlayoutinfo.h"
#include "typeserializer.h"

namespace olive
{

/**
 * @brief An abstract base class for serializing/deserializing project data
 *
 * The goal of this is to further abstract serialized project data from their
 * in-memory representations.
 */
class ProjectSerializer {
public:
	enum LoadType {
		k_project,
		k_only_nodes,
		k_only_clips,
		k_only_markers,
		k_only_keyframes
	};

	ProjectSerializer() = default;

	virtual ~ProjectSerializer()
	{
	}

	DISABLE_COPY_MOVE(ProjectSerializer)

	enum ResultCode {
		k_success,
		k_project_too_old,
		k_project_too_new,
		k_unknown_version,
		k_file_error,
		k_xml_error,
		k_overwrite_error,
		k_no_data
	};

	using SerializedProperties =
		std::map<Node *, std::map<std::string, std::string>>;
	using SerializedKeyframes =
		std::map<std::string, std::vector<NodeKeyframe *>>;

	class LoadData {
	public:
		LoadData() = default;

		SerializedProperties properties;

		std::vector<TimelineMarker *> markers;

		SerializedKeyframes keyframes;

		SerializedLayoutInfo layout;

		std::vector<Node *> nodes;

		std::map<uintptr_t, Node *> node_ptrs;

		std::map<Node *, std::string> node_uuids;

		Node::OutputConnections promised_connections;
	};

	class Result {
	public:
		Result(const ResultCode &code)
			: code_(code)
		{
		}

		bool operator==(const ResultCode &code)
		{
			return code_ == code;
		}
		bool operator!=(const ResultCode &code)
		{
			return code_ != code;
		}

		const ResultCode &code() const
		{
			return code_;
		}

		const std::string &get_details() const
		{
			return details_;
		}

		void set_details(const std::string &s)
		{
			details_ = s;
		}

		const LoadData &get_load_data() const
		{
			return load_data_;
		}

		void set_load_data(const LoadData &p)
		{
			load_data_ = p;
		}

	private:
		ResultCode code_;

		std::string details_;

		LoadData load_data_;
	};

	class SaveData {
	public:
		SaveData(LoadType type, Project *project = nullptr,
				 const std::string &filename = std::string())
		{
			type_ = type;
			project_ = project;
			filename_ = filename;
		}

		Project *get_project() const
		{
			return project_;
		}
		void set_project(Project *p)
		{
			project_ = p;
		}

		const std::string &get_filename() const
		{
			return filename_;
		}
		void set_filename(const std::string &s)
		{
			filename_ = s;
		}

		LoadType type() const
		{
			return type_;
		}

		const SerializedLayoutInfo &get_layout() const
		{
			return layout_;
		}
		void set_layout(const SerializedLayoutInfo &layout)
		{
			layout_ = layout;
		}

		const std::vector<Node *> &get_only_serialize_nodes() const
		{
			return only_serialize_nodes_;
		}
		void set_only_serialize_nodes(const std::vector<Node *> &only)
		{
			only_serialize_nodes_ = only;
		}
		void set_only_serialize_nodes_and_resolve_groups(
			std::vector<Node *> only);

		const std::vector<TimelineMarker *> &get_only_serialize_markers() const
		{
			return only_serialize_markers_;
		}
		void set_only_serialize_markers(
			const std::vector<TimelineMarker *> &only)
		{
			only_serialize_markers_ = only;
		}

		const std::vector<NodeKeyframe *> &get_only_serialize_keyframes() const
		{
			return only_serialize_keyframes_;
		}
		void set_only_serialize_keyframes(
			const std::vector<NodeKeyframe *> &only)
		{
			only_serialize_keyframes_ = only;
		}

		const SerializedProperties &get_properties() const
		{
			return properties_;
		}
		void set_properties(const SerializedProperties &p)
		{
			properties_ = p;
		}

	private:
		LoadType type_;

		Project *project_;

		std::string filename_;

		SerializedLayoutInfo layout_;

		std::vector<Node *> only_serialize_nodes_;

		SerializedProperties properties_;

		std::vector<TimelineMarker *> only_serialize_markers_;

		std::vector<NodeKeyframe *> only_serialize_keyframes_;
	};

	static void initialize();

	static void destroy();

	static Result load(Project *project, const std::string &filename,
					   LoadType load_type);
	static Result load(Project *project, XmlStreamReader *read_device,
					   LoadType load_type);
	static Result paste(LoadType load_type, Project *project = nullptr);

	static Result save(const SaveData &data, bool compress);
	static Result save(XmlStreamWriter *write_device, const SaveData &data);
	static Result copy(const SaveData &data);

	/**
	 * @brief Returns true if the file starts with the "OVEC" compressed
	 * project signature
	 *
	 * NOTE(de-Qt): took a QFile* before; now takes the file path.
	 */
	static bool check_compressed_id(const std::string &filename);

protected:
	virtual LoadData load(Project *project, XmlStreamReader *reader,
						  LoadType load_type, void *reserved) const = 0;

	virtual void save(XmlStreamWriter *writer, const SaveData &data,
					  void *reserved) const
	{
	}

	virtual uint version() const = 0;

	bool is_cancelled() const;

private:
	static Result load_with_serializer_version(uint version, Project *project,
											XmlStreamReader *reader,
											LoadType load_type);

	static std::vector<ProjectSerializer *> instances;
};

}

#endif // OAK_PROJECTSERIALIZER_H
