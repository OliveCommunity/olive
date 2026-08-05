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

#ifndef OAK_FOOTAGEJOB_H
#define OAK_FOOTAGEJOB_H

#include <filesystem>
#include <string>

#include "acceleratedjob.h"
#include "loopmode.h"
#include "rendermodes.h"
#include "output/track/track.h"
#include "project/footage/footage.h"

namespace olive
{

class FootageJob : public AcceleratedJob {
public:
	FootageJob()
		: type_(Track::k_none)
	{
	}

	FootageJob(const TimeRange &time, const std::string &decoder,
			   const std::string &filename, Track::Type type,
			   const Rational &length, LoopMode loop_mode)
		: time_(time)
		, decoder_(decoder)
		, filename_(filename)
		, type_(type)
		, length_(length)
		, loop_mode_(loop_mode)
	{
	}

	const std::string &decoder() const
	{
		return decoder_;
	}

	const std::string &filename() const
	{
		return filename_;
	}

	bool has_proxy() const
	{
		return has_proxy_;
	}

	const std::string &proxy_filename() const
	{
		return proxy_filename_;
	}

	const std::string &proxy_decoder() const
	{
		return proxy_decoder_;
	}

	int proxy_stream_index() const
	{
		return proxy_stream_index_;
	}

	void set_proxy(const std::string &filename, const std::string &decoder,
				   int stream_index)
	{
		proxy_filename_ = filename;
		proxy_decoder_ = decoder;
		proxy_stream_index_ = stream_index;
		has_proxy_ = !filename.empty();
	}

	/**
	 * @brief Whether decoding for the given render mode should use the proxy
	 *
	 * Proxies are a preview accelerator only: offline (realtime preview)
	 * renders may decode from them, online (export/master) renders must
	 * always decode the original media. The proxy file must also still
	 * exist on disk, otherwise decoding falls back to the original.
	 */
	bool should_use_proxy(RenderMode::Mode mode) const
	{
		std::error_code ec;
		return mode == RenderMode::k_offline && has_proxy() &&
			std::filesystem::exists(proxy_filename_, ec);
	}

	Track::Type type() const
	{
		return type_;
	}

	const VideoParams &video_params() const
	{
		return video_params_;
	}

	void set_video_params(const VideoParams &p)
	{
		video_params_ = p;
	}

	const AudioParams &audio_params() const
	{
		return audio_params_;
	}

	void set_audio_params(const AudioParams &p)
	{
		audio_params_ = p;
	}

	const std::string &cache_path() const
	{
		return cache_path_;
	}

	void set_cache_path(const std::string &p)
	{
		cache_path_ = p;
	}

	const Rational &length() const
	{
		return length_;
	}

	void set_length(const Rational &length)
	{
		length_ = length;
	}

	const TimeRange &time() const
	{
		return time_;
	}

	LoopMode loop_mode() const
	{
		return loop_mode_;
	}
	void set_loop_mode(LoopMode m)
	{
		loop_mode_ = m;
	}

private:
	TimeRange time_;

	std::string decoder_;

	std::string filename_;

	bool has_proxy_ = false;

	std::string proxy_filename_;

	std::string proxy_decoder_;

	int proxy_stream_index_ = -1;

	Track::Type type_;

	VideoParams video_params_;

	AudioParams audio_params_;

	std::string cache_path_;

	Rational length_;

	LoopMode loop_mode_;
};

}

#endif // OAK_FOOTAGEJOB_H
