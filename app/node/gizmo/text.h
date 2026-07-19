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

#ifndef OAK_TEXTGIZMO_H
#define OAK_TEXTGIZMO_H

#include "gizmo.h"
#include "node/param.h"

namespace olive
{

class TextGizmo : public NodeGizmo {
	Q_OBJECT
public:
	explicit TextGizmo(QObject *parent = nullptr);

	const QRectF &get_rect() const
	{
		return rect_;
	}
	void set_rect(const QRectF &r);

	const QString &get_html() const
	{
		return text_;
	}
	void set_html(const QString &t)
	{
		text_ = t;
	}

	void set_input(const NodeKeyframeTrackReference &input)
	{
		input_ = input;
	}

	void update_input_html(const QString &s, const Rational &time);

	Qt::Alignment get_vertical_alignment() const
	{
		return valign_;
	}
	void set_vertical_alignment(Qt::Alignment va);

signals:
	void activated();
	void deactivated();
	void vertical_alignment_changed(Qt::Alignment va);
	void rect_changed(const QRectF &r);

private:
	QRectF rect_;

	QString text_;

	NodeKeyframeTrackReference input_;

	Qt::Alignment valign_;
};

}

#endif // OAK_TEXTGIZMO_H
