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

#ifndef OAK_MOSAICFILTERNODE_H
#define OAK_MOSAICFILTERNODE_H

#include "node.h"

namespace olive
{

class MosaicFilterNode : public Node {
public:
	MosaicFilterNode();

	NODE_DEFAULT_FUNCTIONS(MosaicFilterNode)

	virtual std::string name() const override
	{
		return "Mosaic";
	}

	virtual std::string id() const override
	{
		return "org.olivevideoeditor.Olive.mosaicfilter";
	}

	virtual std::vector<CategoryID> category() const override
	{
		return { k_category_filter };
	}

	virtual std::string description() const override
	{
		return "Apply a pixelated mosaic filter to video.";
	}

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;
	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	static const std::string k_texture_input;
	static const std::string k_horiz_input;
	static const std::string k_vert_input;
};

}

#endif // OAK_MOSAICFILTERNODE_H
