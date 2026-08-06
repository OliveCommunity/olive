/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

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

#ifndef OAK_CODEC_FOOTAGEDESCRIPTION_H
#define OAK_CODEC_FOOTAGEDESCRIPTION_H

#include <cassert>
#include <string>
#include <vector>

#include "common/subtitleparams.h"
#include "common/videoparams.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/util/rational.h"

namespace olive
{

using core::AudioParams;
using core::Rational;

/**
 * @brief Codec-internal replacement for the former
 *        node/project/footage/footagedescription.h
 *
 * Value type describing the streams a Decoder::probe() found in a file.
 * oaknode has no C API counterpart, so codec keeps its own copy. Video and
 * subtitle streams are stored as oakcommon by-value handles; the class
 * addrefs on insert/copy and releases on destruction. The original's
 * Track::Type mapping (get_type_of_stream) and XML load/save (probe cache)
 * belonged to the oaknode-facing side and are intentionally not reproduced
 * here; consumers can use stream_is_video/audio/subtitle.
 */
class FootageDescription {
public:
	FootageDescription(const std::string &decoder = std::string())
		: decoder_(decoder)
		, total_stream_count_(0)
		, has_source_start_time_(false)
	{
	}

	FootageDescription(const FootageDescription &other)
		: decoder_(other.decoder_)
		, video_streams_(other.video_streams_)
		, audio_streams_(other.audio_streams_)
		, subtitle_streams_(other.subtitle_streams_)
		, total_stream_count_(other.total_stream_count_)
		, source_start_time_(other.source_start_time_)
		, source_start_time_source_(other.source_start_time_source_)
		, has_source_start_time_(other.has_source_start_time_)
	{
		for (const OakVideoParams &h : video_streams_) {
			if (h.ctx) {
				h.addref(h.ctx);
			}
		}
		for (const OakSubtitleParams &h : subtitle_streams_) {
			if (h.ctx) {
				h.addref(h.ctx);
			}
		}
	}

	FootageDescription &operator=(const FootageDescription &other)
	{
		if (this != &other) {
			release_streams();
			decoder_ = other.decoder_;
			video_streams_ = other.video_streams_;
			audio_streams_ = other.audio_streams_;
			subtitle_streams_ = other.subtitle_streams_;
			total_stream_count_ = other.total_stream_count_;
			source_start_time_ = other.source_start_time_;
			source_start_time_source_ = other.source_start_time_source_;
			has_source_start_time_ = other.has_source_start_time_;
			for (const OakVideoParams &h : video_streams_) {
				if (h.ctx) {
					h.addref(h.ctx);
				}
			}
			for (const OakSubtitleParams &h : subtitle_streams_) {
				if (h.ctx) {
					h.addref(h.ctx);
				}
			}
		}
		return *this;
	}

	~FootageDescription()
	{
		release_streams();
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

	void add_video_stream(const OakVideoParams &video_params)
	{
		assert(!has_stream_index(stream_index_of(video_params)));

		if (video_params.ctx) {
			video_params.addref(video_params.ctx);
		}
		video_streams_.push_back(video_params);
	}

	void add_audio_stream(const AudioParams &audio_params)
	{
		assert(!has_stream_index(audio_params.stream_index()));

		audio_streams_.push_back(audio_params);
	}

	void add_subtitle_stream(const OakSubtitleParams &sub_params)
	{
		assert(!has_stream_index(stream_index_of(sub_params)));

		if (sub_params.ctx) {
			sub_params.addref(sub_params.ctx);
		}
		subtitle_streams_.push_back(sub_params);
	}

	bool stream_is_video(int index) const
	{
		for (const OakVideoParams &vp : video_streams_) {
			if (stream_index_of(vp) == index) {
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
		for (const OakSubtitleParams &sp : subtitle_streams_) {
			if (stream_index_of(sp) == index) {
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

	const std::vector<OakVideoParams> &get_video_streams() const
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

	const std::vector<OakSubtitleParams> &get_subtitle_streams() const
	{
		return subtitle_streams_;
	}

private:
	static int stream_index_of(const OakVideoParams &params)
	{
		int index = -1;
		oakcommon_videoparams_get_stream_index(params, &index);
		return index;
	}

	static int stream_index_of(const OakSubtitleParams &params)
	{
		int index = -1;
		oakcommon_subtitleparams_get_stream_index(params, &index);
		return index;
	}

	void release_streams()
	{
		for (OakVideoParams &h : video_streams_) {
			if (h.ctx) {
				h.release(h.ctx);
			}
		}
		video_streams_.clear();
		for (OakSubtitleParams &h : subtitle_streams_) {
			if (h.ctx) {
				h.release(h.ctx);
			}
		}
		subtitle_streams_.clear();
	}

	std::string decoder_;

	std::vector<OakVideoParams> video_streams_;

	std::vector<AudioParams> audio_streams_;

	std::vector<OakSubtitleParams> subtitle_streams_;

	int total_stream_count_;

	Rational source_start_time_;

	std::string source_start_time_source_;

	bool has_source_start_time_;
};

}

#endif // OAK_CODEC_FOOTAGEDESCRIPTION_H
