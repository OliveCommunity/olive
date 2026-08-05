/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2026 mikesolar

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

#ifndef OAK_THREEWAYCOLORNODE_H
#define OAK_THREEWAYCOLORNODE_H

#include "node.h"

namespace olive
{

class ThreeWayColorNode : public Node {
public:
	ThreeWayColorNode();

	NODE_DEFAULT_FUNCTIONS(ThreeWayColorNode)

	virtual std::string name() const override
	{
		return "Three-Way Color";
	}

	virtual std::string id() const override
	{
		return "org.olivevideoeditor.Olive.threewaycolor";
	}

	virtual std::vector<CategoryID> category() const override
	{
		return { k_category_color };
	}

	virtual std::string description() const override
	{
		return "Adjusts shadows, midtones, and highlights separately.";
	}

	virtual void retranslate() override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	static const std::string k_texture_input;
	static const std::string k_shadows_color_input;
	static const std::string k_midtones_color_input;
	static const std::string k_highlights_color_input;
	static const std::string k_shadows_amount_input;
	static const std::string k_midtones_amount_input;
	static const std::string k_highlights_amount_input;
	static const std::string k_luma_coefficients_input;
};

}

#endif // OAK_THREEWAYCOLORNODE_H
