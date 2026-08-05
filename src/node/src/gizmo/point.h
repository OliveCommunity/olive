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

#ifndef OAK_POINTGIZMO_H
#define OAK_POINTGIZMO_H

#include "draggable.h"
#include "mathtypes.h"

namespace olive
{

class PointGizmo : public DraggableGizmo {
public:
	enum Shape { k_square, k_circle, k_anchor_point };

	explicit PointGizmo(const Shape &shape, bool smaller,
						Node *parent = nullptr);
	explicit PointGizmo(const Shape &shape, Node *parent = nullptr);
	explicit PointGizmo(Node *parent = nullptr);

	const Shape &get_shape() const
	{
		return shape_;
	}
	void set_shape(const Shape &s)
	{
		shape_ = s;
	}

	const PointF &get_point() const
	{
		return point_;
	}
	void set_point(const PointF &pt)
	{
		point_ = pt;
	}

	bool get_smaller() const
	{
		return smaller_;
	}
	void set_smaller(bool e)
	{
		smaller_ = e;
	}

private:
	Shape shape_;

	PointF point_;

	bool smaller_;
};

}

#endif // OAK_POINTGIZMO_H
