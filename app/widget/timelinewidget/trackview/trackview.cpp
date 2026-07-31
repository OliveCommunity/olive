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

#include "oakengine/node.h"
#include "oakengine/timeline.h"

namespace olive
{

namespace
{
/// Number of tracks of `track_type` (OAKENGINE_TRACK_TYPE_*) in `sequence`.
int track_count_for(OakEngineSequence *sequence, int track_type)
{
	int video = 0, audio = 0, subtitle = 0;
	oakengine_sequence_track_count(sequence, &video, &audio, &subtitle);
	switch (track_type) {
	case OAKENGINE_TRACK_TYPE_VIDEO:
		return video;
	case OAKENGINE_TRACK_TYPE_AUDIO:
		return audio;
	case OAKENGINE_TRACK_TYPE_SUBTITLE:
		return subtitle;
	}
	return 0;
}
} // namespace

TrackView::TrackView(Qt::Alignment vertical_alignment, QWidget *parent)
	: QScrollArea(parent)
	, sequence_(nullptr)
	, track_type_(OAKENGINE_TRACK_TYPE_VIDEO)
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

void TrackView::connect_track_list(OakEngineSequence *sequence, int track_type)
{
	if (sequence_ != nullptr) {
		// Remove tracks
		const int count = track_count_for(sequence_, track_type_);
		for (int i = 0; i < count; i++) {
			splitter_->remove(0);
		}
	}

	sequence_ = sequence;
	track_type_ = track_type;

	if (sequence_ != nullptr) {
		const int count = track_count_for(sequence_, track_type_);
		for (int i = 0; i < count; i++) {
			insert_track(oakengine_sequence_track_at(sequence_, track_type_, i));
		}
	}
}

void TrackView::disconnect_track_list()
{
	connect_track_list(nullptr, track_type_);
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
	oakengine_track_set_height(
		sequence_, track_type_, index,
		oakengine_track_height_pixels_to_internal(height));
}

void TrackView::insert_track(OakEngineTrack *track)
{
	TrackViewItem *tvi = new TrackViewItem(track);

	connect(tvi, &TrackViewItem::about_to_delete_track, this,
			&TrackView::about_to_delete_track);

	const int index = oakengine_track_get_index(
		reinterpret_cast<OakEngineNode *>(track));
	double internal_height = 0.0;
	oakengine_track_get_height(sequence_, track_type_, index,
							   &internal_height);
	splitter_->insert(
		index, oakengine_track_height_internal_to_pixels(internal_height),
		tvi);
}

void TrackView::remove_track(OakEngineTrack *track)
{
	splitter_->remove(oakengine_track_get_index(
		reinterpret_cast<OakEngineNode *>(track)));
}

}
