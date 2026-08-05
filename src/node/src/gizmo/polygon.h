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

#ifndef OAK_POLYGONGIZMO_H
#define OAK_POLYGONGIZMO_H

#include <vector>

#include "draggable.h"
#include "mathtypes.h"

namespace olive
{

class PolygonGizmo : public DraggableGizmo {
public:
	explicit PolygonGizmo(Node *parent = nullptr);

	// De-Qt: QPolygonF is stored as a plain vector of points. Point-in-polygon
	// testing (formerly QPolygonF::containsPoint) belongs to the app layer.
	const std::vector<PointF> &get_polygon() const
	{
		return polygon_;
	}
	void set_polygon(const std::vector<PointF> &polygon)
	{
		polygon_ = polygon;
	}

private:
	std::vector<PointF> polygon_;
};

}

#endif // OAK_POLYGONGIZMO_H
