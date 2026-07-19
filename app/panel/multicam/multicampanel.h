/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_MULTICAMPANEL_H
#define OAK_MULTICAMPANEL_H

#include "panel/viewer/viewerbase.h"
#include "widget/multicam/multicamwidget.h"

namespace olive
{

class MulticamPanel : public TimeBasedPanel {
	Q_OBJECT
public:
	MulticamPanel();

	MulticamWidget *get_multicam_widget() const
	{
		return static_cast<MulticamWidget *>(get_time_based_widget());
	}

protected:
	virtual void retranslate() override;
};

}

#endif // OAK_MULTICAMPANEL_H
