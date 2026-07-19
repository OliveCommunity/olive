/***
  Olive - Non-Linear Video Editor
  Copyright (C) 2019 Olive Team
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

#include "colordifferencekey.h"

namespace olive
{

const QString ColorDifferenceKeyNode::k_texture_input = QStringLiteral("tex_in");
const QString ColorDifferenceKeyNode::k_garbage_matte_input =
	QStringLiteral("garbage_in");
const QString ColorDifferenceKeyNode::k_core_matte_input =
	QStringLiteral("core_in");
const QString ColorDifferenceKeyNode::k_color_input = QStringLiteral("color_in");
const QString ColorDifferenceKeyNode::k_shadows_input =
	QStringLiteral("shadows_in");
const QString ColorDifferenceKeyNode::k_highlights_input =
	QStringLiteral("highlights_in");
const QString ColorDifferenceKeyNode::k_mask_only_input =
	QStringLiteral("mask_only_in");

#define super Node

ColorDifferenceKeyNode::ColorDifferenceKeyNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_garbage_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_core_matte_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_color_input, NodeValue::k_combo, 0);

	add_input(k_highlights_input, NodeValue::k_float, 1.0f);
	set_input_property(k_highlights_input, QStringLiteral("min"), 0.0);
	set_input_property(k_highlights_input, QStringLiteral("base"), 0.01);

	add_input(k_shadows_input, NodeValue::k_float, 1.0f);
	set_input_property(k_shadows_input, QStringLiteral("min"), 0.0);
	set_input_property(k_shadows_input, QStringLiteral("base"), 0.01);

	add_input(k_mask_only_input, NodeValue::k_boolean, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

QString ColorDifferenceKeyNode::name() const
{
	return tr("Color Difference Key");
}

QString ColorDifferenceKeyNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.colordifferencekey");
}

QVector<Node::CategoryID> ColorDifferenceKeyNode::category() const
{
	return { k_category_keying };
}

QString ColorDifferenceKeyNode::description() const
{
	return tr(
		"A simple color key based on the distance of one color from other colors.");
}

void ColorDifferenceKeyNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_garbage_matte_input, tr("Garbage Matte"));
	set_input_name(k_core_matte_input, tr("Core Matte"));
	set_input_name(k_color_input, tr("Key Color"));
	set_combo_box_strings(k_color_input, { tr("Green"), tr("Blue") });
	set_input_name(k_shadows_input, tr("Shadows"));
	set_input_name(k_highlights_input, tr("Highlights"));
	set_input_name(k_mask_only_input, tr("Show Mask Only"));
}

ShaderCode
ColorDifferenceKeyNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/colordifferencekey.frag"));
}

void ColorDifferenceKeyNode::value(const NodeValueRow &value,
								   const NodeGlobals &globals,
								   NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr tex = value[k_texture_input].to_texture()) {
		ShaderJob job;
		job.insert(value);
		table->push(NodeValue::k_texture, tex->to_job(job), this);
	}
}

} // namespace olive
