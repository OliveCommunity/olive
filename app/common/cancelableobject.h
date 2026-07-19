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

#ifndef OAK_CANCELABLEOBJECT_H
#define OAK_CANCELABLEOBJECT_H

#include "common/define.h"
#include "render/cancelatom.h"

namespace olive
{

class CancelableObject {
public:
	CancelableObject()
	{
	}

	void cancel()
	{
		cancel_.cancel();
		CancelEvent();
	}

	CancelAtom *get_cancel_atom()
	{
		return &cancel_;
	}

	bool is_cancelled()
	{
		return cancel_.is_cancelled();
	}

protected:
	virtual void CancelEvent()
	{
	}

private:
	CancelAtom cancel_;
};

}

#endif // OAK_CANCELABLEOBJECT_H
