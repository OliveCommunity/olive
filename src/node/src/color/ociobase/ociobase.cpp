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

#include "ociobase.h"

#include "color/colormanager/colormanager.h"
#include "project.h"

namespace olive
{

const std::string OCIOBaseNode::k_texture_input = "tex_in";

OCIOBaseNode::OCIOBaseNode()
	: manager_(nullptr)
{
	add_input(k_texture_input, NodeValue::k_texture,
			 InputFlags(k_input_flag_not_keyframable));

	set_effect_input(k_texture_input);

	set_flag(k_video_effect);
}

OCIOBaseNode::~OCIOBaseNode()
{
	oakrender_color_processor_free(&processor_);
}

void OCIOBaseNode::AddedToGraphEvent(Project *p)
{
	manager_ = p->color_manager();
	config_changed();
}

void OCIOBaseNode::RemovedFromGraphEvent(Project *p)
{
	if (manager_) {
		manager_ = nullptr;
	}
}

void OCIOBaseNode::value(const NodeValueRow &value, const NodeGlobals &globals,
						 NodeValueTable *table) const
{
	auto tex_met = value.at(k_texture_input);
	TexturePtr t = tex_met.to_texture();
	if (t) {
		if (processor_.ctx) {
			ColorTransformJob job;

			job.set_color_processor(
				oakrender_color_processor_get_native(processor_));
			job.set_input_texture(tex_met);

			table->push(NodeValue::k_texture, t->to_job(job), this);
		} else {
			// Processor isn't ready yet (e.g. still being generated
			// asynchronously), pass the input through unchanged.
			table->push(NodeValue::k_texture, Variant::from_value(t), this);
		}
	}
}

}
