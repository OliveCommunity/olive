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

#include <iostream>

#include "common/ocioutils.h"
#include "node/project.h"
#include "render/colorprocessor.h"
#include "node/sliderdisplaytype.h"

namespace olive
{

const QString OCIOGradingTransformLinearNode::k_contrast_input =
	QStringLiteral("ocio_grading_primary_contrast");
const QString OCIOGradingTransformLinearNode::k_offset_input =
	QStringLiteral("ocio_grading_primary_offset");
const QString OCIOGradingTransformLinearNode::k_exposure_input =
	QStringLiteral("ocio_grading_primary_exposure");
const QString OCIOGradingTransformLinearNode::k_saturation_input =
	QStringLiteral("ocio_grading_primary_saturation");
const QString OCIOGradingTransformLinearNode::k_pivot_input =
	QStringLiteral("ocio_grading_primary_pivot");
const QString OCIOGradingTransformLinearNode::k_clamp_black_enable_input =
	QStringLiteral("clamp_black_enable_in");
const QString OCIOGradingTransformLinearNode::k_clamp_black_input =
	QStringLiteral("ocio_grading_primary_clampBlack");
const QString OCIOGradingTransformLinearNode::k_clamp_white_enable_input =
	QStringLiteral("clamp_white_enable_in");
const QString OCIOGradingTransformLinearNode::k_clamp_white_input =
	QStringLiteral("ocio_grading_primary_clampWhite");

#define super OCIOBaseNode

OCIOGradingTransformLinearNode::OCIOGradingTransformLinearNode()
{
	add_input(k_contrast_input, NodeValue::k_vec4, QVector4D{ 1.0, 1.0, 1.0, 1.0 });
	// Minimum based on ocio::GradingPrimary::validate
	set_input_property(k_contrast_input, QStringLiteral("min"),
					 QVector4D{ 0.01f, 0.01f, 0.01f, 0.01f });
	set_input_property(k_contrast_input, QStringLiteral("base"), 0.01);
	set_vec4_input_colors(k_contrast_input);

	add_input(k_offset_input, NodeValue::k_vec4, QVector4D{ 0.0, 0.0, 0.0, 0.0 });
	set_input_property(k_offset_input, QStringLiteral("base"), 0.01);
	set_vec4_input_colors(k_offset_input);

	add_input(k_exposure_input, NodeValue::k_vec4, QVector4D{ 0.0, 0.0, 0.0, 0.0 });
	set_input_property(k_exposure_input, QStringLiteral("base"), 0.01);
	set_vec4_input_colors(k_exposure_input);

	add_input(k_saturation_input, NodeValue::k_float, 1.0);
	set_input_property(k_saturation_input, QStringLiteral("view"),
					 slider::k_percentage);
	set_input_property(k_saturation_input, QStringLiteral("min"), 0.0);

	add_input(k_pivot_input, NodeValue::k_float,
			 0.18); // Default listed in ocio::GradingPrimary
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

QString OCIOGradingTransformLinearNode::name() const
{
	return tr("OCIO Color Grading (Linear)");
}

QString OCIOGradingTransformLinearNode::id() const
{
	return QStringLiteral(
		"org.olivevideoeditor.Olive.ociogradingtransformlinear");
}

QVector<Node::CategoryID> OCIOGradingTransformLinearNode::category() const
{
	return { k_category_color };
}

QString OCIOGradingTransformLinearNode::description() const
{
	return tr("Simple linear color grading using OpenColorIO.");
}

void OCIOGradingTransformLinearNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_contrast_input, tr("Contrast"));
	set_input_name(k_offset_input, tr("Offset"));
	set_input_name(k_exposure_input, tr("Exposure"));
	set_input_property(k_exposure_input, QStringLiteral("tooltip"),
					 tr("Exposure increments in stops."));
	set_input_name(k_saturation_input, tr("Saturation"));
	set_input_name(k_pivot_input, tr("Pivot"));
	set_input_name(k_clamp_black_enable_input, tr("Enable Black Clamp"));
	set_input_name(k_clamp_black_input, tr("Black Clamp"));
	set_input_name(k_clamp_white_enable_input, tr("Enable White Clamp"));
	set_input_name(k_clamp_white_input, tr("White Clamp"));
}

void OCIOGradingTransformLinearNode::InputValueChangedEvent(
	const QString &input, int element)
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

void OCIOGradingTransformLinearNode::InputConnectedEvent(const QString &input,
														 int element, Node *output)
{
	super::InputConnectedEvent(input, element, output);

	if (input == k_clamp_black_input) {
		update_clamp_white_minimum();
	}
}

void OCIOGradingTransformLinearNode::InputDisconnectedEvent(const QString &input,
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

	set_input_property(k_clamp_white_input, QStringLiteral("min"),
					 get_standard_value(k_clamp_black_input).toDouble() + 0.000001);
}

void OCIOGradingTransformLinearNode::generate_processor()
{
	if (manager()) {
		ocio::GradingPrimaryTransformRcPtr gp =
			ocio::GradingPrimaryTransform::Create(ocio::GRADING_LIN);
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

void OCIOGradingTransformLinearNode::value(const NodeValueRow &value,
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

			// Oddly, OCIO uses RGBMs when setting the GradingPrimary on the CPU, but uses vec3s on the GPU.
			// Even more oddly, the conversion from RGBM to vec3 does not appear to have a public API.
			// Therefore, this code has been duplicated from OCIO here:
			// https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/3abbe5b20521169580fcfe3692aca81859859953/src/OpenColorIO/ops/gradingprimary/GradingPrimary.cpp#L157
			QVector4D offset = value[k_offset_input].to_vec4();
			offset[red_channel] += offset[master_channel];
			offset[green_channel] += offset[master_channel];
			offset[blue_channel] += offset[master_channel];
			job.insert(k_offset_input,
					   NodeValue(NodeValue::k_vec3,
								 QVector3D(offset[red_channel],
										   offset[green_channel],
										   offset[blue_channel])));

			QVector4D exposure = value[k_exposure_input].to_vec4();
			exposure[red_channel] = std::pow(2.0f, exposure[master_channel] +
													   exposure[red_channel]);
			exposure[green_channel] = std::pow(
				2.0f, exposure[master_channel] + exposure[green_channel]);
			exposure[blue_channel] = std::pow(2.0f, exposure[master_channel] +
														exposure[blue_channel]);
			job.insert(k_exposure_input,
					   NodeValue(NodeValue::k_vec3,
								 QVector3D(exposure[red_channel],
										   exposure[green_channel],
										   exposure[blue_channel])));

			QVector4D contrast = value[k_contrast_input].to_vec4();
			contrast[red_channel] *= contrast[master_channel];
			contrast[green_channel] *= contrast[master_channel];
			contrast[blue_channel] *= contrast[master_channel];
			job.insert(k_contrast_input,
					   NodeValue(NodeValue::k_vec3,
								 QVector3D(contrast[red_channel],
										   contrast[green_channel],
										   contrast[blue_channel])));

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

void OCIOGradingTransformLinearNode::config_changed()
{
	generate_processor();
}

void OCIOGradingTransformLinearNode::set_vec4_input_colors(const QString &input)
{
	set_input_property(input, QStringLiteral("color0"),
					 QColor(192, 192, 192).name());
	set_input_property(input, QStringLiteral("color1"), QColor(255, 0, 0).name());
	set_input_property(input, QStringLiteral("color2"), QColor(0, 255, 0).name());
	set_input_property(input, QStringLiteral("color3"), QColor(0, 0, 255).name());
}

}
