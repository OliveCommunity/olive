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

#include "codec/encoder.h"
#include "node/node.h"
#include "node/output/track/track.h"
#include "render/audioplaybackcache.h"
#include "render/framehashcache.h"
#include "render/subtitleparams.h"
#include "render/videoparams.h"
#include "timeline/timelinemarker.h"
#include "timeline/timelineworkarea.h"

namespace olive
{

class Footage;

/**
 * @brief A bridge between a node system and a ViewerPanel
 *
 * Receives update/time change signals from ViewerPanels and responds by sending them a texture of that frame
 */
class ViewerOutput : public Node {
	Q_OBJECT
public:
	ViewerOutput(bool create_buffer_inputs = true,
				 bool create_default_streams = true);

	NODE_DEFAULT_FUNCTIONS(ViewerOutput)

	virtual QString name() const override;
	virtual QString id() const override;
	virtual QVector<CategoryID> category() const override;
	virtual QString description() const override;

	virtual QVariant data(const DataType &d) const override;

	void set_default_parameters();

	void set_parameters_from_footage(const QVector<ViewerOutput *> footage);

	virtual void invalidate_cache(const TimeRange &range, const QString &from,
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
		set_standard_value(k_video_params_input, QVariant::fromValue(video), index);
	}

	void set_audio_params(const AudioParams &audio, int index = 0)
	{
		set_standard_value(k_audio_params_input, QVariant::fromValue(audio), index);
	}

	void set_subtitle_params(const SubtitleParams &subs, int index = 0)
	{
		set_standard_value(k_subtitle_params_input, QVariant::fromValue(subs),
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

	TimelineWorkArea *get_work_area() const
	{
		return workarea_;
	}
	TimelineMarkerList *get_markers() const
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

	QVector<Track::Reference> get_enabled_streams_as_references() const;

	QVector<VideoParams> get_enabled_video_streams() const;

	QVector<AudioParams> get_enabled_audio_streams() const;

	virtual void retranslate() override;

	virtual Node *get_connected_texture_output();

	virtual ValueHint get_connected_texture_value_hint();

	virtual Node *get_connected_sample_output();

	virtual ValueHint get_connected_sample_value_hint();

	void set_waveform_enabled(bool e);

	bool is_video_auto_cache_enabled() const
	{
		qDebug() << "sequence ac is a stub";
		return false;
	}
	void set_video_auto_cache_enabled(bool e)
	{
		qDebug() << "sequence ac is a stub";
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

	virtual bool load_custom(QXmlStreamReader *reader,
							SerializedData *data) override;
	virtual void save_custom(QXmlStreamWriter *writer) const override;

	static const QString k_video_params_input;
	static const QString k_audio_params_input;
	static const QString k_subtitle_params_input;

	static const QString k_texture_input;
	static const QString k_samples_input;

	static const SampleFormat k_default_sample_format;

signals:
	void frame_rate_changed(const Rational &);

	void length_changed(const Rational &length);

	void size_changed(int width, int height);

	void pixel_aspect_changed(const Rational &pixel_aspect);

	void interlacing_changed(VideoParams::Interlacing mode);

	void video_params_changed();
	void audio_params_changed();

	void texture_input_changed();

	void sample_rate_changed(int sr);

	void connected_waveform_changed();

	void playhead_changed(const Rational &t);

public slots:
	void verify_length();

	void set_playhead(const Rational &t);

protected:
	virtual void InputConnectedEvent(const QString &input, int element,
									 Node *output) override;

	virtual void InputDisconnectedEvent(const QString &input, int element,
										Node *output) override;

	virtual Rational verify_length_internal(Track::Type type) const;

	virtual void InputValueChangedEvent(const QString &input,
										int element) override;

	int add_stream(Track::Type type, const QVariant &value);
	int set_stream(Track::Type type, const QVariant &value, int index);

private:
	Rational last_length_;
	Rational video_length_;
	Rational audio_length_;

	VideoParams cached_video_params_;

	AudioParams cached_audio_params_;

	TimelineWorkArea *workarea_;
	TimelineMarkerList *markers_;

	bool autocache_input_video_;
	bool autocache_input_audio_;

	EncodingParams last_used_encoding_params_;

	bool waveform_requests_enabled_;

	Rational playhead_;
};

}

#endif // OAK_VIEWER_H
