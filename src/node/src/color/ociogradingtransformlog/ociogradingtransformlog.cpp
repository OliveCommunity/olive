/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 Oak Team

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

#include "ociogradingtransformlog.h"

#include <cmath>
#include <iostream>

#include "ocioutils.h"
#include "project.h"
#include "render/colorprocessor.h"
#include "sliderdisplaytype.h"

namespace olive
{

// These ids double as the OCIO GPU uniform names for the dynamic
// GradingPrimaryTransform; do not rename them. OCIO's log style maps the
// classic wheels as: brightness = lift, contrast = gain, gamma = gamma.
const std::string OCIOGradingTransformLogNode::k_lift_input =
	"ocio_grading_primary_brightness";
const std::string OCIOGradingTransformLogNode::k_gain_input =
	"ocio_grading_primary_contrast";
const std::string OCIOGradingTransformLogNode::k_gamma_input =
	"ocio_grading_primary_gamma";
const std::string OCIOGradingTransformLogNode::k_saturation_input =
	"ocio_grading_primary_saturation";
const std::string OCIOGradingTransformLogNode::k_pivot_input =
	"ocio_grading_primary_pivot";
const std::string OCIOGradingTransformLogNode::k_clamp_black_enable_input =
	"clamp_black_enable_in";
const std::string OCIOGradingTransformLogNode::k_clamp_black_input =
	"ocio_grading_primary_clampBlack";
const std::string OCIOGradingTransformLogNode::k_clamp_white_enable_input =
	"clamp_white_enable_in";
const std::string OCIOGradingTransformLogNode::k_clamp_white_input =
	"ocio_grading_primary_clampWhite";

#define super OCIOBaseNode

OCIOGradingTransformLogNode::OCIOGradingTransformLogNode()
{
	add_input(k_lift_input, NodeValue::k_vec4, Vector4D{ 0.0, 0.0, 0.0, 0.0 });
	set_input_property(k_lift_input, "base", 0.01);
	set_vec4_input_colors(k_lift_input);

	add_input(k_gain_input, NodeValue::k_vec4, Vector4D{ 1.0, 1.0, 1.0, 1.0 });
	set_input_property(k_gain_input, "base", 0.01);
	set_vec4_input_colors(k_gain_input);

	add_input(k_gamma_input, NodeValue::k_vec4, Vector4D{ 1.0, 1.0, 1.0, 1.0 });
	set_input_property(k_gamma_input, "base", 0.01);
	set_vec4_input_colors(k_gamma_input);

	add_input(k_saturation_input, NodeValue::k_float, 1.0);
	set_input_property(k_saturation_input, "view", slider::k_percentage);
	set_input_property(k_saturation_input, "min", 0.0);

	add_input(k_pivot_input, NodeValue::k_float,
			 -0.2); // Default for GRADING_LOG listed in ocio::GradingPrimary
	set_input_property(k_pivot_input, "base", 0.01);

	add_input(k_clamp_black_enable_input, NodeValue::k_boolean, false);

	add_input(k_clamp_black_input, NodeValue::k_float, 0.0);
	set_input_property(k_clamp_black_input, "enabled",
					 get_standard_value(k_clamp_black_enable_input).to_bool());
	set_input_property(k_clamp_black_input, "base", 0.01);

	add_input(k_clamp_white_enable_input, NodeValue::k_boolean, false);

	add_input(k_clamp_white_input, NodeValue::k_float, 1.0);
	set_input_property(k_clamp_white_input, "enabled",
					 get_standard_value(k_clamp_white_enable_input).to_bool());
	set_input_property(k_clamp_white_input, "base", 0.01);

	// Constrain the white clamp minimum to just above the (static) black clamp
	// as per ocio::GradingPrimary::validate. When the black clamp is keyframed
	// or connected, Value() enforces the invariant per frame instead.
	update_clamp_white_minimum();
}

std::string OCIOGradingTransformLogNode::name() const
{
	return "OCIO Color Grading (Log)";
}

std::string OCIOGradingTransformLogNode::id() const
{
	return "org.olivevideoeditor.Olive.ociogradingtransformlog";
}

std::vector<Node::CategoryID> OCIOGradingTransformLogNode::category() const
{
	return { k_category_color };
}

std::string OCIOGradingTransformLogNode::description() const
{
	return "Lift/gamma/gain color grading using OpenColorIO.";
}

void OCIOGradingTransformLogNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_lift_input, "Lift");
	set_input_name(k_gain_input, "Gain");
	set_input_name(k_gamma_input, "Gamma");
	set_input_name(k_saturation_input, "Saturation");
	set_input_name(k_pivot_input, "Pivot");
	set_input_name(k_clamp_black_enable_input, "Enable Black Clamp");
	set_input_name(k_clamp_black_input, "Black Clamp");
	set_input_name(k_clamp_white_enable_input, "Enable White Clamp");
	set_input_name(k_clamp_white_input, "White Clamp");
}

void OCIOGradingTransformLogNode::InputValueChangedEvent(const std::string &input,
														 int element)
{
	(void) element;

	if (input == k_clamp_white_enable_input) {
		set_input_property(k_clamp_white_input, "enabled",
						 get_standard_value(k_clamp_white_enable_input).to_bool());
	} else if (input == k_clamp_black_enable_input) {
		set_input_property(k_clamp_black_input, "enabled",
						 get_standard_value(k_clamp_black_enable_input).to_bool());
	} else if (input == k_clamp_black_input) {
		// Ensure the white clamp is always greater than the black clamp as per
		// ocio::GradingPrimary::validate
		update_clamp_white_minimum();
	}

	generate_processor();
}

