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

#include "clip.h"

#include "config/config.h"
#include "node/block/transition/transition.h"
#include "node/output/track/track.h"
#include "node/output/viewer/viewer.h"
#include "node/project/sequence/sequence.h"
#include "widget/slider/floatslider.h"
#include "widget/slider/rationalslider.h"

namespace olive
{

#define super Block

const QString ClipBlock::k_buffer_in = QStringLiteral("buffer_in");
const QString ClipBlock::k_media_in_input = QStringLiteral("media_in_in");
const QString ClipBlock::k_speed_input = QStringLiteral("speed_in");
const QString ClipBlock::k_reverse_input = QStringLiteral("reverse_in");
const QString ClipBlock::k_maintain_audio_pitch_input =
	QStringLiteral("maintain_audio_pitch_in");
const QString ClipBlock::k_auto_cache_input = QStringLiteral("autocache_in");
const QString ClipBlock::k_loop_mode_input = QStringLiteral("loop_in");

ClipBlock::ClipBlock()
	: in_transition_(nullptr)
	, out_transition_(nullptr)
	, connected_viewer_(nullptr)
{
	add_input(k_media_in_input, NodeValue::k_rational,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));
	set_input_property(k_media_in_input, QStringLiteral("view"),
					 RationalSlider::k_time);
	set_input_property(k_media_in_input, QStringLiteral("viewlock"), true);

	add_input(k_speed_input, NodeValue::k_float, 1.0,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));
	set_input_property(k_speed_input, QStringLiteral("view"),
					 FloatSlider::k_percentage);
	set_input_property(k_speed_input, QStringLiteral("min"), 0.0);

	add_input(k_reverse_input, NodeValue::k_boolean, false,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_maintain_audio_pitch_input, NodeValue::k_boolean, false,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	add_input(k_auto_cache_input, NodeValue::k_boolean, false,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));

	prepend_input(k_buffer_in, NodeValue::k_none,
				 InputFlags(k_input_flag_not_keyframable));
	//SetValueHintForInput(kBufferIn, ValueHint(NodeValue::kBuffer));

	set_effect_input(k_buffer_in);

	add_input(k_loop_mode_input, NodeValue::k_combo, 0,
			 InputFlags(k_input_flag_not_connectable | k_input_flag_not_keyframable));
}

QString ClipBlock::name() const
{
	if (connected_viewer_ && !connected_viewer_->get_label().isEmpty()) {
		return connected_viewer_->get_label();
	} else if (track()) {
		if (track()->type() == Track::k_video) {
			return tr("Video Clip");
		} else if (track()->type() == Track::k_audio) {
			return tr("Audio Clip");
		}
	}

	return tr("Clip");
}

QString ClipBlock::id() const
{
	return QStringLiteral("org.olivevideoeditor.Olive.clip");
}

QString ClipBlock::description() const
{
	return tr("A time-based node that represents a media source.");
}

void ClipBlock::set_length_and_media_out(const Rational &length)
{
	if (length == this->length()) {
		return;
	}

	if (reverse()) {
		// Calculate media_in adjustment
		Rational proposed_media_in = sequence_to_media_time(
			this->length() - length, k_stm_ignore_reverse | k_stm_ignore_loop);
		set_media_in(proposed_media_in);
	}

	super::set_length_and_media_out(length);
}

void ClipBlock::set_length_and_media_in(const Rational &length)
{
	if (length == this->length()) {
		return;
	}

	Rational old_length = this->length();

	super::set_length_and_media_in(length);

	if (!reverse()) {
		// Calculate media_in adjustment
		set_media_in(sequence_to_media_time(old_length - length, k_stm_ignore_loop));
	}
}

Rational ClipBlock::media_in() const
{
	return get_standard_value(k_media_in_input).value<Rational>();
}

