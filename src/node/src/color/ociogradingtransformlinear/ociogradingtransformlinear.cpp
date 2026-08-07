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

#include "ociogradingtransformlinear.h"
#include "node/colormanager.h"
#include "render/color.h"

#include <cmath>
#include <iostream>

#include "ocioutils.h"
#include "project.h"
#include "render/colorprocessor.h"
#include "sliderdisplaytype.h"

namespace olive
{

const std::string OCIOGradingTransformLinearNode::k_contrast_input =
	"ocio_grading_primary_contrast";
const std::string OCIOGradingTransformLinearNode::k_offset_input =
	"ocio_grading_primary_offset";
const std::string OCIOGradingTransformLinearNode::k_exposure_input =
	"ocio_grading_primary_exposure";
const std::string OCIOGradingTransformLinearNode::k_saturation_input =
	"ocio_grading_primary_saturation";
const std::string OCIOGradingTransformLinearNode::k_pivot_input =
	"ocio_grading_primary_pivot";
const std::string OCIOGradingTransformLinearNode::k_clamp_black_enable_input =
	"clamp_black_enable_in";
const std::string OCIOGradingTransformLinearNode::k_clamp_black_input =
	"ocio_grading_primary_clampBlack";
const std::string OCIOGradingTransformLinearNode::k_clamp_white_enable_input =
	"clamp_white_enable_in";
const std::string OCIOGradingTransformLinearNode::k_clamp_white_input =
	"ocio_grading_primary_clampWhite";

#define super OCIOBaseNode

OCIOGradingTransformLinearNode::OCIOGradingTransformLinearNode()
{
	add_input(k_contrast_input, NodeValue::k_vec4, Vector4D{ 1.0, 1.0, 1.0, 1.0 });
	// Minimum based on ocio::GradingPrimary::validate
	set_input_property(k_contrast_input, "min",
					 Vector4D{ 0.01f, 0.01f, 0.01f, 0.01f });
	set_input_property(k_contrast_input, "base", 0.01);
	set_vec4_input_colors(k_contrast_input);

	add_input(k_offset_input, NodeValue::k_vec4, Vector4D{ 0.0, 0.0, 0.0, 0.0 });
	set_input_property(k_offset_input, "base", 0.01);
	set_vec4_input_colors(k_offset_input);

	add_input(k_exposure_input, NodeValue::k_vec4, Vector4D{ 0.0, 0.0, 0.0, 0.0 });
	set_input_property(k_exposure_input, "base", 0.01);
	set_vec4_input_colors(k_exposure_input);

	add_input(k_saturation_input, NodeValue::k_float, 1.0);
	set_input_property(k_saturation_input, "view", slider::k_percentage);
	set_input_property(k_saturation_input, "min", 0.0);

	add_input(k_pivot_input, NodeValue::k_float,
			 0.18); // Default listed in ocio::GradingPrimary
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

std::string OCIOGradingTransformLinearNode::name() const
{
	return "OCIO Color Grading (Linear)";
}

std::string OCIOGradingTransformLinearNode::id() const
{
	return "org.olivevideoeditor.Olive.ociogradingtransformlinear";
}

std::vector<Node::CategoryID> OCIOGradingTransformLinearNode::category() const
{
	return { k_category_color };
}

std::string OCIOGradingTransformLinearNode::description() const
{
	return "Simple linear color grading using OpenColorIO.";
}

void OCIOGradingTransformLinearNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, "Input");
	set_input_name(k_contrast_input, "Contrast");
	set_input_name(k_offset_input, "Offset");
	set_input_name(k_exposure_input, "Exposure");
	set_input_property(k_exposure_input, "tooltip",
					 "Exposure increments in stops.");
	set_input_name(k_saturation_input, "Saturation");
	set_input_name(k_pivot_input, "Pivot");
	set_input_name(k_clamp_black_enable_input, "Enable Black Clamp");
	set_input_name(k_clamp_black_input, "Black Clamp");
	set_input_name(k_clamp_white_enable_input, "Enable White Clamp");
	set_input_name(k_clamp_white_input, "White Clamp");
}

