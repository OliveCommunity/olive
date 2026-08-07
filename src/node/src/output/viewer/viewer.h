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

#ifndef OAK_VIEWER_H
#define OAK_VIEWER_H

#include <cstdio>

#include "codec/encoder.h"
#include "node.h"
#include "output/track/track.h"
#include "render/audioplaybackcache.h"
#include "render/framehashcache.h"
#include "subtitleparams.h"
#include "videoparams.h"
#include "timeline/marker.h"
#include "timeline/workarea.h"

namespace olive
{

class Footage;

/**
 * @brief A bridge between a node system and a ViewerPanel
 *
 * Receives update/time change signals from ViewerPanels and responds by sending them a texture of that frame
 */
class ViewerOutput : public Node {
public:
	ViewerOutput(bool create_buffer_inputs = true,
				 bool create_default_streams = true);
	virtual ~ViewerOutput() override;

	NODE_COPY_FUNCTION(ViewerOutput)

	virtual std::string name() const override;
	virtual std::string id() const override;
	virtual std::vector<CategoryID> category() const override;
	virtual std::string description() const override;

	virtual Variant data(const DataType &d) const override;

	void set_default_parameters();

	void set_parameters_from_footage(const std::vector<ViewerOutput *> footage);

	virtual void invalidate_cache(const TimeRange &range, const std::string &from,
								 int element,
								 InvalidateCacheOptions options) override;

	VideoParams get_video_params(int index = 0) const
	{
		// This check isn't strictly necessary (GetStandardValue will return a null VideoParams anyway),
		// but it does suppress a warning message that we don't need
		if (index < input_array_size(k_video_params_input)) {
			return get_standard_value(k_video_params_input, index)
				.value<VideoParams>();
		} else {
			return VideoParams();
		}
	}

	AudioParams get_audio_params(int index = 0) const
	{
		// This check isn't strictly necessary (GetStandardValue will return a null VideoParams anyway),
		// but it does suppress a warning message that we don't need
		if (index < input_array_size(k_audio_params_input)) {
			return get_standard_value(k_audio_params_input, index)
				.value<AudioParams>();
		} else {
			return AudioParams();
		}
	}

	SubtitleParams get_subtitle_params(int index = 0) const
	{
		// This check isn't strictly necessary (GetStandardValue will return a null VideoParams anyway),
		// but it does suppress a warning message that we don't need
		if (index < input_array_size(k_subtitle_params_input)) {
			return get_standard_value(k_subtitle_params_input, index)
				.value<SubtitleParams>();
		} else {
			return SubtitleParams();
		}
	}

	const Rational &get_playhead()
	{
		return playhead_;
	}

	void set_video_params(const VideoParams &video, int index = 0)
	{
		set_standard_value(k_video_params_input, Variant::from_value(video), index);
	}

	void set_audio_params(const AudioParams &audio, int index = 0)
	{
		set_standard_value(k_audio_params_input, Variant::from_value(audio), index);
	}

	void set_subtitle_params(const SubtitleParams &subs, int index = 0)
	{
		set_standard_value(k_subtitle_params_input, Variant::from_value(subs),
						 index);
	}

	int get_video_stream_count() const
	{
		return input_array_size(k_video_params_input);
	}

	int get_audio_stream_count() const
	{
		return input_array_size(k_audio_params_input);
	}

	int get_subtitle_stream_count() const
	{
		return input_array_size(k_subtitle_params_input);
	}

	virtual int get_total_stream_count() const
	{
		return get_video_stream_count() + get_audio_stream_count() +
			   get_subtitle_stream_count();
	}

	const AudioWaveformCache *get_connected_waveform()
	{
		if (Node *n = get_connected_sample_output()) {
			return n->waveform_cache();
		} else {
			return nullptr;
		}
	}

	bool has_enabled_video_streams() const;
	bool has_enabled_audio_streams() const;
	bool has_enabled_subtitle_streams() const;

	VideoParams get_first_enabled_video_stream() const;
	AudioParams get_first_enabled_audio_stream() const;
	SubtitleParams get_first_enabled_subtitle_stream() const;

	const Rational &get_length() const
	{
		return last_length_;
	}
	const Rational &get_video_length() const
	{
		return video_length_;
	}
	const Rational &get_audio_length() const
	{
		return audio_length_;
	}

	/**
	 * @brief Borrowed copies of the timeline handles owned by this
	 *        viewer. Callers must NOT free them; addref first to keep
	 *        one beyond the viewer's lifetime.
	 */
	const OakTimelineWorkArea &workarea_handle() const
	{
		return workarea_;
	}
	const OakTimelineMarkerList &markers_handle() const
	{
		return markers_;
	}

	virtual TimeRange get_video_cache_range() const override
	{
		return TimeRange(0, get_video_length());
	}

	virtual TimeRange get_audio_cache_range() const override
	{
		return TimeRange(0, get_audio_length());
	}

	std::vector<Track::Reference> get_enabled_streams_as_references() const;

	std::vector<VideoParams> get_enabled_video_streams() const;

	std::vector<AudioParams> get_enabled_audio_streams() const;

	virtual void retranslate() override;

	virtual Node *get_connected_texture_output();

	virtual ValueHint get_connected_texture_value_hint();

	virtual Node *get_connected_sample_output();

	virtual ValueHint get_connected_sample_value_hint();

	void set_waveform_enabled(bool e);

	bool is_video_auto_cache_enabled() const
	{
		fprintf(stderr, "sequence ac is a stub\n");
		return false;
	}
	void set_video_auto_cache_enabled(bool e)
	{
		(void) e;
		fprintf(stderr, "sequence ac is a stub\n");
	}

	virtual void value(const NodeValueRow &value, const NodeGlobals &globals,
					   NodeValueTable *table) const override;

	const EncodingParams &get_last_used_encoding_params() const
	{
		return last_used_encoding_params_;
	}
	void set_last_used_encoding_params(const EncodingParams &p)
	{
		last_used_encoding_params_ = p;
	}

	virtual bool load_custom(XmlStreamReader *reader,
							 SerializedData *data) override;
	virtual void save_custom(XmlStreamWriter *writer) const override;

	static const std::string k_video_params_input;
	static const std::string k_audio_params_input;
	static const std::string k_subtitle_params_input;

	static const std::string k_texture_input;
	static const std::string k_samples_input;

	static const SampleFormat k_default_sample_format;

	void verify_length();

	void set_playhead(const Rational &t);

protected:
	virtual void InputConnectedEvent(const std::string &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const std::string &input, int element,
										Node *output) override;

	virtual Rational verify_length_internal(Track::Type type) const;

	virtual void InputValueChangedEvent(const std::string &input,
										int element) override;

	int add_stream(Track::Type type, const Variant &value);
	int set_stream(Track::Type type, const Variant &value, int index);

private:
	Rational last_length_;
	Rational video_length_;
	Rational audio_length_;

	VideoParams cached_video_params_;

	AudioParams cached_audio_params_;

	OakTimelineWorkArea workarea_ = {};
	OakTimelineMarkerList markers_ = {};

	bool autocache_input_video_;
	bool autocache_input_audio_;

	EncodingParams last_used_encoding_params_;

	bool waveform_requests_enabled_;

	Rational playhead_;
};

}

#endif // OAK_VIEWER_H
