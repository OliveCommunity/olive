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

#ifndef OAK_PIXELSAMPLERWIDGET_H
#define OAK_PIXELSAMPLERWIDGET_H

#include <QLabel>
#include <QGroupBox>
#include <QWidget>

#include "widget/colorwheel/colorpreviewbox.h"

namespace olive
{

class PixelSamplerWidget : public QGroupBox {
	Q_OBJECT
public:
	PixelSamplerWidget(QWidget *parent = nullptr);

public slots:
	void set_values(const Color &color);

private:
	void update_label_internal();

	Color color_;

	ColorPreviewBox *box_;

	QLabel *label_;
};

class ManagedPixelSamplerWidget : public QWidget {
	Q_OBJECT
public:
	ManagedPixelSamplerWidget(QWidget *parent = nullptr);

public slots:
	void set_values(const Color &reference, const Color &display);

private:
	PixelSamplerWidget *reference_view_;

	PixelSamplerWidget *display_view_;
};

}

#endif // OAK_PIXELSAMPLERWIDGET_H
