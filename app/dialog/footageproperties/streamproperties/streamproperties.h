/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2020 Olive Team
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

#ifndef OAK_STREAMPROPERTIES_H
#define OAK_STREAMPROPERTIES_H

#include <QWidget>

#include "oakutil/define.h"

namespace olive
{

class StreamProperties : public QWidget {
public:
	StreamProperties(QWidget *parent = nullptr);

	virtual void accept(void *)
	{
	}

	virtual bool sanity_check()
	{
		return true;
	}
};

}

#endif // OAK_STREAMPROPERTIES_H
