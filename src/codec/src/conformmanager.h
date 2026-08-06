/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OAK_CONFORMMANAGER_H
#define OAK_CONFORMMANAGER_H

#include <string>
#include <vector>

#include "decoder.h"
#include "olive/core/render/audioparams.h"

namespace olive
{

/**
 * @brief Manages audio conform (pcm cache) generation
 *
 * Qt-free interim state: actual conform work is delegated to the global
 * task submit callback (include/codec/task.h). While no callback is
 * registered (pre-M8), requests report k_conform_unavailable instead of
 * starting background work.
 *
 * Behavior changes vs. the Qt version:
 * - The `conform_ready` signal is gone; completion notification is the
 *   task system's / facade's business.
 * - Submission is synchronous: get_conform_state() calls the submit
 *   callback inline and re-checks the filesystem afterwards. `wait`
 *   only controls whether a post-submit miss is reported as
 *   k_conform_unavailable (wait) or k_conform_generating (queued).
 */
class ConformManager {
public:
	static void create_instance()
	{
		if (!instance_) {
			instance_ = new ConformManager();
		}
	}

	static void destroy_instance()
	{
		delete instance_;
		instance_ = nullptr;
	}

	static ConformManager *instance()
	{
		return instance_;
	}

	enum ConformState {
		k_conform_exists,
		k_conform_generating,
		k_conform_unavailable /**< No task callback registered / submit failed. */
	};

	struct Conform {
		ConformState state;
		std::vector<std::string> filenames;
	};

	/**
	 * @brief Get conform state, and start conforming if no conform exists
	 *
	 * Stateless and thread-safe. The decoder_id parameter of the Qt
	 * version was dropped: the task request addresses the source by
	 * filename/stream only.
	 */
	Conform get_conform_state(const std::string &cache_path,
							  const Decoder::CodecStream &stream,
							  const core::AudioParams &params, bool wait);

	/**
	 * @brief Get the destination filenames of an audio stream conformed to
	 * a set of parameters (one per channel)
	 *
	 * Pure path computation: never touches the filesystem and never
	 * submits work.
	 */
	static std::vector<std::string>
	get_conformed_filename(const std::string &cache_path,
						   const Decoder::CodecStream &stream,
						   const core::AudioParams &params);

private:
	ConformManager() = default;

	static ConformManager *instance_;

	static bool all_conforms_exist(const std::vector<std::string> &filenames);
};

} // namespace olive

#endif // OAK_CONFORMMANAGER_H
