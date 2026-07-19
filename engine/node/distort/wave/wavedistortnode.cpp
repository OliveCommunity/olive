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

#include "wavedistortnode.h"

namespace olive
{

const QString WaveDistortNode::k_texture_input = QStringLiteral("tex_in");
const QString WaveDistortNode::k_frequency_input = QStringLiteral("frequency_in");
const QString WaveDistortNode::k_intensity_input = QStringLiteral("intensity_in");
const QString WaveDistortNode::k_evolution_input = QStringLiteral("evolution_in");
const QString WaveDistortNode::k_vertical_input = QStringLiteral("vertical_in");

#define super Node

WaveDistortNode::WaveDistortNode()
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	add_input(k_frequency_input, NodeValue::k_float, 10);
	add_input(k_intensity_input, NodeValue::k_float, 10);
	add_input(k_evolution_input, NodeValue::k_float, 0);

	add_input(k_vertical_input, NodeValue::k_combo, false);

	set_flag(k_video_effect);
	set_effect_input(k_texture_input);
}

QString WaveDistortNode::name() const
{
	return tr("Wave");
}

QString WaveDistortNode::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.wave");
}

QVector<Node::CategoryID> WaveDistortNode::category() const
{
	return { k_category_distort };
}

QString WaveDistortNode::description() const
{
	return tr("Distorts an image along a sine wave.");
}

void WaveDistortNode::retranslate()
{
	super::retranslate();

	set_input_name(k_texture_input, tr("Input"));
	set_input_name(k_frequency_input, tr("Frequency"));
	set_input_name(k_intensity_input, tr("Intensity"));
	set_input_name(k_evolution_input, tr("Evolution"));
	set_input_name(k_vertical_input, tr("Direction"));
	set_combo_box_strings(k_vertical_input, { tr("Horizontal"), tr("Vertical") });
}

ShaderCode WaveDistortNode::get_shader_code(const ShaderRequest &request) const
{
	Q_UNUSED(request)
	return ShaderCode(FileFunctions::read_file_as_string(":/shaders/wave.frag"));
}

void WaveDistortNode::value(const NodeValueRow &value,
							const NodeGlobals &globals,
							NodeValueTable *table) const
{
	// If there's no texture, no need to run an operation
	if (TexturePtr texture = value[k_texture_input].to_texture()) {
		// Only run shader if at least one of flip or flop are selected
		if (!qIsNull(value[k_intensity_input].to_double())) {
			table->push(NodeValue::k_texture,
						Texture::job(texture->params(), ShaderJob(value)),
						this);
		} else {
			// If we're not flipping or flopping just push the texture
			table->push(value[k_texture_input]);
		}
	}
}

}
