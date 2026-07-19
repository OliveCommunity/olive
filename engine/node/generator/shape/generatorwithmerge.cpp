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

#include "generatorwithmerge.h"

#include "node/math/merge/merge.h"

namespace olive
{

#define super Node

const QString GeneratorWithMerge::k_base_input = QStringLiteral("base_in");

GeneratorWithMerge::GeneratorWithMerge()
{
	add_input(k_base_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));
	set_effect_input(k_base_input);
	set_flag(k_video_effect);
}

void GeneratorWithMerge::retranslate()
{
	super::retranslate();

	set_input_name(k_base_input, tr("Base"));
}

ShaderCode GeneratorWithMerge::get_shader_code(const ShaderRequest &request) const
{
	if (request.id == QStringLiteral("mrg")) {
		return ShaderCode(
			FileFunctions::read_file_as_string(":/shaders/alphaover.frag"));
	}

	return ShaderCode();
}

void GeneratorWithMerge::push_mergable_job(const NodeValueRow &value,
										 TexturePtr job,
										 NodeValueTable *table) const
{
	if (TexturePtr base = value[k_base_input].to_texture()) {
		// Push as merge node
		ShaderJob merge;

		merge.set_shader_id(QStringLiteral("mrg"));
		merge.insert(MergeNode::k_base_in, value[k_base_input]);
		merge.insert(MergeNode::k_blend_in,
					 NodeValue(NodeValue::k_texture, job, this));

		table->push(NodeValue::k_texture, base->to_job(merge), this);
	} else {
		// Just push generate job
		table->push(NodeValue::k_texture, job, this);
	}
}

}
