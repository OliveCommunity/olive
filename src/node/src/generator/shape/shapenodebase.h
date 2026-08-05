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

#ifndef OAK_SHAPENODEBASE_H
#define OAK_SHAPENODEBASE_H

#include "generatorwithmerge.h"
#include "geometry.h"
#include "gizmo/point.h"
#include "gizmo/polygon.h"
#include "gizmo/text.h"
#include "inputdragger.h"
#include "node.h"

namespace olive
{

class ShapeNodeBase : public GeneratorWithMerge {
public:
	ShapeNodeBase(bool create_color_input = true);

	virtual void retranslate() override;

	virtual void update_gizmo_positions(const NodeValueRow &row,
									  const NodeGlobals &globals) override;

	void set_rect(RectF rect, const VideoParams &sequence_res,
				 MultiUndoCommand *command);

	static const std::string k_position_input;
	static const std::string k_size_input;
	static const std::string k_color_input;

protected:
	PolygonGizmo *poly_gizmo() const
	{
		return poly_gizmo_;
	}

	virtual void gizmo_drag_move(double x, double y, int modifiers) override;

private:
	Vector2D generate_gizmo_anchor(const Vector2D &pos, const Vector2D &size,
								 NodeGizmo *gizmo,
								 Vector2D *pt = nullptr) const;

	bool is_gizmo_top(NodeGizmo *g) const;
	bool is_gizmo_bottom(NodeGizmo *g) const;
	bool is_gizmo_left(NodeGizmo *g) const;
	bool is_gizmo_right(NodeGizmo *g) const;
	bool is_gizmo_horizontal_center(NodeGizmo *g) const;
	bool is_gizmo_vertical_center(NodeGizmo *g) const;
	bool is_gizmo_corner(NodeGizmo *g) const;

	// Gizmo variables
	static const int k_gizmo_whole_rect = k_gizmo_scale_count;
	PointGizmo *point_gizmo_[k_gizmo_scale_count];
	PolygonGizmo *poly_gizmo_;
};

}

#endif // OAK_SHAPENODEBASE_H