void OCIOGradingTransformLogNode::InputConnectedEvent(const std::string &input,
													  int element, Node *output)
{
	super::InputConnectedEvent(input, element, output);

	if (input == k_clamp_black_input) {
		update_clamp_white_minimum();
	}
}

void OCIOGradingTransformLogNode::InputDisconnectedEvent(const std::string &input,
														 int element,
														 Node *output)
{
	super::InputDisconnectedEvent(input, element, output);

	if (input == k_clamp_black_input) {
		update_clamp_white_minimum();
	}
}

void OCIOGradingTransformLogNode::update_clamp_white_minimum()
{
	// A static UI minimum cannot follow an animated black clamp; for keyframed
	// or connected values the white>black invariant is enforced per frame in
	// Value() instead
	if (is_input_keyframing(k_clamp_black_input) ||
		is_input_connected(k_clamp_black_input)) {
		return;
	}

	set_input_property(k_clamp_white_input, "min",
					 get_standard_value(k_clamp_black_input).to_double() + 0.000001);
}

void OCIOGradingTransformLogNode::generate_processor()
{
	if (manager()) {
		ocio::GradingPrimaryTransformRcPtr gp =
			ocio::GradingPrimaryTransform::Create(ocio::GRADING_LOG);
		gp->makeDynamic();
		gp->setDirection(ocio::TransformDirection::TRANSFORM_DIR_FORWARD);

		try {
			set_processor(ColorProcessor::create(
				manager()->get_config()->getProcessor(gp)));
		} catch (const ocio::Exception &e) {
			std::cerr << std::endl << e.what() << std::endl;
		}
	}
}

void OCIOGradingTransformLogNode::value(const NodeValueRow &value,
										const NodeGlobals &globals,
										NodeValueTable *table) const
{
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		if (processor()) {
			ColorTransformJob job(value);

			job.set_color_processor(processor());
			job.set_input_texture(value.at(k_texture_input));

			// Vector4D components stand in for the former QVector4D indices:
			// x = master channel, y = red, z = green, w = blue.

			// OCIO expects vec3s on the GPU but RGBMs (master + RGB) on the
			// CPU; the per-style master combination below mirrors
			// ocio::GradingPrimary. Lift is additive, gain/gamma multiply.
			Vector4D lift = value.at(k_lift_input).to_vec4();
			lift.set_y(lift.y() + lift.x());
			lift.set_z(lift.z() + lift.x());
			lift.set_w(lift.w() + lift.x());
			job.insert(k_lift_input,
					   NodeValue(NodeValue::k_vec3,
								 Vector3D(lift.y(), lift.z(), lift.w())));

			Vector4D gain = value.at(k_gain_input).to_vec4();
			gain.set_y(gain.y() * gain.x());
			gain.set_z(gain.z() * gain.x());
			gain.set_w(gain.w() * gain.x());
			job.insert(k_gain_input,
					   NodeValue(NodeValue::k_vec3,
								 Vector3D(gain.y(), gain.z(), gain.w())));

			Vector4D gamma = value.at(k_gamma_input).to_vec4();
			gamma.set_y(gamma.y() * gamma.x());
			gamma.set_z(gamma.z() * gamma.x());
			gamma.set_w(gamma.w() * gamma.x());
			job.insert(k_gamma_input,
					   NodeValue(NodeValue::k_vec3,
								 Vector3D(gamma.y(), gamma.z(), gamma.w())));

			if (!value.at(k_clamp_black_enable_input).to_bool()) {
				job.insert(k_clamp_black_input,
						   NodeValue(NodeValue::k_float,
									 ocio::GradingPrimary::NoClampBlack()));
			}

			if (!value.at(k_clamp_white_enable_input).to_bool()) {
				job.insert(k_clamp_white_input,
						   NodeValue(NodeValue::k_float,
									 ocio::GradingPrimary::NoClampWhite()));
			}

			if (value.at(k_clamp_black_enable_input).to_bool() &&
				value.at(k_clamp_white_enable_input).to_bool()) {
				// ocio::GradingPrimary::validate requires the white clamp to be
				// greater than the black clamp. Keyframed or connected values
				// can violate this at arbitrary times, so enforce the invariant
				// per frame here.
				const double clamp_black = value.at(k_clamp_black_input).to_double();
				const double clamp_white = value.at(k_clamp_white_input).to_double();
				if (clamp_white <= clamp_black) {
					job.insert(k_clamp_white_input,
							   NodeValue(NodeValue::k_float,
										 clamp_black + 0.000001));
				}
			}

			table->push(NodeValue::k_texture, tex->to_job(job), this);
		}
	}
}

void OCIOGradingTransformLogNode::config_changed()
{
	generate_processor();
}

void OCIOGradingTransformLogNode::set_vec4_input_colors(const std::string &input)
{
	set_input_property(input, "color0", "#c0c0c0");
	set_input_property(input, "color1", "#ff0000");
	set_input_property(input, "color2", "#00ff00");
	set_input_property(input, "color3", "#0000ff");
}

}
