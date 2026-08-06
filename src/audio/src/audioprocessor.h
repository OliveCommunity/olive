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

#ifndef OAK_AUDIOPROCESSOR_H
#define OAK_AUDIOPROCESSOR_H

#include <inttypes.h>
#include <vector>

#include <ffmpeg_bridge/ffmpeg_bridge.h>

#include "olive/core/render/audioparams.h"

namespace olive
{

class AudioProcessor {
public:
	AudioProcessor();

	~AudioProcessor();

	AudioProcessor(const AudioProcessor &) = delete;
	AudioProcessor &operator=(const AudioProcessor &) = delete;

	bool open(const core::AudioParams &from, const core::AudioParams &to,
			  double tempo = 1.0);

	void close();

	bool is_open() const
	{
		return graph_;
	}

	using Buffer = std::vector<std::vector<char>>;
	int convert(float **in, int nb_in_samples, AudioProcessor::Buffer *output);

	void flush();

	const core::AudioParams &from() const
	{
		return from_;
	}
	const core::AudioParams &to() const
	{
		return to_;
	}

private:
	FBAudioGraph *graph_;

	core::AudioParams from_;

	core::AudioParams to_;

	FBFrame *out_frame_;
};

}

#endif // OAK_AUDIOPROCESSOR_H
