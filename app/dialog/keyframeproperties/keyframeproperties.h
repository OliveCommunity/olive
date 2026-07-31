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

#ifndef OAK_KEYFRAMEPROPERTIESDIALOG_H
#define OAK_KEYFRAMEPROPERTIESDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QGroupBox>

#include "oakutil/oaknode.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/rationalslider.h"

namespace olive
{

class KeyframePropertiesDialog : public QDialog {
	Q_OBJECT
public:
	KeyframePropertiesDialog(const QVector<oak::Keyframe> &keys,
							 const Rational &timebase,
							 QWidget *parent = nullptr);

public slots:
	virtual void accept() override;

private:
	void set_up_bezier_slider(FloatSlider *slider, bool all_same, double value);

	const QVector<oak::Keyframe> &keys_;

	Rational timebase_;

	RationalSlider *time_slider_;

	QComboBox *type_select_;

	QGroupBox *bezier_group_;

	FloatSlider *bezier_in_x_slider_;

	FloatSlider *bezier_in_y_slider_;

	FloatSlider *bezier_out_x_slider_;

	FloatSlider *bezier_out_y_slider_;

private slots:
	void key_type_changed(int index);
};

}

#endif // OAK_KEYFRAMEPROPERTIESDIALOG_H
