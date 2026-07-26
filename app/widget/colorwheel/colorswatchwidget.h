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

#ifndef OAK_COLORSWATCHWIDGET_H
#define OAK_COLORSWATCHWIDGET_H

#include <QOpenGLWidget>

#include "widget/manageddisplay/colorprocessorhandle.h"

namespace olive
{

class ColorSwatchWidget : public QWidget {
	Q_OBJECT
public:
	ColorSwatchWidget(QWidget *parent = nullptr);

	const Color &get_selected_color() const;

	void set_color_processor(ColorProcessorHandlePtr to_linear,
						   ColorProcessorHandlePtr to_display);

public slots:
	void set_selected_color(const Color &c);

signals:
	void selected_color_changed(const Color &c);

protected:
	virtual void mousePressEvent(QMouseEvent *e) override;

	virtual void mouseMoveEvent(QMouseEvent *e) override;

	virtual Color get_color_from_screen_pos(const QPoint &p) const = 0;

	virtual void SelectedColorChangedEvent(const Color &c, bool external);

	Qt::GlobalColor get_ui_selector_color() const;

	Color get_managed_color(const Color &input) const;

private:
	void set_selected_color_internal(const Color &c, bool external);

	Color selected_color_;

	ColorProcessorHandlePtr to_linear_processor_;

	ColorProcessorHandlePtr to_display_processor_;
};

}

#endif // OAK_COLORSWATCHWIDGET_H
