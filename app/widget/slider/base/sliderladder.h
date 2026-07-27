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

#ifndef OAK_SLIDERLADDER_H
#define OAK_SLIDERLADDER_H

#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "oakutil/define.h"

namespace olive
{

class SliderLadderElement : public QWidget {
	Q_OBJECT
public:
	SliderLadderElement(const double &multiplier, QString width_hint,
						QWidget *parent = nullptr);

	void set_highlighted(bool e);

	void set_value(const QString &value);

	void set_multiplier_visible(bool e);

	double get_multiplier() const
	{
		return multiplier_;
	}

private:
	void update_label();

	QLabel *label_;

	double multiplier_;
	QString value_;

	bool highlighted_;

	bool multiplier_visible_;
};

class SliderLadder : public QFrame {
	Q_OBJECT
public:
	SliderLadder(double drag_multiplier, int nb_outer_values,
				 QString width_hint, QWidget *parent = nullptr);

	virtual ~SliderLadder() override;

	void set_value(const QString &s);

	void start_listening_to_mouse_input();

protected:
	virtual void mouseReleaseEvent(QMouseEvent *event) override;

	virtual void closeEvent(QCloseEvent *event) override;

signals:
	void dragged_by_value(int value, double multiplier);

	void released();

private:
	bool using_ladders() const;

	int drag_start_x_;
	int drag_start_y_;
	int wrap_count_;

	QList<SliderLadderElement *> elements_;

	int active_element_;

	QTimer drag_timer_;

	QScreen *screen_;

private slots:
	void timer_update();
};

}

#endif // OAK_SLIDERLADDER_H
