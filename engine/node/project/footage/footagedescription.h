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

#ifndef OAK_FOOTAGEDESCRIPTION_H
#define OAK_FOOTAGEDESCRIPTION_H

#include <QString>

#include "node/output/track/track.h"
#include "render/subtitleparams.h"
#include "render/videoparams.h"

namespace olive
{

class FootageDescription {
public:
	FootageDescription(const QString &decoder = QString())
		: decoder_(decoder)
		, total_stream_count_(0)
		, has_source_start_time_(false)
	{
	}

	bool is_valid() const
	{
		return !decoder_.isEmpty() &&
			   (!video_streams_.isEmpty() || !audio_streams_.isEmpty() ||
				!subtitle_streams_.isEmpty());
	}

	const QString &decoder() const
	{
		return decoder_;
	}

	void add_video_stream(const VideoParams &video_params)
	{
		Q_ASSERT(!has_stream_index(video_params.stream_index()));

		video_streams_.append(video_params);
	}

	void add_audio_stream(const AudioParams &audio_params)
	{
		Q_ASSERT(!has_stream_index(audio_params.stream_index()));

		audio_streams_.append(audio_params);
	}

	void add_subtitle_stream(const SubtitleParams &sub_params)
	{
		Q_ASSERT(!has_stream_index(sub_params.stream_index()));

		subtitle_streams_.append(sub_params);
	}

	Track::Type get_type_of_stream(int index)
	{
		if (stream_is_video(index)) {
			return Track::k_video;
		} else if (stream_is_audio(index)) {
			return Track::k_audio;
		} else if (stream_is_subtitle(index)) {
			return Track::k_subtitle;
		} else {
			return Track::k_none;
		}
	}

	bool stream_is_video(int index) const
	{
		foreach (const VideoParams &vp, video_streams_) {
			if (vp.stream_index() == index) {
				return true;
			}
		}

		return false;
	}

	bool stream_is_audio(int index) const
	{
		foreach (const AudioParams &ap, audio_streams_) {
			if (ap.stream_index() == index) {
				return true;
			}
		}

		return false;
	}

	bool stream_is_subtitle(int index) const
	{
		foreach (const SubtitleParams &sp, subtitle_streams_) {
			if (sp.stream_index() == index) {
				return true;
			}
		}

		return false;
	}

	bool has_stream_index(int index) const
	{
		return stream_is_video(index) || stream_is_audio(index) ||
			   stream_is_subtitle(index);
	}

	int get_stream_count() const
	{
		return total_stream_count_;
	}
	void set_stream_count(int s)
	{
		total_stream_count_ = s;
	}

	void set_source_start_time(const Rational &time, const QString &source)
	{
		source_start_time_ = time;
		source_start_time_source_ = source;
		has_source_start_time_ = true;
	}

	bool has_source_start_time() const
	{
		return has_source_start_time_;
	}

	const Rational &source_start_time() const
	{
		return source_start_time_;
	}

	const QString &source_start_time_source() const
	{
		return source_start_time_source_;
	}

	bool load(const QString &filename);

	bool save(const QString &filename) const;

	const QVector<VideoParams> &get_video_streams() const
	{
		return video_streams_;
	}
	QVector<VideoParams> &get_video_streams()
	{
		return video_streams_;
	}

	const QVector<AudioParams> &get_audio_streams() const
	{
		return audio_streams_;
	}
	QVector<AudioParams> &get_audio_streams()
	{
		return audio_streams_;
	}

	const QVector<SubtitleParams> &get_subtitle_streams() const
	{
		return subtitle_streams_;
	}
	QVector<SubtitleParams> &get_subtitle_streams()
	{
		return subtitle_streams_;
	}

private:
	static constexpr unsigned k_footage_meta_version = 7;

	QString decoder_;

	QVector<VideoParams> video_streams_;

	QVector<AudioParams> audio_streams_;

	QVector<SubtitleParams> subtitle_streams_;

	int total_stream_count_;

	Rational source_start_time_;

	QString source_start_time_source_;

	bool has_source_start_time_;
};

}

#endif // OAK_FOOTAGEDESCRIPTION_H
