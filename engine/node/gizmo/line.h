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

#ifndef OAK_LINEGIZMO_H
#define OAK_LINEGIZMO_H

#include <QLineF>

#include "gizmo.h"

namespace olive
{

class LineGizmo : public NodeGizmo {
	Q_OBJECT
public:
	LineGizmo(QObject *parent = nullptr);

	const QLineF &get_line() const
	{
		return line_;
	}
	void set_line(const QLineF &line)
	{
		line_ = line;
	}

	virtual void draw(QPainter *p) const override;

private:
	QLineF line_;
};

}

#endif // OAK_LINEGIZMO_H
