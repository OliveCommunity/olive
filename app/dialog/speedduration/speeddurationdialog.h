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

#ifndef OAK_SPEEDDURATIONDIALOG_H
#define OAK_SPEEDDURATIONDIALOG_H

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>

#include "node/block/gap/gap.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/rationalslider.h"

namespace olive {

class ClipBlock;

class SpeedDurationDialog : public QDialog {
	Q_OBJECT
public:
	explicit SpeedDurationDialog(const QVector<ClipBlock *> &clips,
								 const Rational &timebase,
								 QWidget *parent = nullptr);

public slots:
	virtual void accept() override;

signals:

private:
	static Rational get_length_adjustment(const Rational &original_length,
										double original_speed, double new_speed,
										const Rational &timebase);

	static double get_speed_adjustment(double original_speed,
									 const Rational &original_length,
									 const Rational &new_length);

	QVector<ClipBlock *> clips_;

	FloatSlider *speed_slider_;

	RationalSlider *dur_slider_;

	QCheckBox *link_box_;

	QCheckBox *reverse_box_;

	QCheckBox *maintain_audio_pitch_box_;

	QCheckBox *ripple_box_;

	QComboBox *loop_combo_;

	int start_reverse_;

	int start_maintain_audio_pitch_;

	double start_speed_;

	Rational start_duration_;

	int start_loop_;

	Rational timebase_;

private slots:
	void speed_changed(double s);

	void duration_changed(const Rational &r);
};

}

#endif // OAK_SPEEDDURATIONDIALOG_H
