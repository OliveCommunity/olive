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

#ifndef OAK_TILEDISTORTNODE_H
#define OAK_TILEDISTORTNODE_H

#include "gizmo/point.h"
#include "node.h"

namespace olive
{

class TileDistortNode : public Node {
public:
	TileDistortNode();

	NODE_DEFAULT_FUNCTIONS(TileDistortNode)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	static const std::string k_texture_input;
	static const std::string k_scale_input;
	static const std::string k_position_input;
	static const std::string k_anchor_input;
	static const std::string k_mirror_x_input;
	static const std::string k_mirror_y_input;

protected:
	virtual void gizmo_drag_move(double x, double y, int modifiers) override;

private:
	enum Anchor {
		k_top_left,
		k_top_center,
		k_top_right,
		k_middle_left,
		k_middle_center,
		k_middle_right,
		k_bottom_left,
		k_bottom_center,
		k_bottom_right
	};

	PointGizmo *gizmo_;
};

}

#endif // OAK_TILEDISTORTNODE_H
