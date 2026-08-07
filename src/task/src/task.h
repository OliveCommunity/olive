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

#ifndef OAK_TASK_H
#define OAK_TASK_H

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "render/cancelatom.h"

#include <new>

namespace olive
{

/**
 * @brief A base class for background tasks running in Olive.
 *
 * Tasks are multithreaded by design (i.e. they will always spawn
 * a new thread and run in it).
 *
 * To subclass your own Task, override run() and return TRUE on success or FALSE on failure. Note that a Task can
 * provide a "negative" output and still have succeeded. For example, the ProbeTask's role is to determine whether a
 * certain media file can be used in Olive. Even if the probe *fails* to find a Decoder for this file, the Task itself
 * has *succeeded* at discovering this. A failure of ProbeTask would indicate a catastrophic failure meaning it was
 * unable to determine anything about the file.
 *
 * Tasks should be used with the TaskManager which will manage starting and deleting them.
 *
 * De-Qt version: Qt signals are replaced by listener callbacks
 * (01 §4: async tasks are the one place callbacks are allowed - they are
 * the command's return channel).
 */
class Task {
public:
	/**
	 * @brief Task lifecycle events delivered to listeners
	 */
	enum EventType {
		k_event_started,
		k_event_progress,
		k_event_finished
	};

	/**
	 * @brief Listener callback. For k_event_finished, `value` is 1.0 on
	 * success and 0.0 on failure; for k_event_progress it is 0..1; for
	 * k_event_started it is the start time in milliseconds.
	 */
	using EventListener = std::function<void(EventType, double)>;

	Task()
		: title_("Task")
		, error_("Unknown error")
		, start_time_(0)
		, cancel_atom_(oakrender_cancelatom_init())
	{
	}

	virtual ~Task()
	{
		oakrender_cancelatom_free(&cancel_atom_);
	}

	/**
	 * @brief Retrieve the current title of this Task
	 */
	const std::string &get_title() const
	{
		return title_;
	}

	/**
	 * @brief Returns the error that occurred if run() returns false
	 */
	const std::string &get_error() const
	{
		return error_;
	}

	const int64_t &get_start_time() const
	{
		return start_time_;
	}

	/**
	 * @brief Register a lifecycle listener (async command return channel)
	 */
	void add_event_listener(EventListener listener)
	{
		std::lock_guard<std::mutex> lock(listeners_mutex_);
		listeners_.push_back(std::move(listener));
	}

	/**
	 * @brief Run this task
	 *
	 * @return True if the task completed successfully, false if not.
	 *
	 * \see get_error() if this returns false.
	 */
	bool start()
	{
		start_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
						  std::chrono::system_clock::now().time_since_epoch())
						  .count();
		emit_event(k_event_started, double(start_time_));

		bool ret = run();

		emit_event(k_event_finished, ret ? 1.0 : 0.0);

		return ret;
	}

	/**
	 * @brief Reset state so that run() can be called again.
	 */
	virtual void reset()
	{
	}

	/**
	 * @brief Cancel the Task
	 *
	 * Sends a signal to the Task to stop as soon as possible. Always call this directly.
	 */
	void cancel()
	{
		oakrender_cancelatom_cancel(cancel_atom_);
		cancel_event();
	}

	bool is_cancelled() const
	{
		int cancelled = 0;
		oakrender_cancelatom_is_cancelled(cancel_atom_, &cancelled);
		return cancelled != 0;
	}

	/**
	 * @brief The task's cancellation atom (borrowed handle; addref if
	 *        retained beyond the task's lifetime)
	 */
	OakCancelAtom get_cancel_atom() const
	{
		return cancel_atom_;
	}

	/**
	 * @brief Emit a progress value between 0.0 and 1.0
	 */
	void emit_progress(double p)
	{
		emit_event(k_event_progress, p);
	}

protected:
	virtual bool run() = 0;

	/**
	 * @brief Called when the task is cancelled (subclass hook)
	 */
	virtual void cancel_event()
	{
	}

	/**
	 * @brief Set the error message
	 */
	void set_error(const std::string &s)
	{
		error_ = s;
	}

	/**
	 * @brief Set the Task title
	 */
	void set_title(const std::string &s)
	{
		title_ = s;
	}

private:
	void emit_event(EventType type, double value)
	{
		std::vector<EventListener> listeners;
		{
			std::lock_guard<std::mutex> lock(listeners_mutex_);
			listeners = listeners_;
		}
		for (const EventListener &listener : listeners) {
			listener(type, value);
		}
	}

	std::string title_;

	std::string error_;

	int64_t start_time_;

	OakCancelAtom cancel_atom_;

	std::mutex listeners_mutex_;
	std::vector<EventListener> listeners_;
};

}

#endif // OAK_TASK_H
