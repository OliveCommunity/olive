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

#ifndef OAK_CLIPBLOCK_H
#define OAK_CLIPBLOCK_H

#include "audio/audiovisualwaveform.h"
#include "codec/decoder.h"
#include "node/block/block.h"
#include "node/input/multicam/multicamnode.h"
#include "node/output/track/track.h"

namespace olive
{

class ViewerOutput;

/**
 * @brief Node that represents a block of Media
 */
class ClipBlock : public Block {
	Q_OBJECT
public:
	ClipBlock();

	NODE_DEFAULT_FUNCTIONS(ClipBlock)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QString description() const override;

	virtual void set_length_and_media_out(const Rational &length) override;
	virtual void set_length_and_media_in(const Rational &length) override;

	Track::Type get_track_type() const
	{
		if (track()) {
			return track()->type();
		} else {
			return Track::k_none;
		}
	}

	virtual Node::ValueHint
	get_value_hint_for_input(const QString &input, int element = -1) const override;

	Rational media_in() const;
	void set_media_in(const Rational &media_in);

	bool is_autocaching() const
	{
		return get_standard_value(k_auto_cache_input).toBool();
	}
	void set_autocache(bool e);

	void discard_cache();

	virtual void invalidate_cache(const TimeRange &range, const QString &from,
								 int element,
								 InvalidateCacheOptions options) override;

	virtual TimeRange input_time_adjustment(const QString &input, int element,
										  const TimeRange &input_time,
										  bool clamp) const override;

	virtual TimeRange
	output_time_adjustment(const QString &input, int element,
						 const TimeRange &input_time) const override;

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	virtual void retranslate() override;

	void
	request_invalidated_from_connected(bool force_all = false,
									const TimeRange &intersect = TimeRange());

	double speed() const
	{
		return get_standard_value(k_speed_input).toDouble();
	}

	bool reverse() const
	{
		return get_standard_value(k_reverse_input).toBool();
	}

	void set_reverse(bool e)
	{
		set_standard_value(k_reverse_input, e);
	}

	bool maintain_audio_pitch() const
	{
		return get_standard_value(k_maintain_audio_pitch_input).toBool();
	}

	void set_maintain_audio_pitch(bool e)
	{
		set_standard_value(k_maintain_audio_pitch_input, e);
	}

	TransitionBlock *in_transition()
	{
		return in_transition_;
	}

	void set_in_transition(TransitionBlock *t)
	{
		in_transition_ = t;
	}

	TransitionBlock *out_transition()
	{
		return out_transition_;
	}

	void set_out_transition(TransitionBlock *t)
	{
		out_transition_ = t;
	}

	const QVector<Block *> &block_links() const
	{
		return block_links_;
	}

	FrameHashCache *connected_video_cache() const
	{
		if (Node *n = get_connected_output(k_buffer_in)) {
			return n->video_frame_cache();
		} else {
			return nullptr;
		}
	}

	AudioPlaybackCache *connected_audio_cache() const
	{
		if (Node *n = get_connected_output(k_buffer_in)) {
			return n->audio_playback_cache();
		} else {
			return nullptr;
		}
	}

	FrameHashCache *thumbnails()
	{
		if (Node *n = get_connected_output(k_buffer_in)) {
			return n->thumbnail_cache();
		} else {
			return nullptr;
		}
	}

	AudioWaveformCache *waveform()
	{
		if (Node *n = get_connected_output(k_buffer_in)) {
			return n->waveform_cache();
		} else {
			return nullptr;
		}
	}

	void add_cache_passthrough_from(ClipBlock *other);

	ViewerOutput *connected_viewer() const
	{
		return connected_viewer_;
	}

	virtual TimeRange get_video_cache_range() const override
	{
		return TimeRange(0, length());
	}

	virtual TimeRange get_audio_cache_range() const override
	{
		return TimeRange(0, length());
	}

	virtual void ConnectedToPreviewEvent() override;

	TimeRange media_range() const;

	/**
   * @brief Get currently set loop mode
   */
	LoopMode loop_mode() const
	{
		return static_cast<LoopMode>(get_standard_value(k_loop_mode_input).toInt());
	}

	void set_loop_mode(LoopMode l)
	{
		set_standard_value(k_loop_mode_input, int(l));
	}

	MultiCamNode *find_multicam();

	static const QString k_buffer_in;
	static const QString k_media_in_input;
	static const QString k_speed_input;
	static const QString k_reverse_input;
	static const QString k_maintain_audio_pitch_input;
	static const QString k_loop_mode_input;

	static const QString k_auto_cache_input;

protected:
	virtual void LinkChangeEvent() override;

	virtual void InputConnectedEvent(const QString &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const QString &input, int element,
										Node *output) override;

	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

private:
	enum SequenceToMediaTimeFlag {
		k_stm_none = 0x0,
		k_stm_ignore_reverse = 0x1,
		k_stm_ignore_speed = 0x2,
		k_stm_ignore_loop = 0x4
	};

	Rational sequence_to_media_time(const Rational &sequence_time,
								 uint64_t flags = k_stm_none) const;

	Rational media_to_sequence_time(const Rational &media_time) const;

	void request_range_from_connected(const TimeRange &range);

	void request_range_for_cache(PlaybackCache *cache, const TimeRange &max_range,
							  const TimeRange &range, bool invalidate,
							  bool request);
	void request_invalidated_for_cache(PlaybackCache *cache,
									const TimeRange &max_range);

	bool get_adjusted_thumbnail_range(TimeRange *r) const;

	QVector<Block *> block_links_;

	TransitionBlock *in_transition_;
	TransitionBlock *out_transition_;

	ViewerOutput *connected_viewer_;

private:
	Rational last_media_in_;
};

}

#endif // TIMELINEBLOCK_H
