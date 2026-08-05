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

#ifndef OAK_BLURFILTERNODE_H
#define OAK_BLURFILTERNODE_H

#include "gizmo/point.h"
#include "node.h"

namespace olive
{

class BlurFilterNode : public Node {
public:
	BlurFilterNode();

	enum Method { k_box, k_gaussian, k_directional, k_radial };

	NODE_DEFAULT_FUNCTIONS(BlurFilterNode)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual void retranslate() override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;
	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	Method get_method() const
	{
		return static_cast<Method>(get_standard_value(k_method_input).to_int());
	}

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	static const std::string k_texture_input;
	static const std::string k_method_input;
	static const std::string k_radius_input;
	static const std::string k_horiz_input;
	static const std::string k_vert_input;
	static const std::string k_repeat_edge_pixels_input;

	static const std::string k_directional_degrees_input;

	static const std::string k_radial_center_input;

protected:
	virtual void gizmo_drag_move(double x, double y, int modifiers) override;

	virtual void InputValueChangedEvent(const std::string &input,
										int element) override;

private:
	void update_inputs(Method method);

	PointGizmo *radial_center_gizmo_;
};

}

#endif // OAK_BLURFILTERNODE_H
