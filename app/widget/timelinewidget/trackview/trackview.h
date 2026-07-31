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

#ifndef OAK_TRACKVIEW_H
#define OAK_TRACKVIEW_H

#include <QScrollArea>
#include <QSplitter>

#include "oakengine/timeline.h"
#include "trackviewitem.h"
#include "trackviewsplitter.h"

namespace olive
{

class TrackView : public QScrollArea {
	Q_OBJECT
public:
	TrackView(Qt::Alignment vertical_alignment = Qt::AlignTop,
			  QWidget *parent = nullptr);

	/// Bind to the track list of `sequence` for `track_type`
	/// (OAKENGINE_TRACK_TYPE_*). The view holds the (sequence, type) pair
	/// and resolves tracks through the C ABI (no engine track-list object
	/// crosses the boundary).
	void connect_track_list(OakEngineSequence *sequence, int track_type);
	void disconnect_track_list();

	void insert_track(OakEngineTrack *track);
	void remove_track(OakEngineTrack *track);
signals:
	void about_to_delete_track(OakEngineTrack *track);

protected:
	virtual void resizeEvent(QResizeEvent *e) override;

private:
	OakEngineSequence *sequence_;

	int track_type_;

	TrackViewSplitter *splitter_;

	Qt::Alignment alignment_;

	int last_scrollbar_max_;

private slots:
	void scrollbar_range_changed(int min, int max);

	void track_height_changed(int index, int height);
};

}

#endif // OAK_TRACKVIEW_H