Node::ValueHint ClipBlock::get_value_hint_for_input(const QString &input,
												int element) const
{
	if (input == k_buffer_in) {
		// The buffer input takes whatever the connected node provides, so it
		// is declared as kNone and carries no stored hint. When the connected
		// node pushes more than one value type (a footage pushes both a
		// kTexture job and a kSamples job), a typeless lookup falls back to
		// the last value in the table, which may feed audio samples into a
		// video clip and produce a black frame. Prefer the value type that
		// matches this clip's track.
		switch (get_track_type()) {
		case Track::k_video:
			return ValueHint(QVector<NodeValue::Type>{ NodeValue::k_texture });
		case Track::k_audio:
			return ValueHint(QVector<NodeValue::Type>{ NodeValue::k_samples });
		default:
			break;
		}
	}

	return super::get_value_hint_for_input(input, element);
}

void ClipBlock::set_media_in(const Rational &media_in)
{
	set_standard_value(k_media_in_input, QVariant::fromValue(media_in));

	request_invalidated_from_connected();
}

void ClipBlock::set_autocache(bool e)
{
	set_standard_value(k_auto_cache_input, e);
}

void ClipBlock::discard_cache()
{
	if (Node *connected = get_connected_output(k_buffer_in)) {
		Track::Type type = get_track_type();
		if (type == Track::k_video) {
			connected->video_frame_cache()->invalidate(
				TimeRange(RATIONAL_MIN, RATIONAL_MAX));
		} else if (type == Track::k_audio) {
			connected->audio_playback_cache()->invalidate(
				TimeRange(RATIONAL_MIN, RATIONAL_MAX));
		}
	}
}

Rational ClipBlock::sequence_to_media_time(const Rational &sequence_time,
										uint64_t flags) const
{
	// These constants are not considered "values" per se, so we don't modify them
	if (sequence_time == RATIONAL_MIN || sequence_time == RATIONAL_MAX) {
		return sequence_time;
	}

	Rational media_time = sequence_time;

	if (reverse() && !(flags & k_stm_ignore_reverse)) {
		media_time = length() - media_time;
	}

	if (!(flags & k_stm_ignore_speed)) {
		double speed_value = speed();
		if (qIsNull(speed_value)) {
			// Effectively holds the frame at the in point
			media_time = 0;
		} else if (!qFuzzyCompare(speed_value, 1.0)) {
			// Multiply time
			media_time =
				Rational::from_double(media_time.to_double() * speed_value);
		}
	}

	media_time += media_in();

	/*if (!(flags & kSTMIgnoreLoop)
      && this->loop_mode() != kLoopModeOff
      && connected_viewer_
      && !connected_viewer_->GetLength().isNull()
      && (media_time < 0 || media_time >= connected_viewer_->GetLength())) {
    if (loop_mode() == kLoopModeLoop) {
      while (media_time < 0) {
        media_time += connected_viewer_->GetLength();
      }
      while (media_time >= connected_viewer_->GetLength()) {
        media_time -= connected_viewer_->GetLength();
      }
    } else if (loop_mode() == kLoopModeClamp) {
      media_time = std::clamp(media_time, Rational(0), connected_viewer_->GetLength()-connected_viewer_->GetVideoParams().frame_rate_as_time_base());
    }
  }*/

	return media_time;
}

Rational ClipBlock::media_to_sequence_time(const Rational &media_time) const
{
	// These constants are not considered "values" per se, so we don't modify them
	if (media_time == RATIONAL_MIN || media_time == RATIONAL_MAX) {
		return media_time;
	}

	Rational sequence_time = media_time - media_in();

	double speed_value = speed();
	if (qIsNull(speed_value)) {
		// Speed zero holds the frame at the in point, so map to that frame
		sequence_time = media_in();
	} else if (!qFuzzyCompare(speed_value, 1.0)) {
		// Divide time
		sequence_time =
			Rational::from_double(sequence_time.to_double() / speed_value);
	}

	if (reverse()) {
		sequence_time = length() - sequence_time;
	}

	return sequence_time;
}

