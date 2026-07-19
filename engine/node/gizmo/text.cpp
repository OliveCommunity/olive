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
#include "undo/undocommand.h"

namespace olive
{

TextGizmo::TextGizmo(QObject *parent)
	: NodeGizmo{ parent }
	, valign_(Qt::AlignTop)
{
}

void TextGizmo::set_rect(const QRectF &r)
{
	rect_ = r;
	emit rect_changed(rect_);
}

void TextGizmo::update_input_html(const QString &s, const Rational &time)
{
	if (input_.is_valid()) {
		MultiUndoCommand *command = new MultiUndoCommand();
		Node::set_value_at_time(input_.input(), time, s, input_.track(), command,
							 true);
		EngineCore::instance()->undo_stack()->push(command, tr("Edit Text"));
	}
}

void TextGizmo::set_vertical_alignment(Qt::Alignment va)
{
	valign_ = va;
	emit vertical_alignment_changed(valign_);
}

}
