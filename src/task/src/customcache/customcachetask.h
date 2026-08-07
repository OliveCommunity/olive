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

#ifndef OAK_CUSTOMCACHETASK_H
#define OAK_CUSTOMCACHETASK_H

#include <condition_variable>
#include <functional>
#include <mutex>

#include "task.h"

namespace olive
{

class CustomCacheTask : public Task {
public:
	CustomCacheTask(const std::string &sequence_name);

	void finish();

	/**
	 * @brief Called when the task is cancelled by anything other than
	 *        finish() (async return channel, 01 §4 exception)
	 */
	void set_cancelled_callback(std::function<void()> callback)
	{
		cancelled_callback_ = std::move(callback);
	}

protected:
	virtual bool run() override;

	virtual void cancel_event() override;

private:
	std::mutex mutex_;

	std::condition_variable wait_cond_;

	bool cancelled_through_finish_;

	std::function<void()> cancelled_callback_;
};

}

#endif // OAK_CUSTOMCACHETASK_H
