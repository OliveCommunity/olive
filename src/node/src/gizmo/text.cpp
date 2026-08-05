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

#include "text.h"

#include "coreengine.h"
#include "node.h"
#include "undocommand.h"

namespace olive
{

TextGizmo::TextGizmo(Node *parent)
	: NodeGizmo{ parent }
	, valign_(k_align_top)
{
}

void TextGizmo::set_rect(const RectF &r)
{
	rect_ = r;
}

void TextGizmo::update_input_html(const std::string &s, const Rational &time)
{
	if (input_.is_valid()) {
		MultiUndoCommand *command = new MultiUndoCommand();
		Node::set_value_at_time(input_.input(), time, s, input_.track(), command,
							 true);
		EngineCore::instance()->undo_stack()->push(command, "Edit Text");
	}
}

void TextGizmo::set_vertical_alignment(int va)
{
	valign_ = va;
}

}
