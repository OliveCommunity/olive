/***

  Oak - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#include "whitebalance.h"

#include <algorithm>
#include <cmath>

#include "filefunctions.h"
#include "render/job/shaderjob.h"
#include "render/texture.h"
#include "sliderdisplaytype.h"

namespace olive
{

#define super Node

const std::string WhiteBalanceNode::k_texture_input = "tex_in";
const std::string WhiteBalanceNode::k_temperature_input = "temperature_in";
const std::string WhiteBalanceNode::k_tint_input = "tint_in";
const std::string WhiteBalanceNode::k_gain_input = "wb_gain_in";

WhiteBalanceNode::WhiteBalanceNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_temperature_input, NodeValue::k_float, 6500.0);
	set_input_property(k_temperature_input, "min", 1000.0);
	set_input_property(k_temperature_input, "max", 40000.0);
	set_input_property(k_temperature_input, "view", slider::k_normal);

	add_input(k_tint_input, NodeValue::k_float, 0.0);
	set_input_property(k_tint_input, "min", -1.0);
	set_input_property(k_tint_input, "max", 1.0);
	set_input_property(k_tint_input, "base", 0.01);

	set_effect_input(k_texture_input);
	set_flag(k_video_effect);
}

std::string WhiteBalanceNode::name() const
{
	return "White Balance";
}

std::string WhiteBalanceNode::id() const
{
	return "org.olivevideoeditor.Olive.whitebalance";
}

std::vector<Node::CategoryID> WhiteBalanceNode::category() const
{
	return { k_category_color };
}

std::string WhiteBalanceNode::description() const
{
	return "Adjust white balance by color temperature and tint.";
}

void WhiteBalanceNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_temperature_input, "Temperature (K)");
	set_input_name(k_tint_input, "Tint");
}

ShaderCode WhiteBalanceNode::get_shader_code(const ShaderRequest &request) const
{
	(void) request;
	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/whitebalance.frag"));
}

void WhiteBalanceNode::value(const NodeValueRow &value,
							 const NodeGlobals &globals,
							 NodeValueTable *table) const
{
	(void) globals;

	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		ShaderJob job(value);

		const Vector3D gain = get_gain_for_temperature(
			value.at(k_temperature_input).to_double(),
			value.at(k_tint_input).to_double());
		job.insert(k_gain_input, NodeValue(NodeValue::k_vec3, gain));

		table->push(NodeValue::k_texture, tex->to_job(job), this);
	}
}

Vector3D WhiteBalanceNode::get_gain_for_temperature(double kelvin, double tint)
{
	// Tanner Helland blackbody approximation (1000K-40000K), returning
	// 0-255 per channel
	kelvin = std::max(1000.0, std::min(kelvin, 40000.0));
	const double t = kelvin / 100.0;

	double red;
	if (t <= 66.0) {
		red = 255.0;
	} else {
		red = 329.698727446 * std::pow(t - 60.0, -0.1332047592);
	}

	double green;
	if (t <= 66.0) {
		green = 99.4708025861 * std::log(t) - 161.1195681661;
	} else {
		green = 288.1221695283 * std::pow(t - 60.0, -0.0755148492);
	}

	double blue;
	if (t >= 66.0) {
		blue = 255.0;
	} else if (t <= 19.0) {
		blue = 0.0;
	} else {
		blue = 138.5177312231 * std::log(t - 10.0) - 305.0447927307;
	}

	// Normalize to the green channel so temperature shifts do not change
	// exposure, then let tint move along the green-magenta axis
	red /= green;
	blue /= green;
	green = 1.0;

	const double tint_gain = std::max(0.0, std::min(1.0 + tint, 2.0));

	return Vector3D(float(red), float(green * tint_gain), float(blue));
}

}
