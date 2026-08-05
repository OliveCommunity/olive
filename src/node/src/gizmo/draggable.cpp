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

#include "draggable.h"

#include "node.h"

namespace olive
{

DraggableGizmo::DraggableGizmo(Node *parent)
	: NodeGizmo{ parent }
	, drag_value_behavior_(k_absolute)
{
}

void DraggableGizmo::drag_start(const NodeValueRow &row, double abs_x,
							   double abs_y, const Rational &time)
{
	for (int i = 0; i < int(draggers_.size()); i++) {
		draggers_[i].start(inputs_[i], time);
	}

	if (Node *p = parent_node()) {
		p->gizmo_drag_start(row, abs_x, abs_y, time);
	}
}

void DraggableGizmo::drag_move(double x, double y, int modifiers)
{
	if (Node *p = parent_node()) {
		p->gizmo_drag_move(x, y, modifiers);
	}
}

void DraggableGizmo::drag_end(MultiUndoCommand *command)
{
	for (int i = 0; i < int(draggers_.size()); i++) {
		draggers_[i].end(command);
	}
}

}
