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

#include "customcachetask.h"

namespace olive
{

CustomCacheTask::CustomCacheTask(const std::string &sequence_name)
	: cancelled_through_finish_(false)
{
	set_title("Caching custom range for \"" + sequence_name + "\"");
}

void CustomCacheTask::finish()
{
	std::lock_guard<std::mutex> lock(mutex_);

	cancelled_through_finish_ = true;
	cancel();
}

bool CustomCacheTask::run()
{
	std::unique_lock<std::mutex> lock(mutex_);

	while (!is_cancelled()) {
		wait_cond_.wait(lock);
	}

	return true;
}

void CustomCacheTask::cancel_event()
{
	if (!cancelled_through_finish_ && cancelled_callback_) {
		cancelled_callback_();
	}
	wait_cond_.notify_one();
}

}
