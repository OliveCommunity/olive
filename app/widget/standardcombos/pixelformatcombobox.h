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

#ifndef OAK_PIXELFORMATCOMBOBOX_H
#define OAK_PIXELFORMATCOMBOBOX_H

#include <QComboBox>

#include "oakengine/videoparams.h"

namespace olive
{

class PixelFormatComboBox : public QComboBox {
	Q_OBJECT
public:
	PixelFormatComboBox(bool float_only, QWidget *parent = nullptr)
		: QComboBox(parent)
	{
		// Set up preview formats (PixelFormat::u8=0 .. f32=4)
		for (int i = 0; i < 5; i++) {
			if (!float_only || oakengine_video_params_format_is_float(i)) {
				char name_buf[64];
				oakengine_video_params_pixel_format_name(i, name_buf,
														 sizeof(name_buf));
				this->addItem(QString::fromUtf8(name_buf), i);
			}
		}
	}

	PixelFormat get_pixel_format() const
	{
		return static_cast<PixelFormat::Format>(this->currentData().toInt());
	}

	void set_pixel_format(PixelFormat fmt)
	{
		for (int i = 0; i < this->count(); i++) {
			if (this->itemData(i).toInt() == fmt) {
				this->setCurrentIndex(i);
				break;
			}
		}
	}
};

}

#endif // OAK_PIXELFORMATCOMBOBOX_H
