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

#include <atomic>

#include "audio/error.h"
#include "audio/manager.h"

namespace
{
std::atomic<int> g_alive{ 0 };
}

namespace oakaudio
{

void alive_inc()
{
	g_alive.fetch_add(1, std::memory_order_relaxed);
}

void alive_dec()
{
	g_alive.fetch_sub(1, std::memory_order_relaxed);
}

}

extern "C" int oakaudio_debug_alive_count(void)
{
	return g_alive.load(std::memory_order_relaxed);
}
