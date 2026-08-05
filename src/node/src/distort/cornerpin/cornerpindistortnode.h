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

#ifndef OAK_CORNERPINDISTORTNODE_H
#define OAK_CORNERPINDISTORTNODE_H

#include "gizmo/point.h"
#include "gizmo/polygon.h"
#include "inputdragger.h"
#include "node.h"

namespace olive
{
class CornerPinDistortNode : public Node {
public:
	CornerPinDistortNode();

	NODE_DEFAULT_FUNCTIONS(CornerPinDistortNode)

	virtual std::string name() const override
	{
		return "Corner Pin";
	}

	virtual std::string id() const override
	{
		return "org.olivevideoeditor.Olive.cornerpin";
	}

	virtual std::vector<CategoryID> category() const override
	{
		return { k_category_distort };
	}

	virtual std::string description() const override
	{
		return "Distort the image by dragging the corners.";
	}

	virtual void retranslate() override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual ShaderCode
	get_shader_code(const ShaderRequest &request) const override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	/**
   * @brief Convenience function - converts the 2D slider values from being
   * an offset to the actual pixel value.
   */
	PointF value_to_pixel(int value, const NodeValueRow &row,
						 const Vector2D &resolution) const;

	static const std::string k_texture_input;
	static const std::string k_perspective_input;
	static const std::string k_top_left_input;
	static const std::string k_top_right_input;
	static const std::string k_bottom_right_input;
	static const std::string k_bottom_left_input;

protected:
	virtual void gizmo_drag_move(double x, double y, int modifiers) override;

private:
	// Gizmo variables
	static const int k_gizmo_corner_count = 4;
	PointGizmo *gizmo_resize_handle_[k_gizmo_corner_count];
	PolygonGizmo *gizmo_whole_rect_;
};

}

#endif // OAK_CORNERPINDISTORTNODE_H
