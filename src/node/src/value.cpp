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

#include "value.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "olive/core/util/stringutils.h"
#include "subtitleparams.h"
#include "videoparams.h"

namespace olive
{

/**
 * @brief Format a float like QString::number(f) ('g', 6 significant digits).
 */
static std::string number_to_string(double d)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%g", d);
	return std::string(buf);
}

std::string NodeValue::value_to_string(Type data_type, const Variant &value,
									 bool value_is_a_key_track)
{
	if (!value_is_a_key_track && data_type == k_vec2) {
		Vector2D vec = value.value<Vector2D>();

		return number_to_string(vec.x()) + ":" + number_to_string(vec.y());
	} else if (!value_is_a_key_track && data_type == k_vec3) {
		Vector3D vec = value.value<Vector3D>();

		return number_to_string(vec.x()) + ":" + number_to_string(vec.y()) + ":" +
			   number_to_string(vec.z());
	} else if (!value_is_a_key_track && data_type == k_vec4) {
		Vector4D vec = value.value<Vector4D>();

		return number_to_string(vec.x()) + ":" + number_to_string(vec.y()) + ":" +
			   number_to_string(vec.z()) + ":" + number_to_string(vec.w());
	} else if (!value_is_a_key_track && data_type == k_color) {
		Color c = value.value<Color>();

		return number_to_string(c.red()) + ":" + number_to_string(c.green()) + ":" +
			   number_to_string(c.blue()) + ":" + number_to_string(c.alpha());
	} else if (!value_is_a_key_track && data_type == k_bezier) {
		Bezier b = value.value<Bezier>();

		return number_to_string(b.x()) + ":" + number_to_string(b.y()) + ":" +
			   number_to_string(b.cp1_x()) + ":" + number_to_string(b.cp1_y()) + ":" +
			   number_to_string(b.cp2_x()) + ":" + number_to_string(b.cp2_y());
	} else if (data_type == k_rational) {
		return value.value<Rational>().to_string();
	} else if (data_type == k_texture || data_type == k_samples ||
			   data_type == k_none) {
		// These data types need no XML representation
		return std::string();
	} else if (data_type == k_int) {
		return std::to_string(value.value<int64_t>());
	} else if (data_type == k_binary) {
		return byte_array_to_base64(value.to_byte_array());
	} else {
		if (value.can_convert<std::string>()) {
			return value.to_string();
		}

		if (!value.is_null()) {
			fprintf(stderr, "Failed to convert type %x to string\n",
					(unsigned int)(data_type));
		}

		return std::string();
	}
}

std::vector<Variant>
NodeValue::split_normal_value_into_track_values(Type type, const Variant &value)
{
	std::vector<Variant> vals(get_number_of_keyframe_tracks(type));

	switch (type) {
	case k_vec2: {
		Vector2D vec = value.value<Vector2D>();
		vals[0] = vec.x();
		vals[1] = vec.y();
		break;
	}
	case k_vec3: {
		Vector3D vec = value.value<Vector3D>();
		vals[0] = vec.x();
		vals[1] = vec.y();
		vals[2] = vec.z();
		break;
	}
	case k_vec4: {
		Vector4D vec = value.value<Vector4D>();
		vals[0] = vec.x();
		vals[1] = vec.y();
		vals[2] = vec.z();
		vals[3] = vec.w();
		break;
	}
	case k_color: {
		Color c = value.value<Color>();
		vals[0] = c.red();
		vals[1] = c.green();
		vals[2] = c.blue();
		vals[3] = c.alpha();
		break;
	}
	case k_bezier: {
		Bezier b = value.value<Bezier>();
		vals[0] = b.x();
		vals[1] = b.y();
		vals[2] = b.cp1_x();
		vals[3] = b.cp1_y();
		vals[4] = b.cp2_x();
		vals[5] = b.cp2_y();
		break;
	}
	default:
		vals[0] = value;
	}

	return vals;
}

