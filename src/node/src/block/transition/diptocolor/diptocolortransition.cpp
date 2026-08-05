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

#include "diptocolortransition.h"

#include "filefunctions.h"

namespace olive
{

const std::string DipToColorTransition::k_color_input = "color_in";

#define super TransitionBlock

DipToColorTransition::DipToColorTransition()
{
	add_input(k_color_input, NodeValue::k_color,
			 Variant::from_value(Color(0, 0, 0)));
}

std::string DipToColorTransition::name() const
{
	return "Dip To Color";
}

std::string DipToColorTransition::id() const
{
	return "org.olivevideoeditor.Olive.diptocolor";
}

std::vector<Node::CategoryID> DipToColorTransition::category() const
{
	return { k_category_transition };
}

std::string DipToColorTransition::description() const
{
	return "Transition between clips by dipping to a color.";
}

ShaderCode
DipToColorTransition::get_shader_code(const ShaderRequest &request) const
{
	(void) request;

	return ShaderCode(
		FileFunctions::read_file_as_string(":/shaders/diptoblack.frag"),
		std::string());
}

void DipToColorTransition::retranslate()
{
	super::retranslate();

	set_input_name(k_color_input, "Color");
}

void DipToColorTransition::ShaderJobEvent(const NodeValueRow &value,
										  ShaderJob *job) const
{
	job->insert(k_color_input, value);
}

}
