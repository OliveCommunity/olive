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

#ifndef OAK_TRACKVIEWITEM_H
#define OAK_TRACKVIEWITEM_H

#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

#include "engineeventbridge.h"
#include "node/output/track/track.h"
#include "oakengine/timeline.h"
#include "widget/clickablelabel/clickablelabel.h"
#include "widget/focusablelineedit/focusablelineedit.h"
#include "widget/timelinewidget/view/timelineviewmouseevent.h"

namespace olive
{

class TrackViewItem : public QWidget {
	Q_OBJECT
public:
	TrackViewItem(Track *track, QWidget *parent = nullptr);

signals:
	void about_to_delete_track(OakEngineTrack *track);

private:
	QPushButton *create_msl_button(const QColor &checked_color) const;

	QStackedWidget *stack_;

	ClickableLabel *label_;
	FocusableLineEdit *line_edit_;

	QPushButton *mute_button_;
	QPushButton *solo_button_;
	QPushButton *lock_button_;

	Track *track_;

	EngineEventBridge bridge_;

private slots:
	void label_clicked();

	void line_edit_confirmed();

	void line_edit_cancelled();

	void update_label();

	void show_context_menu(const QPoint &p);

	void delete_track();

	void delete_all_empty_tracks();

	void update_mute_button(bool e);

	void update_lock_button(bool e);
};

}

#endif // OAK_TRACKVIEWITEM_H
