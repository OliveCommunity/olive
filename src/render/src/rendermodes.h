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

#ifndef OAK_RENDERMODE_H
#define OAK_RENDERMODE_H

#include "define.h"

namespace olive
{

class RenderMode {
public:
	/**
   * @brief The primary different "modes" the renderer can function in
   */
	enum Mode {
		/**
     * This render is for realtime preview ONLY and does not need to be "perfect". Nodes can use lower-accuracy functions
     * to save performance when possible.
     */
		k_offline,

		/**
     * This render is some sort of export or master copy and Nodes should take time/bandwidth/system resources to produce
     * a higher accuracy version.
     */
		k_online
	};
};

/**
 * @brief Integer frame size, replacing QSize at the render ticket/params
 * boundary
 */
class FrameSize {
public:
	FrameSize() = default;
	FrameSize(int w, int h)
		: width_(w)
		, height_(h)
	{
	}

	bool is_null() const
	{
		return width_ == 0 && height_ == 0;
	}

	int width() const
	{
		return width_;
	}
	int height() const
	{
		return height_;
	}

	bool operator==(const FrameSize &rhs) const
	{
		return width_ == rhs.width_ && height_ == rhs.height_;
	}

private:
	int width_ = 0;
	int height_ = 0;
};

}

#endif // OAK_RENDERMODE_H