void ClipBlock::request_range_from_connected(const TimeRange &range)
{
	Track::Type type = get_track_type();

	if (type == Track::k_video || type == Track::k_audio) {
		if (Node *connected = get_connected_output(k_buffer_in)) {
			TimeRange max_range = media_range();
			if (type == Track::k_video) {
				// Handle thumbnails
				request_range_for_cache(connected->thumbnail_cache(), max_range,
									 range, true, false);
				{
					TimeRange thumb_range = range.intersected(max_range);
					if (get_adjusted_thumbnail_range(&thumb_range)) {
						connected->thumbnail_cache()->request(
							this->track()->sequence(), thumb_range);
					}
				}

				// Handle video cache
				request_range_for_cache(connected->video_frame_cache(), max_range,
									 range, true, is_autocaching());
			} else if (type == Track::k_audio) {
				// Handle waveforms
				request_range_for_cache(
					connected->waveform_cache(), max_range, range, true,
					(OAK_CONFIG("TimelineWaveformMode").toInt() ==
					 Timeline::k_waveforms_enabled));

				// Handle audio cache
				request_range_for_cache(connected->audio_playback_cache(),
									 max_range, range, true, is_autocaching());
			}
		}
	}
}

void ClipBlock::request_invalidated_from_connected(bool force_all,
												const TimeRange &intersect)
{
	Track::Type type = get_track_type();

	if (type == Track::k_video || type == Track::k_audio) {
		if (Node *connected = get_connected_output(k_buffer_in)) {
			TimeRange max_range = media_range();

			if (!intersect.length().isNull()) {
				max_range = max_range.intersected(intersect);
			}

			if (type == Track::k_video) {
				// Handle thumbnails
				TimeRange thumb_range = max_range;
				if (get_adjusted_thumbnail_range(&thumb_range)) {
					request_invalidated_for_cache(connected->thumbnail_cache(),
											   thumb_range);
				}

				// Handle video cache
				if (is_autocaching() || force_all) {
					request_invalidated_for_cache(connected->video_frame_cache(),
											   max_range);
				}
			} else if (type == Track::k_audio) {
				// Handle waveforms
				if (OAK_CONFIG("TimelineWaveformMode").toInt() ==
					Timeline::k_waveforms_enabled) {
					request_invalidated_for_cache(connected->waveform_cache(),
											   max_range);
				}

				// Handle audio cache
				if (is_autocaching() || force_all) {
					request_invalidated_for_cache(
						connected->audio_playback_cache(), max_range);
				}
			}
		}
	}
}

void ClipBlock::request_range_for_cache(PlaybackCache *cache,
									 const TimeRange &max_range,
									 const TimeRange &range, bool invalidate,
									 bool request)
{
	TimeRange r = range.intersected(max_range);

	if (invalidate) {
		cache->invalidate(r);
	}

	if (request) {
		cache->request(this->track()->sequence(), r);
	}
}

void ClipBlock::request_invalidated_for_cache(PlaybackCache *cache,
										   const TimeRange &max_range)
{
	TimeRangeList invalid = cache->get_invalidated_ranges(max_range);

	for (const PlaybackCache::Passthrough &p : cache->get_passthroughs()) {
		invalid.remove(p);
	}

	for (const TimeRange &r : invalid) {
		request_range_for_cache(cache, max_range, r, false, true);
	}
}

bool ClipBlock::get_adjusted_thumbnail_range(TimeRange *r) const
{
	switch (static_cast<Timeline::ThumbnailMode>(
		OAK_CONFIG("TimelineThumbnailMode").toInt())) {
	case Timeline::k_thumbnail_off:
		// Don't cache any range
		return false;
	case Timeline::k_thumbnail_in_out: {
		// Only cache in point
		Rational in = this->media_range().in();
		if (r->contains(in)) {
			// Cache only the in point
			*r = TimeRange(in, in + thumbnail_cache()->get_timebase());
			return true;
		} else {
			// Cache nothing
			return false;
		}
	}
	case Timeline::k_thumbnail_on:
		// Cache entire range
		return true;
	}

	// Fallback
	return true;
}

