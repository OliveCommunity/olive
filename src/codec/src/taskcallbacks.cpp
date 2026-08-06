/*

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

*/

#include "taskcallbacks.h"

#include <mutex>

namespace
{

std::mutex g_task_cb_mutex;
oakcodec_task_submit_fn g_task_cb = nullptr;
void *g_task_cb_userdata = nullptr;

} // namespace

extern "C" {

void oakcodec_set_task_submit_cb(oakcodec_task_submit_fn cb, void *userdata)
{
	std::lock_guard<std::mutex> lock(g_task_cb_mutex);
	g_task_cb = cb;
	g_task_cb_userdata = userdata;
}

int oakcodec_task_submit_is_registered(void)
{
	std::lock_guard<std::mutex> lock(g_task_cb_mutex);
	return g_task_cb != nullptr ? 1 : 0;
}

} // extern "C"

namespace olive
{

int SubmitTask(const OakCodecTaskRequest &req)
{
	std::lock_guard<std::mutex> lock(g_task_cb_mutex);
	if (!g_task_cb) {
		return OAKCODEC_E_STATE;
	}
	return g_task_cb(&req, g_task_cb_userdata);
}

} // namespace olive