Variant NodeValue::combine_track_values_into_normal_value(
	Type type, const std::vector<Variant> &split)
{
	if (split.empty()) {
		return Variant();
	}

	switch (type) {
	case k_vec2: {
		return Vector2D(split.at(0).to_float(), split.at(1).to_float());
	}
	case k_vec3: {
		return Vector3D(split.at(0).to_float(), split.at(1).to_float(),
						split.at(2).to_float());
	}
	case k_vec4: {
		return Vector4D(split.at(0).to_float(), split.at(1).to_float(),
						split.at(2).to_float(), split.at(3).to_float());
	}
	case k_color: {
		return Variant::from_value(
			Color(split.at(0).to_float(), split.at(1).to_float(),
				  split.at(2).to_float(), split.at(3).to_float()));
	}
	case k_bezier:
		return Variant::from_value(
			Bezier(split.at(0).to_double(), split.at(1).to_double(),
				   split.at(2).to_double(), split.at(3).to_double(),
				   split.at(4).to_double(), split.at(5).to_double()));
	default:
		return split.front();
	}
}

int NodeValue::get_number_of_keyframe_tracks(Type type)
{
	switch (type) {
	case NodeValue::k_vec2:
		return 2;
	case NodeValue::k_vec3:
		return 3;
	case NodeValue::k_vec4:
	case NodeValue::k_color:
		return 4;
	case NodeValue::k_bezier:
		return 6;
	default:
		return 1;
	}
}

Variant NodeValue::string_to_value(Type data_type, const std::string &string,
								  bool value_is_a_key_track)
{
	if (!value_is_a_key_track && data_type == k_vec2) {
		std::vector<std::string> vals = core::StringUtils::split(string, ':');

		validate_vector_string(&vals, 2);

		return Vector2D(strtof(vals.at(0).c_str(), nullptr),
						strtof(vals.at(1).c_str(), nullptr));
	} else if (!value_is_a_key_track && data_type == k_vec3) {
		std::vector<std::string> vals = core::StringUtils::split(string, ':');

		validate_vector_string(&vals, 3);

		return Vector3D(strtof(vals.at(0).c_str(), nullptr),
						strtof(vals.at(1).c_str(), nullptr),
						strtof(vals.at(2).c_str(), nullptr));
	} else if (!value_is_a_key_track && data_type == k_vec4) {
		std::vector<std::string> vals = core::StringUtils::split(string, ':');

		validate_vector_string(&vals, 4);

		return Vector4D(strtof(vals.at(0).c_str(), nullptr),
						strtof(vals.at(1).c_str(), nullptr),
						strtof(vals.at(2).c_str(), nullptr),
						strtof(vals.at(3).c_str(), nullptr));
	} else if (!value_is_a_key_track && data_type == k_color) {
		std::vector<std::string> vals = core::StringUtils::split(string, ':');

		validate_vector_string(&vals, 4);

		return Variant::from_value(
			Color(strtod(vals.at(0).c_str(), nullptr),
				  strtod(vals.at(1).c_str(), nullptr),
				  strtod(vals.at(2).c_str(), nullptr),
				  strtod(vals.at(3).c_str(), nullptr)));
	} else if (!value_is_a_key_track && data_type == k_bezier) {
		std::vector<std::string> vals = core::StringUtils::split(string, ':');

		validate_vector_string(&vals, 6);

		return Variant::from_value(
			Bezier(strtod(vals.at(0).c_str(), nullptr),
				   strtod(vals.at(1).c_str(), nullptr),
				   strtod(vals.at(2).c_str(), nullptr),
				   strtod(vals.at(3).c_str(), nullptr),
				   strtod(vals.at(4).c_str(), nullptr),
				   strtod(vals.at(5).c_str(), nullptr)));
	} else if (data_type == k_int) {
		return Variant::from_value(strtoll(string.c_str(), nullptr, 10));
	} else if (data_type == k_rational) {
		return Variant::from_value(Rational::from_string(string));
	} else if (data_type == k_binary) {
		return byte_array_from_base64(string);
	} else {
		return string;
	}
}

void NodeValue::validate_vector_string(std::vector<std::string> *list, int count)
{
	while (list->size() < size_t(count)) {
		list->push_back("0");
	}
}