void ClipBlock::invalidate_cache(const TimeRange &range, const QString &from,
								int element, InvalidateCacheOptions options)
{
	Q_UNUSED(element)

	// If signal is from texture input, transform all times from media time to sequence time
	if (from == k_buffer_in) {
		// Render caches where necessary
		if (are_caches_enabled()) {
			request_range_from_connected(range);
		}

		// Adjust range from media time to sequence time
		TimeRange adj;
		double speed_value = speed();

		if (qIsNull(speed_value)) {
			// Handle 0 speed by invalidating the whole clip
			adj = TimeRange(RATIONAL_MIN, RATIONAL_MAX);
		} else {
			adj = TimeRange(media_to_sequence_time(range.in()),
							media_to_sequence_time(range.out()));
		}

		// Find connected viewer node
		auto viewers = find_input_nodes_connected_to_input<ViewerOutput>(
			NodeInput(this, k_buffer_in), 1);
		ViewerOutput *new_connected_viewer =
			viewers.isEmpty() ? nullptr : viewers.first();

		if (new_connected_viewer != connected_viewer_) {
			if (connected_viewer_) {
				disconnect(connected_viewer_->get_markers(),
						   &TimelineMarkerList::marker_added, this,
						   &ClipBlock::preview_changed);
				disconnect(connected_viewer_->get_markers(),
						   &TimelineMarkerList::marker_removed, this,
						   &ClipBlock::preview_changed);
				disconnect(connected_viewer_->get_markers(),
						   &TimelineMarkerList::marker_modified, this,
						   &ClipBlock::preview_changed);
			}

			connected_viewer_ = new_connected_viewer;

			if (connected_viewer_) {
				connect(connected_viewer_->get_markers(),
						&TimelineMarkerList::marker_added, this,
						&ClipBlock::preview_changed);
				connect(connected_viewer_->get_markers(),
						&TimelineMarkerList::marker_removed, this,
						&ClipBlock::preview_changed);
				connect(connected_viewer_->get_markers(),
						&TimelineMarkerList::marker_modified, this,
						&ClipBlock::preview_changed);
			}
		}

		super::invalidate_cache(adj, from, element, options);
	} else {
		// Otherwise, pass signal along normally
		super::invalidate_cache(range, from, element, options);
	}
}

void ClipBlock::LinkChangeEvent()
{
	block_links_.clear();

	foreach (Node *n, links()) {
		ClipBlock *b = dynamic_cast<ClipBlock *>(n);

		if (b) {
			block_links_.append(b);
		}
	}
}

void ClipBlock::InputConnectedEvent(const QString &input, int element,
									Node *output)
{
	super::InputConnectedEvent(input, element, output);

	if (input == k_buffer_in) {
		connect(output->thumbnail_cache(), &FrameHashCache::invalidated, this,
				&Block::preview_changed);
		connect(output->waveform_cache(), &AudioPlaybackCache::invalidated,
				this, &Block::preview_changed);
		connect(output->video_frame_cache(), &FrameHashCache::invalidated, this,
				&Block::preview_changed);
		connect(output->audio_playback_cache(),
				&AudioPlaybackCache::invalidated, this, &Block::preview_changed);
		connect(output->thumbnail_cache(), &FrameHashCache::validated, this,
				&Block::preview_changed);
		connect(output->waveform_cache(), &AudioPlaybackCache::validated, this,
				&Block::preview_changed);
		connect(output->video_frame_cache(), &FrameHashCache::validated, this,
				&Block::preview_changed);
		connect(output->audio_playback_cache(), &AudioPlaybackCache::validated,
				this, &Block::preview_changed);
	}
}

void ClipBlock::InputDisconnectedEvent(const QString &input, int element,
									   Node *output)
{
	super::InputDisconnectedEvent(input, element, output);

	if (input == k_buffer_in) {
		disconnect(output->thumbnail_cache(), &FrameHashCache::invalidated,
				   this, &Block::preview_changed);
		disconnect(output->waveform_cache(), &AudioPlaybackCache::invalidated,
				   this, &Block::preview_changed);
		disconnect(output->video_frame_cache(), &FrameHashCache::invalidated,
				   this, &Block::preview_changed);
		disconnect(output->audio_playback_cache(),
				   &AudioPlaybackCache::invalidated, this,
				   &Block::preview_changed);
		disconnect(output->thumbnail_cache(), &FrameHashCache::validated, this,
				   &Block::preview_changed);
		disconnect(output->waveform_cache(), &AudioPlaybackCache::validated,
				   this, &Block::preview_changed);
		disconnect(output->video_frame_cache(), &FrameHashCache::validated,
				   this, &Block::preview_changed);
		disconnect(output->audio_playback_cache(),
				   &AudioPlaybackCache::validated, this,
				   &Block::preview_changed);
	}
}

