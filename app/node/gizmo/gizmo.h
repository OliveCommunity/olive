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

#ifndef OAK_NODEGIZMO_H
#define OAK_NODEGIZMO_H

#include <QObject>
#include <QPainter>

#include "node/globals.h"

namespace olive
{

class NodeGizmo : public QObject {
	Q_OBJECT
public:
	explicit NodeGizmo(QObject *parent = nullptr);
	virtual ~NodeGizmo() override;

	virtual void draw(QPainter *p) const
	{
	}

	const NodeGlobals &get_globals() const
	{
		return globals_;
	}
	void set_globals(const NodeGlobals &globals)
	{
		globals_ = globals;
	}

	bool is_visible() const
	{
		return visible_;
	}
	void set_visible(bool e)
	{
		visible_ = e;
	}

signals:

private:
	NodeGlobals globals_;

	bool visible_;
};

}

#endif // OAK_NODEGIZMO_H
