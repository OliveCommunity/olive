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

#include <QCoreApplication>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include "common/tohex.h"
#include "render/subtitleparams.h"
#include "render/videoparams.h"

namespace olive
{

QString NodeValue::value_to_string(Type data_type, const QVariant &value,
								 bool value_is_a_key_track)
{
	if (!value_is_a_key_track && data_type == k_vec2) {
		QVector2D vec = value.value<QVector2D>();

		return QStringLiteral("%1:%2").arg(QString::number(vec.x()),
										   QString::number(vec.y()));
	} else if (!value_is_a_key_track && data_type == k_vec3) {
		QVector3D vec = value.value<QVector3D>();

		return QStringLiteral("%1:%2:%3")
			.arg(QString::number(vec.x()), QString::number(vec.y()),
				 QString::number(vec.z()));
	} else if (!value_is_a_key_track && data_type == k_vec4) {
		QVector4D vec = value.value<QVector4D>();

		return QStringLiteral("%1:%2:%3:%4")
			.arg(QString::number(vec.x()), QString::number(vec.y()),
				 QString::number(vec.z()), QString::number(vec.w()));
	} else if (!value_is_a_key_track && data_type == k_color) {
		Color c = value.value<Color>();

		return QStringLiteral("%1:%2:%3:%4")
			.arg(QString::number(c.red()), QString::number(c.green()),
				 QString::number(c.blue()), QString::number(c.alpha()));
	} else if (!value_is_a_key_track && data_type == k_bezier) {
		Bezier b = value.value<Bezier>();

		return QStringLiteral("%1:%2:%3:%4:%5:%6")
			.arg(QString::number(b.x()), QString::number(b.y()),
				 QString::number(b.cp1_x()), QString::number(b.cp1_y()),
				 QString::number(b.cp2_x()), QString::number(b.cp2_y()));
	} else if (data_type == k_rational) {
		return QString::fromStdString(value.value<Rational>().to_string());
	} else if (data_type == k_texture || data_type == k_samples ||
			   data_type == k_none) {
		// These data types need no XML representation
		return QString();
	} else if (data_type == k_int) {
		return QString::number(value.value<int64_t>());
	} else if (data_type == k_binary) {
		return value.toByteArray().toBase64();
	} else {
		if (value.canConvert<QString>()) {
			return value.toString();
		}

		if (!value.isNull()) {
			qWarning()
				<< "Failed to convert type" << to_hex(data_type) << "to string";
		}

		return QString();
	}
}

QVector<QVariant>
NodeValue::split_normal_value_into_track_values(Type type,
												const QVariant &value)
{
	QVector<QVariant> vals(get_number_of_keyframe_tracks(type));

	switch (type) {
	case k_vec2: {
		QVector2D vec = value.value<QVector2D>();
		vals.replace(0, vec.x());
		vals.replace(1, vec.y());
		break;
	}
	case k_vec3: {
		QVector3D vec = value.value<QVector3D>();
		vals.replace(0, vec.x());
		vals.replace(1, vec.y());
		vals.replace(2, vec.z());
		break;
	}
	case k_vec4: {
		QVector4D vec = value.value<QVector4D>();
		vals.replace(0, vec.x());
		vals.replace(1, vec.y());
		vals.replace(2, vec.z());
		vals.replace(3, vec.w());
		break;
	}
	case k_color: {
		Color c = value.value<Color>();
		vals.replace(0, c.red());
		vals.replace(1, c.green());
		vals.replace(2, c.blue());
		vals.replace(3, c.alpha());
		break;
	}
	case k_bezier: {
		Bezier b = value.value<Bezier>();
		vals.replace(0, b.x());
		vals.replace(1, b.y());
		vals.replace(2, b.cp1_x());
		vals.replace(3, b.cp1_y());
		vals.replace(4, b.cp2_x());
		vals.replace(5, b.cp2_y());
		break;
	}
	default:
		vals.replace(0, value);
	}

	return vals;
}

QVariant NodeValue::combine_track_values_into_normal_value(
	Type type, const QVector<QVariant> &split)
{
	if (split.isEmpty()) {
		return QVariant();
	}

	switch (type) {
	case k_vec2: {
		return QVector2D(split.at(0).toFloat(), split.at(1).toFloat());
	}
	case k_vec3: {
		return QVector3D(split.at(0).toFloat(), split.at(1).toFloat(),
						 split.at(2).toFloat());
	}
	case k_vec4: {
		return QVector4D(split.at(0).toFloat(), split.at(1).toFloat(),
						 split.at(2).toFloat(), split.at(3).toFloat());
	}
	case k_color: {
		return QVariant::fromValue(
			Color(split.at(0).toFloat(), split.at(1).toFloat(),
				  split.at(2).toFloat(), split.at(3).toFloat()));
	}
	case k_bezier:
		return QVariant::fromValue(
			Bezier(split.at(0).toDouble(), split.at(1).toDouble(),
				   split.at(2).toDouble(), split.at(3).toDouble(),
				   split.at(4).toDouble(), split.at(5).toDouble()));
	default:
		return split.first();
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

QVariant NodeValue::string_to_value(Type data_type, const QString &string,
								  bool value_is_a_key_track)
{
	if (!value_is_a_key_track && data_type == k_vec2) {
		QStringList vals = string.split(':');

		validate_vector_string(&vals, 2);

		return QVector2D(vals.at(0).toFloat(), vals.at(1).toFloat());
	} else if (!value_is_a_key_track && data_type == k_vec3) {
		QStringList vals = string.split(':');

		validate_vector_string(&vals, 3);

		return QVector3D(vals.at(0).toFloat(), vals.at(1).toFloat(),
						 vals.at(2).toFloat());
	} else if (!value_is_a_key_track && data_type == k_vec4) {
		QStringList vals = string.split(':');

		validate_vector_string(&vals, 4);

		return QVector4D(vals.at(0).toFloat(), vals.at(1).toFloat(),
						 vals.at(2).toFloat(), vals.at(3).toFloat());
	} else if (!value_is_a_key_track && data_type == k_color) {
		QStringList vals = string.split(':');

		validate_vector_string(&vals, 4);

		return QVariant::fromValue(
			Color(vals.at(0).toDouble(), vals.at(1).toDouble(),
				  vals.at(2).toDouble(), vals.at(3).toDouble()));
	} else if (!value_is_a_key_track && data_type == k_bezier) {
		QStringList vals = string.split(':');

		validate_vector_string(&vals, 6);

		return QVariant::fromValue(
			Bezier(vals.at(0).toDouble(), vals.at(1).toDouble(),
				   vals.at(2).toDouble(), vals.at(3).toDouble(),
				   vals.at(4).toDouble(), vals.at(5).toDouble()));
	} else if (data_type == k_int) {
		return QVariant::fromValue(string.toLongLong());
	} else if (data_type == k_rational) {
		return QVariant::fromValue(Rational::from_string(string.toStdString()));
	} else if (data_type == k_binary) {
		return QByteArray::fromBase64(string.toLatin1());
	} else {
		return string;
	}
}

void NodeValue::validate_vector_string(QStringList *list, int count)
{
	while (list->size() < count) {
		list->append(QStringLiteral("0"));
	}
}

QString NodeValue::get_pretty_data_type_name(Type type)
{
	switch (type) {
	case k_none:
		return QCoreApplication::translate("NodeValue", "None");
	case k_int:
	case k_combo:
		return QCoreApplication::translate("NodeValue", "Integer");
	case k_str_combo:
		return QCoreApplication::translate("NodeValue", "String Combo");
	case k_float:
		return QCoreApplication::translate("NodeValue", "Float");
	case k_rational:
		return QCoreApplication::translate("NodeValue", "Rational");
	case k_boolean:
		return QCoreApplication::translate("NodeValue", "Boolean");
	case k_color:
		return QCoreApplication::translate("NodeValue", "Color");
	case k_matrix:
		return QCoreApplication::translate("NodeValue", "Matrix");
	case k_text:
		return QCoreApplication::translate("NodeValue", "Text");
	case k_font:
		return QCoreApplication::translate("NodeValue", "Font");
	case k_file:
		return QCoreApplication::translate("NodeValue", "File");
	case k_texture:
		return QCoreApplication::translate("NodeValue", "Texture");
	case k_samples:
		return QCoreApplication::translate("NodeValue", "Samples");
	case k_vec2:
		return QCoreApplication::translate("NodeValue", "Vector 2D");
	case k_vec3:
		return QCoreApplication::translate("NodeValue", "Vector 3D");
	case k_vec4:
		return QCoreApplication::translate("NodeValue", "Vector 4D");
	case k_bezier:
		return QCoreApplication::translate("NodeValue", "Bezier");
	case k_video_params:
		return QCoreApplication::translate("NodeValue", "Video Parameters");
	case k_audio_params:
		return QCoreApplication::translate("NodeValue", "Audio Parameters");
	case k_subtitle_params:
		return QCoreApplication::translate("NodeValue", "Subtitle Parameters");
	case k_binary:
		return QCoreApplication::translate("NodeValue", "Binary");
	case k_push_button:
		return QCoreApplication::translate("NodeValue", "Push Button");

	case k_data_type_count:
		break;
	}

	return QCoreApplication::translate("NodeValue", "Unknown");
}

QString NodeValue::get_data_type_name(Type type)
{
	switch (type) {
	case k_none:
		return QStringLiteral("none");
	case k_int:
		return QStringLiteral("int");
	case k_combo:
		return QStringLiteral("combo");
	case k_str_combo:
		return QStringLiteral("strcombo");
	case k_float:
		return QStringLiteral("float");
	case k_rational:
		return QStringLiteral("Rational");
	case k_boolean:
		return QStringLiteral("bool");
	case k_color:
		return QStringLiteral("color");
	case k_matrix:
		return QStringLiteral("matrix");
	case k_text:
		return QStringLiteral("text");
	case k_font:
		return QStringLiteral("font");
	case k_file:
		return QStringLiteral("file");
	case k_texture:
		return QStringLiteral("texture");
	case k_samples:
		return QStringLiteral("samples");
	case k_vec2:
		return QStringLiteral("vec2");
	case k_vec3:
		return QStringLiteral("vec3");
	case k_vec4:
		return QStringLiteral("vec4");
	case k_bezier:
		return QStringLiteral("bezier");
	case k_video_params:
		return QStringLiteral("vparam");
	case k_audio_params:
		return QStringLiteral("aparam");
	case k_subtitle_params:
		return QStringLiteral("sparam");
	case k_binary:
		return QStringLiteral("binary");
	case k_push_button:
		return QStringLiteral("pushbutton");
	case k_data_type_count:
		break;
	}

	return QString();
}

NodeValue::Type NodeValue::get_data_type_from_name(const QString &n)
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

NodeValue NodeValueTable::get(const QVector<NodeValue::Type> &type,
							  const QString &tag) const
{
	int value_index = get_value_index(type, tag);

	if (value_index >= 0) {
		return values_.at(value_index);
	}

	return NodeValue();
}

NodeValue NodeValueTable::take(const QVector<NodeValue::Type> &type,
							   const QString &tag)
{
	int value_index = get_value_index(type, tag);

	if (value_index >= 0) {
		return values_.takeAt(value_index);
	}

	return NodeValue();
}

bool NodeValueTable::has(NodeValue::Type type) const
{
	for (int i = values_.size() - 1; i >= 0; i--) {
		const NodeValue &v = values_.at(i);

		if (v.type() == type) {
			return true;
		}
	}

	return false;
}

void NodeValueTable::remove(const NodeValue &v)
{
	for (int i = values_.size() - 1; i >= 0; i--) {
		const NodeValue &compare = values_.at(i);

		if (compare == v) {
			values_.removeAt(i);
			return;
		}
	}
}

NodeValueTable NodeValueTable::merge(QList<NodeValueTable> tables)
{
	if (tables.size() == 1) {
		return tables.first();
	}

	int row = 0;

	NodeValueTable merged_table;

	// Slipstreams all tables together
	while (true) {
		bool all_merged = true;

		foreach (const NodeValueTable &t, tables) {
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

int NodeValueTable::get_value_index(const QVector<NodeValue::Type> &types,
								  const QString &tag) const
{
	int index = -1;

	for (int i = values_.size() - 1; i >= 0; i--) {
		const NodeValue &v = values_.at(i);

		if (types.contains(v.type()) && (tag.isEmpty() || tag == v.tag())) {
			index = i;
			break;
		}
	}

	return index;
}

}