void ClipBlock::InputValueChangedEvent(const QString &input, int element)
{
	super::InputValueChangedEvent(input, element);

	if (input == k_auto_cache_input) {
		if (is_autocaching()) {
			request_invalidated_from_connected();
		} else {
			Track::Type type = get_track_type();

			if (Node *connected = get_connected_output(k_buffer_in)) {
				if (type == Track::k_video) {
					emit connected->video_frame_cache()->cancel_all();
				} else if (type == Track::k_audio) {
					emit connected->audio_playback_cache()->cancel_all();
				}
			}
		}
	} else if (input == k_loop_mode_input) {
		emit preview_changed();
	}
}

TimeRange ClipBlock::input_time_adjustment(const QString &input, int element,
										 const TimeRange &input_time,
										 bool clamp) const
{
	Q_UNUSED(element)

	if (input == k_buffer_in) {
		return TimeRange(sequence_to_media_time(input_time.in()),
						 sequence_to_media_time(input_time.out()));
	}

	return super::input_time_adjustment(input, element, input_time, clamp);
}

TimeRange ClipBlock::output_time_adjustment(const QString &input, int element,
										  const TimeRange &input_time) const
{
	Q_UNUSED(element)

	if (input == k_buffer_in) {
		return TimeRange(media_to_sequence_time(input_time.in()),
						 media_to_sequence_time(input_time.out()));
	}

	return super::output_time_adjustment(input, element, input_time);
}

void ClipBlock::value(const NodeValueRow &value, const NodeGlobals &globals,
					  NodeValueTable *table) const
{
	Q_UNUSED(globals)

	// We discard most values here except for the buffer we received
	NodeValue data = value[k_buffer_in];

	table->clear();
	if (data.type() != NodeValue::k_none) {
		table->push(data);
	}
}

void ClipBlock::retranslate()
{
	super::retranslate();

	set_input_name(k_buffer_in, tr("Buffer"));
	set_input_name(k_media_in_input, tr("Media In"));
	set_input_name(k_speed_input, tr("Speed"));
	set_input_name(k_reverse_input, tr("Reverse"));
	set_input_name(k_maintain_audio_pitch_input, tr("Maintain Audio Pitch"));
	set_input_name(k_loop_mode_input, tr("Loop"));
	set_combo_box_strings(k_loop_mode_input, { tr("None"), tr("Loop"), tr("Clamp") });
}

void ClipBlock::add_cache_passthrough_from(ClipBlock *other)
{
	if (auto tc = this->video_frame_cache()) {
		if (auto oc = other->video_frame_cache()) {
			tc->set_passthrough(oc);
		}
	}

	if (auto tc = this->audio_playback_cache()) {
		if (auto oc = other->audio_playback_cache()) {
			tc->set_passthrough(oc);
		}
	}

	if (auto tc = this->thumbnails()) {
		if (auto oc = other->thumbnails()) {
			tc->set_passthrough(oc);
		}
	}

	if (auto tc = this->waveform()) {
		if (auto oc = other->waveform()) {
			tc->set_passthrough(oc);
		}
	}
}

void ClipBlock::ConnectedToPreviewEvent()
{
	request_invalidated_from_connected();
}

TimeRange ClipBlock::media_range() const
{
	return input_time_adjustment(k_buffer_in, -1, TimeRange(0, length()), false);
}

MultiCamNode *ClipBlock::find_multicam()
{
	auto v = find_input_nodes_connected_to_input<MultiCamNode>(
		NodeInput(this, k_buffer_in), 1);
	if (v.empty()) {
		return nullptr;
	} else {
		return v.first();
	}
}

}
