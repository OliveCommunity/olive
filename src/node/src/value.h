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

#ifndef OAK_NODEVALUE_H
#define OAK_NODEVALUE_H

#include <map>
#include <string>
#include <vector>

#include "mathtypes.h"
#include "splitvalue.h"
#include "variant.h"
#include "olive/core/util/bezier.h"
#include "olive/core/util/color.h"
#include "olive/core/util/rational.h"
#include "render/texture.h"

namespace olive
{

using core::Bezier;
using core::Color;
using core::Rational;

class Node;
class NodeValue;
class NodeValueTable;

using NodeValueArray = std::map<int, NodeValue>;
using NodeValueTableArray = std::map<int, NodeValueTable>;

class NodeValue {
public:
	/**
   * @brief The types of data that can be passed between Nodes
   */
	enum Type {
		k_none,

		/**
     ****************************** SPECIFIC IDENTIFIERS ******************************
     */

		/**
     * Integer type
     *
     * Resolves to int64_t.
     */
		k_int,

		/**
     * Decimal (floating-point) type
     *
     * Resolves to `double`.
     */
		k_float,

		/**
     * Decimal (Rational) type
     *
     * Resolves to `double`.
     */
		k_rational,

		/**
     * Boolean type
     *
     * Resolves to `bool`.
     */
		k_boolean,

		/**
     * Floating-point type
     *
     * Resolves to `Color`.
     *
     * Colors passed around the nodes should always be in reference space and preferably use
     */
		k_color,

		/**
     * Matrix type
     *
     * Resolves to `Matrix4x4`.
     */
		k_matrix,

		/**
     * Text type
     *
     * Resolves to `std::string`.
     */
		k_text,

		/**
     * Font type
     *
     * Resolves to `std::string` (the font family name).
     */
		k_font,

		/**
     * File type
     *
     * Resolves to a `std::string` containing an absolute file path.
     */
		k_file,

		/**
     * Image buffer type
     *
     * True value type depends on the render engine used.
     */
		k_texture,

		/**
     * Audio samples type
     *
     * Resolves to `SampleBuffer`.
     */
		k_samples,

		/**
     * Two-dimensional vector (XY) type
     *
     * Resolves to `Vector2D`.
     */
		k_vec2,

		/**
     * Three-dimensional vector (XYZ) type
     *
     * Resolves to `Vector3D`.
     */
		k_vec3,

		/**
     * Four-dimensional vector (XYZW) type
     *
     * Resolves to `Vector4D`.
     */
		k_vec4,

		/**
     * Cubic bezier type that contains three X/Y coordinates, the main point, and two control points
     *
     * Resolves to `Bezier`
     */
		k_bezier,

		/**
     * ComboBox type
     *
     * Resolves to `int` - the index currently selected
     */
		k_combo,
		/**
	 * ComboBox type
	 *
	 * Resolves to `std::string` - the text of choice currently selected
	 * This is to support the OpenFX type kOfxParamTypeStrChoice
	 */
		k_str_combo,
		/**
     * Video Parameters type
     *
     * Resolves to `VideoParams`
     */
		k_video_params,

		/**
     * Audio Parameters type
     *
     * Resolves to `AudioParams`
     */
		k_audio_params,

		/**
     * Subtitle Parameters type
     *
     * Resolves to `SubtitleParams`
     */
		k_subtitle_params,

		/**
     * Binary Data
     */
		k_binary,

		/**
		 *Push Button
		 */
		k_push_button,
		/**
     * End of list
     */
		k_data_type_count
	};

	NodeValue()
		: type_(k_none)
		, from_(nullptr)
		, array_(false)
	{
	}

	template <typename T>
	NodeValue(Type type, const T &data, const Node *from = nullptr,
			  bool array = false, const std::string &tag = std::string())
		: type_(type)
		, from_(from)
		, tag_(tag)
		, array_(array)
	{
		set_value(data);
	}

	template <typename T>
	NodeValue(Type type, const T &data, const Node *from, const std::string &tag)
		: NodeValue(type, data, from, false, tag)
	{
	}

	Type type() const
	{
		return type_;
	}

	template <typename T> T value() const
	{
		return data_.value<T>();
	}

	template <typename T> void set_value(const T &v)
	{
		data_ = Variant::from_value(v);
	}

	const Variant &data() const
	{
		return data_;
	}

	template <typename T> bool can_convert() const
	{
		return data_.can_convert<T>();
	}

	const std::string &tag() const
	{
		return tag_;
	}

	void set_tag(const std::string &tag)
	{
		tag_ = tag;
	}

	const Node *source() const
	{
		return from_;
	}

	bool array() const
	{
		return array_;
	}

	bool operator==(const NodeValue &rhs) const
	{
		return type_ == rhs.type_ && tag_ == rhs.tag_ && data_ == rhs.data_;
	}

	operator bool() const
	{
		return !data_.is_null();
	}

	static std::string get_pretty_data_type_name(Type type);

	static std::string get_data_type_name(Type type);
	static NodeValue::Type get_data_type_from_name(const std::string &n);

	static std::string value_to_string(Type data_type, const Variant &value,
									 bool value_is_a_key_track);
	static std::string value_to_string(const NodeValue &v, bool value_is_a_key_track)
	{
		return value_to_string(v.type_, v.data_, value_is_a_key_track);
	}

