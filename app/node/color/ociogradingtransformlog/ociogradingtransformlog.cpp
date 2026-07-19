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

#include <iostream>

#include "common/ocioutils.h"
#include "node/project.h"
#include "render/colorprocessor.h"
#include "widget/slider/floatslider.h"

namespace olive
{

// These ids double as the OCIO GPU uniform names for the dynamic
// GradingPrimaryTransform; do not rename them. OCIO's log style maps the
// classic wheels as: brightness = lift, contrast = gain, gamma = gamma.
const QString OCIOGradingTransformLogNode::k_lift_input =
	QStringLiteral("ocio_grading_primary_brightness");
const QString OCIOGradingTransformLogNode::k_gain_input =
	QStringLiteral("ocio_grading_primary_contrast");
const QString OCIOGradingTransformLogNode::k_gamma_input =
	QStringLiteral("ocio_grading_primary_gamma");
const QString OCIOGradingTransformLogNode::k_saturation_input =
	QStringLiteral("ocio_grading_primary_saturation");
const QString OCIOGradingTransformLogNode::k_pivot_input =
	QStringLiteral("ocio_grading_primary_pivot");
const QString OCIOGradingTransformLogNode::k_clamp_black_enable_input =
	QStringLiteral("clamp_black_enable_in");
const QString OCIOGradingTransformLogNode::k_clamp_black_input =
	QStringLiteral("ocio_grading_primary_clampBlack");
const QString OCIOGradingTransformLogNode::k_clamp_white_enable_input =
	QStringLiteral("clamp_white_enable_in");
const QString OCIOGradingTransformLogNode::k_clamp_white_input =
	QStringLiteral("ocio_grading_primary_clampWhite");

#define super OCIOBaseNode

OCIOGradingTransformLogNode::OCIOGradingTransformLogNode()
{
	add_input(k_lift_input, NodeValue::k_vec4, QVector4D{ 0.0, 0.0, 0.0, 0.0 });
	set_input_property(k_lift_input, QStringLiteral("base"), 0.01);
	set_vec4_input_colors(k_lift_input);

	add_input(k_gain_input, NodeValue::k_vec4, QVector4D{ 1.0, 1.0, 1.0, 1.0 });
	set_input_property(k_gain_input, QStringLiteral("base"), 0.01);
	set_vec4_input_colors(k_gain_input);

	add_input(k_gamma_input, NodeValue::k_vec4, QVector4D{ 1.0, 1.0, 1.0, 1.0 });
	set_input_property(k_gamma_input, QStringLiteral("base"), 0.01);
	set_vec4_input_colors(k_gamma_input);

	add_input(k_saturation_input, NodeValue::k_float, 1.0);
	set_input_property(k_saturation_input, QStringLiteral("view"),
					 FloatSlider::k_percentage);
	set_input_property(k_saturation_input, QStringLiteral("min"), 0.0);

	add_input(k_pivot_input, NodeValue::k_float,
			 -0.2); // Default for GRADING_LOG listed in ocio::GradingPrimary
	set_input_property(k_pivot_input, QStringLiteral("base"), 0.01);

	add_input(k_clamp_black_enable_input, NodeValue::k_boolean, false);

	add_input(k_clamp_black_input, NodeValue::k_float, 0.0);
	set_input_property(k_clamp_black_input, QStringLiteral("enabled"),
					 get_standard_value(k_clamp_black_enable_input).toBool());
	set_input_property(k_clamp_black_input, QStringLiteral("base"), 0.01);

	add_input(k_clamp_white_enable_input, NodeValue::k_boolean, false);

	add_input(k_clamp_white_input, NodeValue::k_float, 1.0);
	set_input_property(k_clamp_white_input, QStringLiteral("enabled"),
					 get_standard_value(k_clamp_white_enable_input).toBool());
	set_input_property(k_clamp_white_input, QStringLiteral("base"), 0.01);

	// Constrain the white clamp minimum to just above the (static) black clamp
	// as per ocio::GradingPrimary::validate. When the black clamp is keyframed
	// or connected, Value() enforces the invariant per frame instead.
	update_clamp_white_minimum();
}

QString OCIOGradingTransformLogNode::name() const
{
	return tr("OCIO Color Grading (Log)");
}

QString OCIOGradingTransformLogNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.ociogradingtransformlog");
}

QVector<Node::CategoryID> OCIOGradingTransformLogNode::category() const
{
	return { k_category_color };
}

QString OCIOGradingTransformLogNode::description() const
{
	return tr("Lift/gamma/gain color grading using OpenColorIO.");
}

void OCIOGradingTransformLogNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_lift_input, tr("Lift"));
	set_input_name(k_gain_input, tr("Gain"));
	set_input_name(k_gamma_input, tr("Gamma"));
	set_input_name(k_saturation_input, tr("Saturation"));
	set_input_name(k_pivot_input, tr("Pivot"));
	set_input_name(k_clamp_black_enable_input, tr("Enable Black Clamp"));
	set_input_name(k_clamp_black_input, tr("Black Clamp"));
	set_input_name(k_clamp_white_enable_input, tr("Enable White Clamp"));
	set_input_name(k_clamp_white_input, tr("White Clamp"));
}

void OCIOGradingTransformLogNode::InputValueChangedEvent(const QString &input,
														 int element)
{
	Q_UNUSED(element);

	if (input == k_clamp_white_enable_input) {
		set_input_property(k_clamp_white_input, QStringLiteral("enabled"),
						 get_standard_value(k_clamp_white_enable_input).toBool());
	} else if (input == k_clamp_black_enable_input) {
		set_input_property(k_clamp_black_input, QStringLiteral("enabled"),
						 get_standard_value(k_clamp_black_enable_input).toBool());
	} else if (input == k_clamp_black_input) {
		// Ensure the white clamp is always greater than the black clamp as per
		// ocio::GradingPrimary::validate
		update_clamp_white_minimum();
	}

	generate_processor();
}

void OCIOGradingTransformLogNode::InputConnectedEvent(const QString &input,
													  int element, Node *output)
{
	super::InputConnectedEvent(input, element, output);

	if (input == k_clamp_black_input) {
		update_clamp_white_minimum();
	}
}

void OCIOGradingTransformLogNode::InputDisconnectedEvent(const QString &input,
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

	set_input_property(k_clamp_white_input, QStringLiteral("min"),
					 get_standard_value(k_clamp_black_input).toDouble() + 0.000001);
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
	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		if (processor()) {
			ColorTransformJob job(value);

			job.set_color_processor(processor());
			job.set_input_texture(value[k_texture_input]);

			const int master_channel = 0;
			const int red_channel = 1;
			const int green_channel = 2;
			const int blue_channel = 3;

			// OCIO expects vec3s on the GPU but RGBMs (master + RGB) on the
			// CPU; the per-style master combination below mirrors
			// ocio::GradingPrimary. Lift is additive, gain/gamma multiply.
			QVector4D lift = value[k_lift_input].to_vec4();
			lift[red_channel] += lift[master_channel];
			lift[green_channel] += lift[master_channel];
			lift[blue_channel] += lift[master_channel];
			job.insert(k_lift_input,
					   NodeValue(NodeValue::k_vec3,
								 QVector3D(lift[red_channel], lift[green_channel],
										   lift[blue_channel])));

			QVector4D gain = value[k_gain_input].to_vec4();
			gain[red_channel] *= gain[master_channel];
			gain[green_channel] *= gain[master_channel];
			gain[blue_channel] *= gain[master_channel];
			job.insert(k_gain_input,
					   NodeValue(NodeValue::k_vec3,
								 QVector3D(gain[red_channel], gain[green_channel],
										   gain[blue_channel])));

			QVector4D gamma = value[k_gamma_input].to_vec4();
			gamma[red_channel] *= gamma[master_channel];
			gamma[green_channel] *= gamma[master_channel];
			gamma[blue_channel] *= gamma[master_channel];
			job.insert(k_gamma_input,
					   NodeValue(NodeValue::k_vec3,
								 QVector3D(gamma[red_channel],
										   gamma[green_channel],
										   gamma[blue_channel])));

			if (!value[k_clamp_black_enable_input].to_bool()) {
				job.insert(k_clamp_black_input,
						   NodeValue(NodeValue::k_float,
									 ocio::GradingPrimary::NoClampBlack()));
			}

			if (!value[k_clamp_white_enable_input].to_bool()) {
				job.insert(k_clamp_white_input,
						   NodeValue(NodeValue::k_float,
									 ocio::GradingPrimary::NoClampWhite()));
			}

			if (value[k_clamp_black_enable_input].to_bool() &&
				value[k_clamp_white_enable_input].to_bool()) {
				// ocio::GradingPrimary::validate requires the white clamp to be
				// greater than the black clamp. Keyframed or connected values
				// can violate this at arbitrary times, so enforce the invariant
				// per frame here.
				const double clamp_black = value[k_clamp_black_input].to_double();
				const double clamp_white = value[k_clamp_white_input].to_double();
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

void OCIOGradingTransformLogNode::set_vec4_input_colors(const QString &input)
{
	set_input_property(input, QStringLiteral("color0"),
					 QColor(192, 192, 192).name());
	set_input_property(input, QStringLiteral("color1"), QColor(255, 0, 0).name());
	set_input_property(input, QStringLiteral("color2"), QColor(0, 255, 0).name());
	set_input_property(input, QStringLiteral("color3"), QColor(0, 0, 255).name());
}

}
