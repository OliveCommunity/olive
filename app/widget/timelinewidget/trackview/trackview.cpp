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

#include "trackview.h"

#include <QDebug>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSplitter>
#include <QVBoxLayout>

#include "trackviewitem.h"

namespace olive
{

TrackView::TrackView(Qt::Alignment vertical_alignment, QWidget *parent)
	: QScrollArea(parent)
	, list_(nullptr)
	, alignment_(vertical_alignment)
{
	setAlignment(Qt::AlignLeft | alignment_);

	QWidget *central = new QWidget();
	setWidget(central);
	setWidgetResizable(true);

	QVBoxLayout *layout = new QVBoxLayout(central);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	if (alignment_ == Qt::AlignBottom) {
		layout->addStretch();

		connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
				&TrackView::scrollbar_range_changed);
		last_scrollbar_max_ = verticalScrollBar()->maximum();
	}

	splitter_ = new TrackViewSplitter(alignment_);
	splitter_->setChildrenCollapsible(false);
	layout->addWidget(splitter_);

	if (alignment_ == Qt::AlignTop) {
		layout->addStretch();
	}

	connect(splitter_, &TrackViewSplitter::track_height_changed, this,
			&TrackView::track_height_changed);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void TrackView::connect_track_list(TrackList *list)
{
	if (list_ != nullptr) {
		// Remove tracks
		for (int i = 0; i < list_->get_track_count(); i++) {
			splitter_->remove(0);
		}

		disconnect(list_, &TrackList::track_added, this,
				   &TrackView::insert_track);
		disconnect(list_, &TrackList::track_removed, this,
				   &TrackView::remove_track);
	}

	list_ = list;

	if (list_ != nullptr) {
		foreach (Track *track, list_->get_tracks()) {
			insert_track(track);
		}

		connect(list_, &TrackList::track_added, this, &TrackView::insert_track);
		connect(list_, &TrackList::track_removed, this, &TrackView::remove_track);
	}
}

void TrackView::disconnect_track_list()
{
	connect_track_list(nullptr);
}

void TrackView::resizeEvent(QResizeEvent *e)
{
	QScrollArea::resizeEvent(e);

	splitter_->set_spacer_height(height() / 2);
}

void TrackView::scrollbar_range_changed(int, int max)
{
	if (max != last_scrollbar_max_) {
		int ba_val = last_scrollbar_max_ - verticalScrollBar()->value();
		int new_val = max - ba_val;

		verticalScrollBar()->setValue(new_val);
		emit verticalScrollBar() -> valueChanged(new_val);

		last_scrollbar_max_ = max;
	}
}

void TrackView::track_height_changed(int index, int height)
{
	list_->get_track_at(index)->set_track_height_in_pixels(height);
}

void TrackView::insert_track(Track *track)
{
	TrackViewItem *tvi = new TrackViewItem(track);

	connect(tvi, &TrackViewItem::about_to_delete_track, this,
			&TrackView::about_to_delete_track);

	splitter_->insert(track->index(), track->get_track_height_in_pixels(), tvi);
}

void TrackView::remove_track(Track *track)
{
	splitter_->remove(track->index());
}

}