	static Variant string_to_value(Type data_type, const std::string &string,
								  bool value_is_a_key_track);

	static std::vector<Variant>
	split_normal_value_into_track_values(Type type, const Variant &value);

	static Variant
	combine_track_values_into_normal_value(Type type,
										   const std::vector<Variant> &split);

	SplitValue to_split_value() const
	{
		return split_normal_value_into_track_values(type_, data_);
	}

	/**
   * @brief Returns whether a data type can be interpolated or not
   */
	static bool type_can_be_interpolated(NodeValue::Type type)
	{
		return type == k_float || type == k_vec2 || type == k_vec3 ||
			   type == k_vec4 || type == k_bezier || type == k_color ||
			   type == k_rational;
	}

	static bool type_is_numeric(NodeValue::Type type)
	{
		return type == k_float || type == k_int || type == k_rational;
	}

	static bool type_is_vector(NodeValue::Type type)
	{
		return type == k_vec2 || type == k_vec3 || type == k_vec4;
	}

	static bool type_is_buffer(NodeValue::Type type)
	{
		return type == k_texture || type == k_samples;
	}

	static int get_number_of_keyframe_tracks(Type type);

	static void validate_vector_string(std::vector<std::string> *list, int count);

	TexturePtr to_texture() const
	{
		return value<TexturePtr>();
	}
	SampleBuffer to_samples() const
	{
		return value<SampleBuffer>();
	}
	bool to_bool() const
	{
		return value<bool>();
	}
	double to_double() const
	{
		return value<double>();
	}
	int64_t to_int() const
	{
		return value<int64_t>();
	}
	Rational to_rational() const
	{
		return value<olive::core::Rational>();
	}
	std::string to_string() const
	{
		return value<std::string>();
	}
	Color to_color() const
	{
		return value<olive::core::Color>();
	}
	Matrix4x4 to_matrix() const
	{
		return value<Matrix4x4>();
	}
	VideoParams to_video_params() const
	{
		return value<VideoParams>();
	}
	AudioParams to_audio_params() const
	{
		return value<AudioParams>();
	}
	Vector2D to_vec2() const
	{
		return value<Vector2D>();
	}
	Vector3D to_vec3() const
	{
		return value<Vector3D>();
	}
	Vector4D to_vec4() const
	{
		return value<Vector4D>();
	}
	Bezier to_bezier() const
	{
		return value<Bezier>();
	}
	NodeValueArray to_array() const
	{
		return value<NodeValueArray>();
	}

private:
	Type type_;
	Variant data_;
	const Node *from_;
	std::string tag_;
	bool array_;
};

class NodeValueTable {
public:
	NodeValueTable() = default;

	NodeValue get(NodeValue::Type type, const std::string &tag = std::string()) const
	{
		std::vector<NodeValue::Type> types = { type };
		return get(types, tag);
	}

	NodeValue get(const std::vector<NodeValue::Type> &type,
				  const std::string &tag = std::string()) const;

	NodeValue take(NodeValue::Type type, const std::string &tag = std::string())
	{
		std::vector<NodeValue::Type> types = { type };
		return take(types, tag);
	}

	NodeValue take(const std::vector<NodeValue::Type> &type,
				   const std::string &tag = std::string());

	void push(const NodeValue &value)
	{
		values_.push_back(value);
	}

	void push(const NodeValueTable &value)
	{
		values_.insert(values_.end(), value.values_.begin(), value.values_.end());
	}

	template <typename T>
	void push(NodeValue::Type type, const T &data, const Node *from,
			  bool array = false, const std::string &tag = std::string())
	{
		push(NodeValue(type, data, from, array, tag));
	}

	template <typename T>
	void push(NodeValue::Type type, const T &data, const Node *from,
			  const std::string &tag)
	{
		push(NodeValue(type, data, from, false, tag));
	}

	void prepend(const NodeValue &value)
	{
		values_.insert(values_.begin(), value);
	}

	template <typename T>
	void prepend(NodeValue::Type type, const T &data, const Node *from,
				 bool array = false, const std::string &tag = std::string())
	{
		prepend(NodeValue(type, data, from, array, tag));
	}

	template <typename T>
	void prepend(NodeValue::Type type, const T &data, const Node *from,
				 const std::string &tag)
	{
		prepend(NodeValue(type, data, from, false, tag));
	}

	const NodeValue &at(int index) const
	{
		return values_.at(index);
	}
	NodeValue take_at(int index)
	{
		NodeValue v = values_.at(index);
		values_.erase(values_.begin() + index);
		return v;
	}

	int count() const
	{
		return values_.size();
	}

	bool has(NodeValue::Type type) const;
	void remove(const NodeValue &v);

	void clear()
	{
		values_.clear();
	}

	bool is_empty() const
	{
		return values_.empty();
	}

	bool operator==(const NodeValueTable &rhs) const
	{
		return values_ == rhs.values_;
	}

	bool operator!=(const NodeValueTable &rhs) const
	{
		return !(*this == rhs);
	}

	int get_value_index(const std::vector<NodeValue::Type> &type,
						const std::string &tag) const;

	static NodeValueTable merge(std::vector<NodeValueTable> tables);

private:
	std::vector<NodeValue> values_;
};

using NodeValueRow = std::map<std::string, NodeValue>;

}

#endif // OAK_NODEVALUE_H
