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

#ifndef OAK_CONFORMTASK_H
#define OAK_CONFORMTASK_H

#include "codec/decoder.h"
#include "codec/task.h"
#include "task.h"

namespace olive
{

/**
 * @brief Audio conform task (pcm cache generation)
 *
 * Executes an OAKCODEC_TASK_CONFORM request: decodes the audio stream
 * and writes per-channel pcm cache files (to ".working" names first,
 * renamed to the final paths on success).
 */
class ConformTask : public Task {
public:
	ConformTask(const OakCodecTaskRequest &request);

	/**
	 * @brief Derive sibling per-channel and ".working" filenames from the
	 *        first channel's final path (".../<base>.0.pcm")
	 *
	 * Extracted for testability. `channel_count` must be >= 1.
	 */
	static bool derive_filenames(const std::string &first_channel_final,
								 int channel_count,
								 std::vector<std::string> *final_names,
								 std::vector<std::string> *working_names);

protected:
	virtual bool run() override;

private:
	std::string input_filename_;
	std::string output_filename_;
	int stream_index_;
	int sample_rate_;
	uint64_t channel_layout_;
	int sample_format_;
};

}

#endif // OAK_CONFORMTASK_H