std::string NodeValue::get_pretty_data_type_name(Type type)
{
	switch (type) {
	case k_none:
		return "None";
	case k_int:
	case k_combo:
		return "Integer";
	case k_str_combo:
		return "String Combo";
	case k_float:
		return "Float";
	case k_rational:
		return "Rational";
	case k_boolean:
		return "Boolean";
	case k_color:
		return "Color";
	case k_matrix:
		return "Matrix";
	case k_text:
		return "Text";
	case k_font:
		return "Font";
	case k_file:
		return "File";
	case k_texture:
		return "Texture";
	case k_samples:
		return "Samples";
	case k_vec2:
		return "Vector 2D";
	case k_vec3:
		return "Vector 3D";
	case k_vec4:
		return "Vector 4D";
	case k_bezier:
		return "Bezier";
	case k_video_params:
		return "Video Parameters";
	case k_audio_params:
		return "Audio Parameters";
	case k_subtitle_params:
		return "Subtitle Parameters";
	case k_binary:
		return "Binary";
	case k_push_button:
		return "Push Button";

	case k_data_type_count:
		break;
	}

	return "Unknown";
}

std::string NodeValue::get_data_type_name(Type type)
{
	switch (type) {
	case k_none:
		return "none";
	case k_int:
		return "int";
	case k_combo:
		return "combo";
	case k_str_combo:
		return "strcombo";
	case k_float:
		return "float";
	case k_rational:
		return "Rational";
	case k_boolean:
		return "bool";
	case k_color:
		return "color";
	case k_matrix:
		return "matrix";
	case k_text:
		return "text";
	case k_font:
		return "font";
	case k_file:
		return "file";
	case k_texture:
		return "texture";
	case k_samples:
		return "samples";
	case k_vec2:
		return "vec2";
	case k_vec3:
		return "vec3";
	case k_vec4:
		return "vec4";
	case k_bezier:
		return "bezier";
	case k_video_params:
		return "vparam";
	case k_audio_params:
		return "aparam";
	case k_subtitle_params:
		return "sparam";
	case k_binary:
		return "binary";
	case k_push_button:
		return "pushbutton";
	case k_data_type_count:
		break;
	}

	return std::string();
}

NodeValue::Type NodeValue::get_data_type_from_name(const std::string &n)
{
	// Slow but easy to maintain
	for (int i = 0; i < k_data_type_count; i++) {
		Type t = static_cast<Type>(i);
		if (get_data_type_name(t) == n) {
			return t;
		}
	}

	return NodeValue::k_none;
}

NodeValue NodeValueTable::get(const std::vector<NodeValue::Type> &type,
							  const std::string &tag) const
{
	int value_index = get_value_index(type, tag);

	if (value_index >= 0) {
		return values_.at(value_index);
	}

	return NodeValue();
}

NodeValue NodeValueTable::take(const std::vector<NodeValue::Type> &type,
							   const std::string &tag)
{
	int value_index = get_value_index(type, tag);

	if (value_index >= 0) {
		return take_at(value_index);
	}

	return NodeValue();
}

bool NodeValueTable::has(NodeValue::Type type) const
{
	for (int i = int(values_.size()) - 1; i >= 0; i--) {
		const NodeValue &v = values_.at(i);

		if (v.type() == type) {
			return true;
		}
	}

	return false;
}

void NodeValueTable::remove(const NodeValue &v)
{
	for (int i = int(values_.size()) - 1; i >= 0; i--) {
		const NodeValue &compare = values_.at(i);

		if (compare == v) {
			values_.erase(values_.begin() + i);
			return;
		}
	}
}

NodeValueTable NodeValueTable::merge(std::vector<NodeValueTable> tables)
{
	if (tables.size() == 1) {
		return tables.front();
	}

	int row = 0;

	NodeValueTable merged_table;

	// Slipstreams all tables together
	while (true) {
		bool all_merged = true;

		for (const NodeValueTable &t : tables) {
			if (row < t.count()) {
				all_merged = false;
			} else {
				continue;
			}

			int row_index = t.count() - 1 - row;

			merged_table.prepend(t.at(row_index));
		}

		row++;

		if (all_merged) {
			break;
		}
	}

	return merged_table;
}

int NodeValueTable::get_value_index(const std::vector<NodeValue::Type> &types,
									const std::string &tag) const
{
	int index = -1;

	for (int i = int(values_.size()) - 1; i >= 0; i--) {
		const NodeValue &v = values_.at(i);

		if (std::find(types.begin(), types.end(), v.type()) != types.end() &&
			(tag.empty() || tag == v.tag())) {
			index = i;
			break;
		}
	}

	return index;
}

}
