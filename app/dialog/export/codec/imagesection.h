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

#ifndef OAK_IMAGESECTION_H
#define OAK_IMAGESECTION_H

#include <QCheckBox>

#include "codecsection.h"
#include "widget/slider/rationalslider.h"

namespace olive
{

class ImageSection : public CodecSection {
	Q_OBJECT
public:
	ImageSection(QWidget *parent = nullptr);

	bool is_image_sequence_checked() const
	{
		return image_sequence_checkbox_->isChecked();
	}

	void set_image_sequence_checked(bool e)
	{
		image_sequence_checkbox_->setChecked(e);
	}

	void set_timebase(const Rational &r)
	{
		frame_slider_->set_timebase(r);
	}

	Rational get_time() const
	{
		return frame_slider_->get_value();
	}

	void set_time(const Rational &t)
	{
		frame_slider_->set_value(t);
	}

signals:
	void time_changed(const Rational &t);

private:
	QCheckBox *image_sequence_checkbox_;

	RationalSlider *frame_slider_;

private slots:
	void image_sequence_check_box_toggled(bool e);
};

}

#endif // OAK_IMAGESECTION_H
