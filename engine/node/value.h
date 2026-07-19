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

#include <QMatrix4x4>
#include <QString>
#include <QVariant>
#include <QVector>

#include "common/qtutils.h"
#include "node/splitvalue.h"
#include "render/texture.h"

namespace olive
{

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
     * Resolves to `QMatrix4x4`.
     */
		k_matrix,

		/**
     * Text type
     *
     * Resolves to `QString`.
     */
		k_text,

		/**
     * Font type
     *
     * Resolves to `QFont`.
     */
		k_font,

		/**
     * File type
     *
     * Resolves to a `QString` containing an absolute file path.
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
     * Resolves to `SampleBufferPtr`.
     */
		k_samples,

		/**
     * Two-dimensional vector (XY) type
     *
     * Resolves to `QVector2D`.
     */
		k_vec2,

		/**
     * Three-dimensional vector (XYZ) type
     *
     * Resolves to `QVector3D`.
     */
		k_vec3,

		/**
     * Four-dimensional vector (XYZW) type
     *
     * Resolves to `QVector4D`.
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
	 * Resolves to `QString` - the text of choice currently selected
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
			  bool array = false, const QString &tag = QString())
		: type_(type)
		, from_(from)
		, tag_(tag)
		, array_(array)
	{
		set_value(data);
	}

	template <typename T>
	NodeValue(Type type, const T &data, const Node *from, const QString &tag)
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
		data_ = QVariant::fromValue(v);
	}

	const QVariant &data() const
	{
		return data_;
	}

	template <typename T> bool canConvert() const
	{
		return data_.canConvert<T>();
	}

	const QString &tag() const
	{
		return tag_;
	}

	void set_tag(const QString &tag)
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
		return !data_.isNull();
	}

	static QString get_pretty_data_type_name(Type type);

	static QString get_data_type_name(Type type);
	static NodeValue::Type get_data_type_from_name(const QString &n);

	static QString value_to_string(Type data_type, const QVariant &value,
								 bool value_is_a_key_track);
	static QString value_to_string(const NodeValue &v, bool value_is_a_key_track)
	{
		return value_to_string(v.type_, v.data_, value_is_a_key_track);
	}

	static QVariant string_to_value(Type data_type, const QString &string,
								  bool value_is_a_key_track);

	static QVector<QVariant>
	split_normal_value_into_track_values(Type type, const QVariant &value);

	static QVariant
	combine_track_values_into_normal_value(Type type,
										   const QVector<QVariant> &split);

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

	static void validate_vector_string(QStringList *list, int count);

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
	QString to_string() const
	{
		return value<QString>();
	}
	Color to_color() const
	{
		return value<olive::core::Color>();
	}
	QMatrix4x4 to_matrix() const
	{
		return value<QMatrix4x4>();
	}
	VideoParams to_video_params() const
	{
		return value<VideoParams>();
	}
	AudioParams to_audio_params() const
	{
		return value<AudioParams>();
	}
	QVector2D to_vec2() const
	{
		return value<QVector2D>();
	}
	QVector3D to_vec3() const
	{
		return value<QVector3D>();
	}
	QVector4D to_vec4() const
	{
		return value<QVector4D>();
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
	QVariant data_;
	const Node *from_;
	QString tag_;
	bool array_;
};

class NodeValueTable {
public:
	NodeValueTable() = default;

	NodeValue get(NodeValue::Type type, const QString &tag = QString()) const
	{
		QVector<NodeValue::Type> types = { type };
		return get(types, tag);
	}

	NodeValue get(const QVector<NodeValue::Type> &type,
				  const QString &tag = QString()) const;

	NodeValue take(NodeValue::Type type, const QString &tag = QString())
	{
		QVector<NodeValue::Type> types = { type };
		return take(types, tag);
	}

	NodeValue take(const QVector<NodeValue::Type> &type,
				   const QString &tag = QString());

	void push(const NodeValue &value)
	{
		values_.append(value);
	}

	void push(const NodeValueTable &value)
	{
		values_.append(value.values_);
	}

	template <typename T>
	void push(NodeValue::Type type, const T &data, const Node *from,
			  bool array = false, const QString &tag = QString())
	{
		push(NodeValue(type, data, from, array, tag));
	}

	template <typename T>
	void push(NodeValue::Type type, const T &data, const Node *from,
			  const QString &tag)
	{
		push(NodeValue(type, data, from, false, tag));
	}

	void prepend(const NodeValue &value)
	{
		values_.prepend(value);
	}

	template <typename T>
	void prepend(NodeValue::Type type, const T &data, const Node *from,
				 bool array = false, const QString &tag = QString())
	{
		prepend(NodeValue(type, data, from, array, tag));
	}

	template <typename T>
	void prepend(NodeValue::Type type, const T &data, const Node *from,
				 const QString &tag)
	{
		prepend(NodeValue(type, data, from, false, tag));
	}

	const NodeValue &at(int index) const
	{
		return values_.at(index);
	}
	NodeValue take_at(int index)
	{
		return values_.takeAt(index);
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

	bool isEmpty() const
	{
		return values_.isEmpty();
	}

	int get_value_index(const QVector<NodeValue::Type> &type,
					  const QString &tag) const;

	static NodeValueTable merge(QList<NodeValueTable> tables);

private:
	QVector<NodeValue> values_;
};

using NodeValueRow = QHash<QString, NodeValue>;

}

Q_DECLARE_METATYPE(olive::NodeValue)
Q_DECLARE_METATYPE(olive::NodeValueTable)

#endif // OAK_NODEVALUE_H
