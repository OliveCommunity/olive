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

#ifndef OAK_VIDEORENDERFRAMECACHE_H
#define OAK_VIDEORENDERFRAMECACHE_H

#include "codec/frame.h"
#include "playbackcache.h"
#include "videoparams.h"

namespace olive
{

class FrameHashCache : public PlaybackCache {
public:
	FrameHashCache(Node *parent = nullptr);

	virtual ~FrameHashCache() override;

	const Rational &get_timebase() const
	{
		return timebase_;
	}

	void set_timebase(const Rational &tb);

	void validate_timestamp(const int64_t &ts);
	void validate_time(const Rational &time);

	bool is_frame_cached(const Rational &time) const
	{
		return get_validated_ranges().contains(time);
	}

	std::string get_valid_cache_filename(const Rational &time) const;

	static bool save_cache_frame(const std::string &filename, FramePtr frame);
	bool save_cache_frame(const int64_t &time, FramePtr frame) const;
	static bool save_cache_frame(const std::string &cache_path,
								 const std::string &uuid, const int64_t &time,
								 FramePtr frame);
	static bool save_cache_frame(const std::string &cache_path,
								 const std::string &uuid, const Rational &time,
								 const Rational &tb, FramePtr frame);
	static FramePtr load_cache_frame(const std::string &cache_path,
									 const std::string &uuid,
									 const int64_t &time);
	FramePtr load_cache_frame(const int64_t &time) const;
	static FramePtr load_cache_frame(const std::string &fn);

	virtual void set_passthrough(PlaybackCache *cache) override;

	// Formerly slots connected to DiskManager's `deleted_frame` /
	// `invalidate_project` signals; registered as explicit handlers now and
	// still public so the facade can re-wire if needed.
	void hash_deleted(const std::string &path, const std::string &filename);

	void project_invalidated(Project *p);

protected:
	virtual void LoadStateEvent(BinaryStreamReader &stream) override;
	virtual void SaveStateEvent(BinaryStreamWriter &stream) override;

private:
	Rational to_time(const int64_t &ts) const;
	int64_t to_timestamp(const Rational &ts,
						 Timecode::Rounding rounding = Timecode::k_round) const;

	/**
   * @brief Return the path of the cached image at this time
   */
	std::string cache_path_name(const int64_t &time) const;
	std::string cache_path_name(const Rational &time) const;

	static std::string cache_path_name(const std::string &cache_path,
									   const std::string &cache_id,
									   const int64_t &time);
	static std::string cache_path_name(const std::string &cache_path,
									   const std::string &cache_id,
									   const Rational &time, const Rational &tb);

	Rational timebase_;

	// DiskManager handler registration ids (0 = not registered)
	size_t deleted_frame_handler_id_;
	size_t invalidate_project_handler_id_;
};

class ThumbnailCache : public FrameHashCache {
public:
	ThumbnailCache(Node *parent = nullptr)
		: FrameHashCache(parent)
	{
		set_timebase(Rational(1, 10));
	}
};

}

#endif // OAK_VIDEORENDERFRAMECACHE_H
