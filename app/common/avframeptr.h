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

#ifndef AVFRAMEPTR_H
#define AVFRAMEPTR_H

extern "C" {
#include <libavutil/frame.h>
}

#include <memory>

namespace olive
{

using AVFramePtr = std::shared_ptr<AVFrame>;

inline AVFramePtr CreateAVFramePtr(AVFrame *f)
{
	return std::shared_ptr<AVFrame>(f, [](AVFrame *g) { av_frame_free(&g); });
}

inline AVFramePtr CreateAVFramePtr()
{
	return CreateAVFramePtr(av_frame_alloc());
}

}

#endif // AVFRAMEPTR_H
