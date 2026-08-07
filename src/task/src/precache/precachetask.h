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

#ifndef OAK_PRECACHETASK_H
#define OAK_PRECACHETASK_H

#include "node/footage.h"
#include "node/sequence.h"
#include "render/render.h"

namespace olive
{

class PreCacheTask : public RenderTask {
public:
	PreCacheTask(OakNodeFootage *footage, int index,
				 OakNodeSequence *sequence);

	virtual ~PreCacheTask() override;

protected:
	virtual bool run() override;

	virtual bool frame_downloaded(OakCodecFrame *frame,
								  const Rational &time) override;

	virtual bool audio_downloaded(const TimeRange &range,
								  OakSampleBuffer *samples) override;

private:
	OakNodeProject *project_;

	OakNodeFootage *footage_;

	OakAudioParams *audio_params_;
};

}

#endif // OAK_PRECACHETASK_H
