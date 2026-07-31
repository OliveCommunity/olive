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

#ifndef OAK_RAZORTIMELINETOOL_H
#define OAK_RAZORTIMELINETOOL_H

#include "beam.h"

namespace olive
{

class RazorTool : public BeamTool {
public:
	RazorTool(TimelineWidget *parent);

	virtual void mouse_press(TimelineViewMouseEvent *event) override;
	virtual void mouse_move(TimelineViewMouseEvent *event) override;
	virtual void mouse_release(TimelineViewMouseEvent *event) override;

private:
	QVector<TrackReference> split_tracks_;
};

}

#endif // OAK_RAZORTIMELINETOOL_H
