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

#include <cassert>
#include <string>
#include <vector>

#include <olive/core/core.h>

#include "output/track/track.h"
#include "subtitleparams.h"
#include "videoparams.h"

namespace olive
{

class FootageDescription {
public:
	FootageDescription(const std::string &decoder = std::string())
		: decoder_(decoder)
		, total_stream_count_(0)
		, has_source_start_time_(false)
	{
	}

	bool is_valid() const
	{
		return !decoder_.empty() &&
			   (!video_streams_.empty() || !audio_streams_.empty() ||
				!subtitle_streams_.empty());
	}

	const std::string &decoder() const
	{
		return decoder_;
	}

	void add_video_stream(const VideoParams &video_params)
	{
		assert(!has_stream_index(video_params.stream_index()));

		video_streams_.push_back(video_params);
	}

	void add_audio_stream(const AudioParams &audio_params)
	{
		assert(!has_stream_index(audio_params.stream_index()));

		audio_streams_.push_back(audio_params);
	}

	void add_subtitle_stream(const SubtitleParams &sub_params)
	{
		assert(!has_stream_index(sub_params.stream_index()));

		subtitle_streams_.push_back(sub_params);
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
		for (const VideoParams &vp : video_streams_) {
			if (vp.stream_index() == index) {
				return true;
			}
		}

		return false;
	}

	bool stream_is_audio(int index) const
	{
		for (const AudioParams &ap : audio_streams_) {
			if (ap.stream_index() == index) {
				return true;
			}
		}

		return false;
	}

	bool stream_is_subtitle(int index) const
	{
		for (const SubtitleParams &sp : subtitle_streams_) {
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

	void set_source_start_time(const Rational &time, const std::string &source)
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

	const std::string &source_start_time_source() const
	{
		return source_start_time_source_;
	}

	bool load(const std::string &filename);

	bool save(const std::string &filename) const;

	const std::vector<VideoParams> &get_video_streams() const
	{
		return video_streams_;
	}
	std::vector<VideoParams> &get_video_streams()
	{
		return video_streams_;
	}

	const std::vector<AudioParams> &get_audio_streams() const
	{
		return audio_streams_;
	}
	std::vector<AudioParams> &get_audio_streams()
	{
		return audio_streams_;
	}

	const std::vector<SubtitleParams> &get_subtitle_streams() const
	{
		return subtitle_streams_;
	}
	std::vector<SubtitleParams> &get_subtitle_streams()
	{
		return subtitle_streams_;
	}

private:
	static constexpr unsigned k_footage_meta_version = 7;

	std::string decoder_;

	std::vector<VideoParams> video_streams_;

	std::vector<AudioParams> audio_streams_;

	std::vector<SubtitleParams> subtitle_streams_;

	int total_stream_count_;

	Rational source_start_time_;

	std::string source_start_time_source_;

	bool has_source_start_time_;
};

}

#endif // OAK_FOOTAGEDESCRIPTION_H
