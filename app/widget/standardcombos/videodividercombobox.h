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

#ifndef OAK_VIDEODIVIDERCOMBOBOX_H
#define OAK_VIDEODIVIDERCOMBOBOX_H

#include <QComboBox>

#include "oakengine/videoparams.h"

namespace olive
{

class VideoDividerComboBox : public QComboBox {
	Q_OBJECT
public:
	VideoDividerComboBox(QWidget *parent = nullptr)
		: QComboBox(parent)
	{
		const int n = oakengine_video_params_supported_divider_count();
		for (int i = 0; i < n; i++) {
			int d = oakengine_video_params_supported_divider_at(i);
			char name_buf[64];
			oakengine_video_params_divider_name(d, name_buf, sizeof(name_buf));
			this->addItem(QString::fromUtf8(name_buf), d);
		}
	}

	int get_divider() const
	{
		return this->currentData().toInt();
	}

	void set_divider(int d)
	{
		for (int i = 0; i < this->count(); i++) {
			if (this->itemData(i).toInt() == d) {
				this->setCurrentIndex(i);
				break;
			}
		}
	}
};

}

#endif // OAK_VIDEODIVIDERCOMBOBOX_H
