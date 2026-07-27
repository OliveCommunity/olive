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

#ifndef OAK_TRACKVIEWSPLITTER_H
#define OAK_TRACKVIEWSPLITTER_H

#include <QSplitter>

#include "oakutil/define.h"

namespace olive
{

class TrackViewSplitterHandle : public QSplitterHandle {
	Q_OBJECT
public:
	TrackViewSplitterHandle(Qt::Orientation orientation, QSplitter *parent);

protected:
	virtual void mousePressEvent(QMouseEvent *e) override;
	virtual void mouseMoveEvent(QMouseEvent *e) override;
	virtual void mouseReleaseEvent(QMouseEvent *e) override;

	virtual void paintEvent(QPaintEvent *e) override;

private:
	int drag_y_;

	bool dragging_;
};

class TrackViewSplitter : public QSplitter {
	Q_OBJECT
public:
	TrackViewSplitter(Qt::Alignment vertical_alignment,
					  QWidget *parent = nullptr);

	void handle_receiver(TrackViewSplitterHandle *h, int diff);

	void set_height_with_sizes(QList<int> sizes);

	void insert(int index, int height, QWidget *item);
	void remove(int index);

	void set_spacer_height(int height);

public slots:
	void set_track_height(int index, int h);

signals:
	void track_height_changed(int index, int height);

protected:
	virtual QSplitterHandle *createHandle() override;

private:
	Qt::Alignment alignment_;

	int spacer_height_;
};

}

#endif // OAK_TRACKVIEWSPLITTER_H
