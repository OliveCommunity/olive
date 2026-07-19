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

#include "noise.h"

#include "node/sliderdisplaytype.h"

namespace olive
{

const QString NoiseGeneratorNode::k_base_in = QStringLiteral("base_in");
const QString NoiseGeneratorNode::k_color_input = QStringLiteral("color_in");
const QString NoiseGeneratorNode::k_strength_input =
	QStringLiteral("strength_in");

#define super Node

NoiseGeneratorNode::NoiseGeneratorNode()
{
	add_input(k_base_in, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_strength_input, NodeValue::k_float, 0.2);
	set_input_property(k_strength_input, QStringLiteral("view"),
					 slider::k_percentage);
	set_input_property(k_strength_input, QStringLiteral("min"), 0);

	add_input(k_color_input, NodeValue::k_boolean, false);

	set_effect_input(k_base_in);
	set_flag(k_video_effect);
}

QString NoiseGeneratorNode::name() const
{
	return tr("Noise");
}

QString NoiseGeneratorNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.noise");
}

QVector<Node::CategoryID> NoiseGeneratorNode::category() const
{
	return { k_category_generator };
}

QString NoiseGeneratorNode::description() const
{
	return tr("Generates noise patterns");
}

void NoiseGeneratorNode::retranslate()
{
	super::retranslate();

	set_input_name(k_base_in, tr("Base"));
	set_input_name(k_strength_input, tr("Strength"));
	set_input_name(k_color_input, tr("Color"));
}

ShaderCode NoiseGeneratorNode::get_shader_code(const ShaderRequest &request) const
{
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/noise.frag"));
}

void NoiseGeneratorNode::value(const NodeValueRow &value,
							   const NodeGlobals &globals,
							   NodeValueTable *table) const
{
	ShaderJob job(value);

	job.insert(value);
	job.insert(QStringLiteral("time_in"),
			   NodeValue(NodeValue::k_float, globals.time().in().to_double(),
						 this));

	TexturePtr base = value[k_base_in].to_texture();

	table->push(NodeValue::k_texture,
				Texture::job(base ? base->params() : globals.vparams(), job),
				this);
}
}
