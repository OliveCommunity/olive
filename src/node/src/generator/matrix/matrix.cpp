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

#include "sliderdisplaytype.h"

namespace olive
{

const std::string MatrixGenerator::k_position_input = "pos_in";
const std::string MatrixGenerator::k_rotation_input = "rot_in";
const std::string MatrixGenerator::k_scale_input = "scale_in";
const std::string MatrixGenerator::k_uniform_scale_input = "uniform_scale_in";
const std::string MatrixGenerator::k_anchor_input = "anchor_in";

#define super Node

MatrixGenerator::MatrixGenerator()
{
	add_input(k_position_input, NodeValue::k_vec2, Vector2D(0.0, 0.0));

	add_input(k_rotation_input, NodeValue::k_float, 0.0);

	add_input(k_scale_input, NodeValue::k_vec2, Vector2D(1.0f, 1.0f));
	set_input_property(k_scale_input, "min", Vector2D(0, 0));
	set_input_property(k_scale_input, "view", slider::k_percentage);
	set_input_property(k_scale_input, "disable1", true);

	add_input(k_uniform_scale_input, NodeValue::k_boolean, true,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_anchor_input, NodeValue::k_vec2, Vector2D(0.0, 0.0));
}

std::string MatrixGenerator::name() const
{
	return "Orthographic Matrix";
}

std::string MatrixGenerator::short_name() const
{
	return "Ortho";
}

std::string MatrixGenerator::id() const
{
	return "org.olivevideoeditor.Olive.ortho";
}

std::vector<Node::CategoryID> MatrixGenerator::category() const
{
	return { k_category_generator, k_category_math };
}

std::string MatrixGenerator::description() const
{
	return "Generate an orthographic matrix using position, rotation, and scale.";
}

void MatrixGenerator::retranslate()
{
	super::retranslate();

	set_input_name(k_position_input, "Position");
	set_input_name(k_rotation_input, "Rotation");
	set_input_name(k_scale_input, "Scale");
	set_input_name(k_uniform_scale_input, "Uniform Scale");
	set_input_name(k_anchor_input, "Anchor Point");
}

void MatrixGenerator::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	// Push matrix output
	Matrix4x4 mat = generate_matrix(value, false, false, false, Matrix4x4());
	table->push(NodeValue::k_matrix, mat, this);
}

Matrix4x4 MatrixGenerator::generate_matrix(const NodeValueRow &value,
										   bool ignore_anchor,
										   bool ignore_position,
										   bool ignore_scale,
										   const Matrix4x4 &mat) const
{
	Vector2D anchor;
	Vector2D position;
	Vector2D scale;

	if (!ignore_anchor) {
		anchor = value.at(k_anchor_input).to_vec2();
	}

	if (!ignore_scale) {
		scale = value.at(k_scale_input).to_vec2();
	}

	if (!ignore_position) {
		position = value.at(k_position_input).to_vec2();
	}

	return generate_matrix(position, value.at(k_rotation_input).to_double(), scale,
						  value.at(k_uniform_scale_input).to_bool(), anchor, mat);
}

Matrix4x4
MatrixGenerator::generate_matrix(const Vector2D &pos, const float &rot,
								const Vector2D &scale, bool uniform_scale,
								const Vector2D &anchor, Matrix4x4 mat)
{
	// Position
	mat.translate(pos.x(), pos.y());

	// Rotation (2D rotation around the Z axis, same as QMatrix4x4::rotate(rot, 0, 0, 1))
	mat.rotate(rot);

	// Scale (convert to a Vector3D so that the identity matrix is preserved if all values are 1.0f)
	Vector3D full_scale;
	if (uniform_scale) {
		full_scale = Vector3D(scale.x(), scale.x(), 1.0f);
	} else {
		full_scale = Vector3D(scale.x(), scale.y(), 1.0f);
	}
	mat.scale(full_scale.x(), full_scale.y(), full_scale.z());

	// Anchor Point
	mat.translate(-anchor.x(), -anchor.y());

	return mat;
}

void MatrixGenerator::InputValueChangedEvent(const std::string &input, int element)
{
	(void) element;

	if (input == k_uniform_scale_input) {
		set_input_property(k_scale_input, "disable1",
						 get_standard_value(k_uniform_scale_input).to_bool());
	}
}

}
