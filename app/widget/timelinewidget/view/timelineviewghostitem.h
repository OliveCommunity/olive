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

#ifndef OAK_TIMELINEVIEWGHOSTITEM_H
#define OAK_TIMELINEVIEWGHOSTITEM_H

#include <QVariant>

#include <olive/core/core.h>

#include "common/trackreferencehandle.h"
#include "oakengine/node.h"
#include "oakengine/timeline.h"
#include "oakutil/qtutils.h"
#include "timeline/timelinecommonapp.h"
#include "widget/timelinewidget/cliphandle.h"

namespace olive
{

using olive::core::Rational;

/**
 * @brief TrackReference of the track owning `block`
 * (the owning track as a reference facade). Type ordinals are the
 * TrackReference mirror values, pinned to the engine track-type ordinals
 * by the static_asserts in trackreferencehandle.h.
 */
inline TrackReference ghost_block_track_reference(OakEngineBlock *block)
{
	OakEngineNode *track = block_track_handle(block);
	if (!track) {
		return TrackReference();
	}
	return TrackReference(
		static_cast<TrackReference::Type>(oakengine_track_get_type(track)),
		oakengine_track_get_index(track));
}

/**
 * @brief A graphical representation of changes the user is making before they apply it
 */
class TimelineViewGhostItem {
public:
	enum DataType {
		k_attached_block,
		k_reference_block,
		k_attached_footage,
		k_ghost_is_sliding,
		k_trim_is_a_roll_edit,
		k_trim_should_be_ignored
	};

	struct AttachedFootage {
		OakEngineNode *footage;
		QString output;
	};

	TimelineViewGhostItem()
		: track_adj_(0)
		, mode_(TimelineApp::k_none)
		, can_have_zero_length_(true)
		, can_move_tracks_(true)
		, invisible_(false)
	{
	}

	static TimelineViewGhostItem *from_block(OakEngineBlock *block)
	{
		TimelineViewGhostItem *ghost = new TimelineViewGhostItem();

		// All engine queries go through the C ABI: oakengine_node_is_clip()/
		// oakengine_node_is_transition() replace dynamic_cast (the block
		// classes are abstract and carry no own type id), the range comes
		// from the block facade as rational seconds.
		OakEngineNode *block_node = reinterpret_cast<OakEngineNode *>(block);

		int in_num = 0, in_den = 1, out_num = 0, out_den = 1;
		oakengine_block_get_in_rational(block_node, &in_num, &in_den);
		oakengine_block_get_out_rational(block_node, &out_num, &out_den);
		ghost->set_in(Rational(in_num, in_den));
		ghost->set_out(Rational(out_num, out_den));

		if (oakengine_node_is_clip(block_node)) {
			ghost->set_media_in(clip_media_in(block));
		}
		ghost->set_track(ghost_block_track_reference(block));
		ghost->set_data(k_attached_block, QtUtils::ptr_to_value(block));

		if (oakengine_node_is_clip(block_node)) {
			ghost->can_have_zero_length_ = false;
		} else if (oakengine_node_is_transition(block_node)) {
			ghost->can_have_zero_length_ = false;
		}

		return ghost;
	}

	bool can_have_zero_length() const
	{
		return can_have_zero_length_;
	}

	bool get_can_move_tracks() const
	{
		return can_move_tracks_;
	}

	void set_can_move_tracks(bool e)
	{
		can_move_tracks_ = e;
	}

	const Rational &get_in() const
	{
		return in_;
	}

	const Rational &get_out() const
	{
		return out_;
	}

	const Rational &get_media_in() const
	{
		return media_in_;
	}

	Rational get_length() const
	{
		return out_ - in_;
	}

	Rational get_adjusted_length() const
	{
		return get_adjusted_out() - get_adjusted_in();
	}

	void set_in(const Rational &in)
	{
		in_ = in;
	}

	void set_out(const Rational &out)
	{
		out_ = out;
	}

	void set_media_in(const Rational &media_in)
	{
		media_in_ = media_in;
	}

	void set_in_adjustment(const Rational &in_adj)
	{
		in_adj_ = in_adj;
	}

	void set_out_adjustment(const Rational &out_adj)
	{
		out_adj_ = out_adj;
	}

	void set_track_adjustment(const int &track_adj)
	{
		track_adj_ = track_adj;
	}

	void set_media_in_adjustment(const Rational &media_in_adj)
	{
		media_in_adj_ = media_in_adj;
	}

	const Rational &get_in_adjustment() const
	{
		return in_adj_;
	}

	const Rational &get_out_adjustment() const
	{
		return out_adj_;
	}

	const Rational &get_media_in_adjustment() const
	{
		return media_in_adj_;
	}

	const int &get_track_adjustment() const
	{
		return track_adj_;
	}

	Rational get_adjusted_in() const
	{
		return in_ + in_adj_;
	}

	Rational get_adjusted_out() const
	{
		return out_ + out_adj_;
	}

	Rational get_adjusted_media_in() const
	{
		return media_in_ + media_in_adj_;
	}

	TrackReference get_adjusted_track() const
	{
		return TrackReference(track_.type(), track_.index() + track_adj_);
	}

	const TimelineApp::MovementMode &get_mode() const
	{
		return mode_;
	}

	void set_mode(const TimelineApp::MovementMode &mode)
	{
		mode_ = mode;
	}

	bool has_been_adjusted() const
	{
		return get_in_adjustment() != 0 || get_out_adjustment() != 0 ||
			   get_media_in_adjustment() != 0 || get_track_adjustment() != 0;
	}

	QVariant get_data(int key) const
	{
		return data_.value(key);
	}

	void set_data(int key, const QVariant &value)
	{
		data_.insert(key, value);
	}

	const TrackReference &get_track() const
	{
		return track_;
	}

	void set_track(const TrackReference &track)
	{
		track_ = track;
	}

	bool is_invisible() const
	{
		return invisible_;
	}

	void set_invisible(bool e)
	{
		invisible_ = e;
	}

protected:
private:
	Rational in_;
	Rational out_;
	Rational media_in_;

	Rational in_adj_;
	Rational out_adj_;
	Rational media_in_adj_;

	int track_adj_;

	TimelineApp::MovementMode mode_;

	bool can_have_zero_length_;
	bool can_move_tracks_;

	TrackReference track_;

	QHash<int, QVariant> data_;

	bool invisible_;
};

}

Q_DECLARE_METATYPE(olive::TimelineViewGhostItem::AttachedFootage)

#endif // OAK_TIMELINEVIEWGHOSTITEM_H
