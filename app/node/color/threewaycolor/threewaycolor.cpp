/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 mikesolar

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

#include "threewaycolor.h"

#include <QVector3D>

#include "node/project.h"
#include "widget/slider/floatslider.h"

namespace olive
{

#define super Node

const QString ThreeWayColorNode::k_texture_input = QStringLiteral("tex_in");
const QString ThreeWayColorNode::k_shadows_color_input =
	QStringLiteral("shadows_color_in");
const QString ThreeWayColorNode::k_midtones_color_input =
	QStringLiteral("midtones_color_in");
const QString ThreeWayColorNode::k_highlights_color_input =
	QStringLiteral("highlights_color_in");
const QString ThreeWayColorNode::k_shadows_amount_input =
	QStringLiteral("shadows_amount_in");
const QString ThreeWayColorNode::k_midtones_amount_input =
	QStringLiteral("midtones_amount_in");
const QString ThreeWayColorNode::k_highlights_amount_input =
	QStringLiteral("highlights_amount_in");
const QString ThreeWayColorNode::k_luma_coefficients_input =
	QStringLiteral("luma_coefficients_in");

ThreeWayColorNode::ThreeWayColorNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	const QVariant neutral = QVariant::fromValue(Color(0.5, 0.5, 0.5, 1.0));
	add_input(k_shadows_color_input, NodeValue::k_color, neutral);
	add_input(k_midtones_color_input, NodeValue::k_color, neutral);
	add_input(k_highlights_color_input, NodeValue::k_color, neutral);

	add_input(k_shadows_amount_input, NodeValue::k_float, 1.0);
	add_input(k_midtones_amount_input, NodeValue::k_float, 1.0);
	add_input(k_highlights_amount_input, NodeValue::k_float, 1.0);

	const QString min = QStringLiteral("min");
	const QString view = QStringLiteral("view");
	set_input_property(k_shadows_amount_input, min, 0.0);
	set_input_property(k_midtones_amount_input, min, 0.0);
	set_input_property(k_highlights_amount_input, min, 0.0);
	set_input_property(k_shadows_amount_input, view, FloatSlider::k_percentage);
	set_input_property(k_midtones_amount_input, view, FloatSlider::k_percentage);
	set_input_property(k_highlights_amount_input, view, FloatSlider::k_percentage);

	set_effect_input(k_texture_input);
	set_flag(k_video_effect);
}

void ThreeWayColorNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_shadows_color_input, tr("Shadows"));
	set_input_name(k_midtones_color_input, tr("Midtones"));
	set_input_name(k_highlights_color_input, tr("Highlights"));
	set_input_name(k_shadows_amount_input, tr("Shadows Amount"));
	set_input_name(k_midtones_amount_input, tr("Midtones Amount"));
	set_input_name(k_highlights_amount_input, tr("Highlights Amount"));
}

ShaderCode ThreeWayColorNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/threewaycolor.frag"));
}

void ThreeWayColorNode::value(const NodeValueRow &value,
							  const NodeGlobals &globals,
							  NodeValueTable *table) const
{
	Q_UNUSED(globals)

	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		ShaderJob job(value);

		double luma_coeffs[3] = { 0.0, 0.0, 0.0 };
		if (project() && project()->color_manager()) {
			project()->color_manager()->get_default_luma_coefs(luma_coeffs);
		} else {
			luma_coeffs[0] = 0.2126;
			luma_coeffs[1] = 0.7152;
			luma_coeffs[2] = 0.0722;
		}
		job.insert(k_luma_coefficients_input,
				   NodeValue(NodeValue::k_vec3,
							 QVector3D(luma_coeffs[0], luma_coeffs[1],
									   luma_coeffs[2])));

		table->push(NodeValue::k_texture, tex->to_job(job), this);
	}
}

}
