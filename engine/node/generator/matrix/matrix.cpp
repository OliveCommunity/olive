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

#include "matrix.h"

#include <QMatrix4x4>
#include <QVector2D>

#include "node/sliderdisplaytype.h"

namespace olive
{

const QString MatrixGenerator::k_position_input = QStringLiteral("pos_in");
const QString MatrixGenerator::k_rotation_input = QStringLiteral("rot_in");
const QString MatrixGenerator::k_scale_input = QStringLiteral("scale_in");
const QString MatrixGenerator::k_uniform_scale_input =
	QStringLiteral("uniform_scale_in");
const QString MatrixGenerator::k_anchor_input = QStringLiteral("anchor_in");

#define super Node

MatrixGenerator::MatrixGenerator()
{
	add_input(k_position_input, NodeValue::k_vec2, QVector2D(0.0, 0.0));

	add_input(k_rotation_input, NodeValue::k_float, 0.0);

	add_input(k_scale_input, NodeValue::k_vec2, QVector2D(1.0f, 1.0f));
	set_input_property(k_scale_input, QStringLiteral("min"), QVector2D(0, 0));
	set_input_property(k_scale_input, QStringLiteral("view"),
					 slider::k_percentage);
	set_input_property(k_scale_input, QStringLiteral("disable1"), true);

	add_input(k_uniform_scale_input, NodeValue::k_boolean, true,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_anchor_input, NodeValue::k_vec2, QVector2D(0.0, 0.0));
}

QString MatrixGenerator::name() const
{
	return tr("Orthographic Matrix");
}

QString MatrixGenerator::short_name() const
{
	return tr("Ortho");
}

QString MatrixGenerator::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.ortho");
}

QVector<Node::CategoryID> MatrixGenerator::category() const
{
	return { k_category_generator, k_category_math };
}

QString MatrixGenerator::description() const
{
	return tr(
		"Generate an orthographic matrix using position, rotation, and scale.");
}

void MatrixGenerator::retranslate()
{
	super::retranslate();

	set_input_name(k_position_input, tr("Position"));
	set_input_name(k_rotation_input, tr("Rotation"));
	set_input_name(k_scale_input, tr("Scale"));
	set_input_name(k_uniform_scale_input, tr("Uniform Scale"));
	set_input_name(k_anchor_input, tr("Anchor Point"));
}

void MatrixGenerator::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	// Push matrix output
	QMatrix4x4 mat = generate_matrix(value, false, false, false, QMatrix4x4());
	table->push(NodeValue::k_matrix, mat, this);
}

QMatrix4x4 MatrixGenerator::generate_matrix(const NodeValueRow &value,
										   bool ignore_anchor,
										   bool ignore_position,
										   bool ignore_scale,
										   const QMatrix4x4 &mat) const
{
	QVector2D anchor;
	QVector2D position;
	QVector2D scale;

	if (!ignore_anchor) {
		anchor = value[k_anchor_input].to_vec2();
	}

	if (!ignore_scale) {
		scale = value[k_scale_input].to_vec2();
	}

	if (!ignore_position) {
		position = value[k_position_input].to_vec2();
	}

	return generate_matrix(position, value[k_rotation_input].to_double(), scale,
						  value[k_uniform_scale_input].to_bool(), anchor, mat);
}

QMatrix4x4
MatrixGenerator::generate_matrix(const QVector2D &pos, const float &rot,
								const QVector2D &scale, bool uniform_scale,
								const QVector2D &anchor, QMatrix4x4 mat)
{
	// Position
	mat.translate(pos.x(), pos.y());

	// Rotation
	mat.rotate(rot, 0, 0, 1);

	// Scale (convert to a QVector3D so that the identity matrix is preserved if all values are 1.0f)
	QVector3D full_scale;
	if (uniform_scale) {
		full_scale = QVector3D(scale.x(), scale.x(), 1.0f);
	} else {
		full_scale = QVector3D(scale, 1.0f);
	}
	mat.scale(full_scale);

	// Anchor Point
	mat.translate(-anchor.x(), -anchor.y());

	return mat;
}

void MatrixGenerator::InputValueChangedEvent(const QString &input, int element)
{
	Q_UNUSED(element)

	if (input == k_uniform_scale_input) {
		set_input_property(k_scale_input, QStringLiteral("disable1"),
						 get_standard_value(k_uniform_scale_input).toBool());
	}
}

}
