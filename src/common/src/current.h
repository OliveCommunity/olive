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

#ifndef OAK_CURRENT_H
#define OAK_CURRENT_H

#include <memory>

/**
 * @brief Thread-shared holder for the "current" session-wide objects.
 *
 * De-Qt version of the old engine/common Current. The external objects
 * (video/audio params, plugin host, plugin cache) live outside
 * oakcommon, so they are kept as type-erased pointers
 * (std::shared_ptr<void>); callers recover the concrete type with
 * std::static_pointer_cast. Slots may be empty (nullptr).
 */
class Current {
public:
	static Current &get_instance()
	{
		return current_;
	}

	std::shared_ptr<void> current_video_params() const
	{
		return video_params_;
	}

	std::shared_ptr<void> current_audio_params() const
	{
		return audio_params_;
	}

	void set_current_video_params(std::shared_ptr<void> params)
	{
		video_params_ = params;
	}

	void set_current_audio_params(std::shared_ptr<void> params)
	{
		audio_params_ = params;
	}

	bool interactive() const
	{
		return true;
	}

	std::shared_ptr<void> plugin_host() const
	{
		return plugin_host_;
	}

	void set_plugin_host(std::shared_ptr<void> host)
	{
		plugin_host_ = host;
	}

	std::shared_ptr<void> plugin_cache() const
	{
		return plugin_cache_;
	}

	void set_plugin_cache(std::shared_ptr<void> cache)
	{
		plugin_cache_ = cache;
	}

private:
	static Current current_;
	std::shared_ptr<void> video_params_;
	std::shared_ptr<void> audio_params_;
	std::shared_ptr<void> plugin_host_;
	std::shared_ptr<void> plugin_cache_;
};

#endif // OAK_CURRENT_H