void OCIOGradingTransformLinearNode::InputValueChangedEvent(
	const std::string &input, int element)
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

void OCIOGradingTransformLinearNode::InputConnectedEvent(const std::string &input,
														 int element, Node *output)
{
	super::InputConnectedEvent(input, element, output);

	if (input == k_clamp_black_input) {
		update_clamp_white_minimum();
	}
}

void OCIOGradingTransformLinearNode::InputDisconnectedEvent(const std::string &input,
															int element,
															Node *output)
{
	super::InputDisconnectedEvent(input, element, output);

	if (input == k_clamp_black_input) {
		update_clamp_white_minimum();
	}
}

void OCIOGradingTransformLinearNode::update_clamp_white_minimum()
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

void OCIOGradingTransformLinearNode::generate_processor()
{
	if (manager()) {
		OakNodeColorManager mgr =
			oaknode_colormanager_wrap_borrowed(manager());
		OakColorProcessor processor =
			oakrender_color_processor_create_grading_primary(mgr, OAKRENDER_GRADING_PRIMARY_LIN);
		mgr.release(mgr.ctx);
		if (processor.ctx) {
			set_processor(processor);
		}
	}
}

void OCIOGradingTransformLinearNode::value(const NodeValueRow &value,
										   const NodeGlobals &globals,
										   NodeValueTable *table) const
{
	if (TexturePtr tex = value.at(k_texture_input).to_texture()) {
		if (processor().ctx) {
			ColorTransformJob job(value);

			job.set_color_processor(
				oakrender_color_processor_get_native(processor()));
			job.set_input_texture(value.at(k_texture_input));

			// Vector4D components stand in for the former QVector4D indices:
			// x = master channel, y = red, z = green, w = blue.

			// Oddly, OCIO uses RGBMs when setting the GradingPrimary on the CPU, but uses vec3s on the GPU.
			// Even more oddly, the conversion from RGBM to vec3 does not appear to have a public API.
			// Therefore, this code has been duplicated from OCIO here:
			// https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/3abbe5b20521169580fcfe3692aca81859859953/src/OpenColorIO/ops/gradingprimary/GradingPrimary.cpp#L157
			Vector4D offset = value.at(k_offset_input).to_vec4();
			offset.set_y(offset.y() + offset.x());
			offset.set_z(offset.z() + offset.x());
			offset.set_w(offset.w() + offset.x());
			job.insert(k_offset_input,
					   NodeValue(NodeValue::k_vec3,
								 Vector3D(offset.y(), offset.z(), offset.w())));

			Vector4D exposure = value.at(k_exposure_input).to_vec4();
			exposure.set_y(std::pow(2.0f, exposure.x() + exposure.y()));
			exposure.set_z(std::pow(2.0f, exposure.x() + exposure.z()));
			exposure.set_w(std::pow(2.0f, exposure.x() + exposure.w()));
			job.insert(k_exposure_input,
					   NodeValue(NodeValue::k_vec3,
								 Vector3D(exposure.y(), exposure.z(),
										  exposure.w())));

			Vector4D contrast = value.at(k_contrast_input).to_vec4();
			contrast.set_y(contrast.y() * contrast.x());
			contrast.set_z(contrast.z() * contrast.x());
			contrast.set_w(contrast.w() * contrast.x());
			job.insert(k_contrast_input,
					   NodeValue(NodeValue::k_vec3,
								 Vector3D(contrast.y(), contrast.z(),
										  contrast.w())));

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

void OCIOGradingTransformLinearNode::config_changed()
{
	generate_processor();
}

void OCIOGradingTransformLinearNode::set_vec4_input_colors(const std::string &input)
{
	set_input_property(input, "color0", "#c0c0c0");
	set_input_property(input, "color1", "#ff0000");
	set_input_property(input, "color2", "#00ff00");
	set_input_property(input, "color3", "#0000ff");
}

}
