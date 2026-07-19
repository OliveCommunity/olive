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
#include <olive/core/core.h>
#include <QByteArray>

#include <ffmpeg_bridge/ffmpeg_bridge.h>

#include "common/define.h"

namespace olive
{

using namespace core;

class AudioProcessor {
public:
	AudioProcessor();

	~AudioProcessor();

	DISABLE_COPY_MOVE(AudioProcessor)

	bool open(const AudioParams &from, const AudioParams &to,
			  double tempo = 1.0);

	void close();

	bool is_open() const
	{
		return graph_;
	}

	using Buffer = QVector<QByteArray>;
	int convert(float **in, int nb_in_samples, AudioProcessor::Buffer *output);

	void flush();

	const AudioParams &from() const
	{
		return from_;
	}
	const AudioParams &to() const
	{
		return to_;
	}

private:
	FBAudioGraph *graph_;

	AudioParams from_;

	AudioParams to_;

	FBFrame *out_frame_;
};

}

#endif // OAK_AUDIOPROCESSOR_H
