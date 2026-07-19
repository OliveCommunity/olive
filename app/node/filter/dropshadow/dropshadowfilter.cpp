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

#include "dropshadowfilter.h"

#include "widget/slider/floatslider.h"

namespace olive
{

#define super Node

const QString DropShadowFilter::k_texture_input = QStringLiteral("tex_in");
const QString DropShadowFilter::k_color_input = QStringLiteral("color_in");
const QString DropShadowFilter::k_distance_input = QStringLiteral("distance_in");
const QString DropShadowFilter::k_angle_input = QStringLiteral("angle_in");
const QString DropShadowFilter::k_softness_input = QStringLiteral("radius_in");
const QString DropShadowFilter::k_opacity_input = QStringLiteral("opacity_in");
const QString DropShadowFilter::k_fast_input = QStringLiteral("fast_in");

DropShadowFilter::DropShadowFilter()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_color_input, NodeValue::k_color,
			 QVariant::fromValue(Color(0.0, 0.0, 0.0)));

	add_input(k_distance_input, NodeValue::k_float, 10.0);

	add_input(k_angle_input, NodeValue::k_float, 135.0);

	add_input(k_softness_input, NodeValue::k_float, 10.0);
	set_input_property(k_softness_input, QStringLiteral("min"), 0.0);

	add_input(k_opacity_input, NodeValue::k_float, 1.0);
	set_input_property(k_opacity_input, QStringLiteral("min"), 0.0);
	set_input_property(k_opacity_input, QStringLiteral("view"),
					 FloatSlider::k_percentage);

	add_input(k_fast_input, NodeValue::k_boolean, false);

	set_effect_input(k_texture_input);
	set_flag(k_video_effect);
}

void DropShadowFilter::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Texture"));
	set_input_name(k_color_input, tr("Color"));
	set_input_name(k_distance_input, tr("Distance"));
	set_input_name(k_angle_input, tr("Angle"));
	set_input_name(k_softness_input, tr("Softness"));
	set_input_name(k_opacity_input, tr("Opacity"));
	set_input_name(k_fast_input, tr("Faster (Lower Quality)"));
}

ShaderCode DropShadowFilter::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/dropshadow.frag"));
}

void DropShadowFilter::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		ShaderJob job(value);

		QString iterative = QStringLiteral("previous_iteration_in");

		job.insert(QStringLiteral("resolution_in"),
				   NodeValue(NodeValue::k_vec2, tex->virtual_resolution(),
							 this));
		job.insert(iterative, value[k_texture_input]);

		if (!qIsNull(value[k_softness_input].to_double())) {
			job.set_iterations(3, iterative);
		}

		table->push(NodeValue::k_texture, tex->to_job(job), this);
	}
}

}
