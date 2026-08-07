/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef OAK_PLUGIN_PROGRESS_REPORTER_H
#define OAK_PLUGIN_PROGRESS_REPORTER_H

#include <functional>
#include <string>

namespace olive
{
namespace plugin
{

/**
 * @brief UI-independent interface for reporting plugin progress
 *
 * The engine cannot show UI itself, so OFX progress reporting goes through
 * this interface. The UI layer registers a factory (see
 * set_plugin_progress_reporter_factory()) that creates a reporter wrapping a
 * ProgressDialog; without a factory, a no-op reporter is used instead.
 *
 * Cancellation is delivered through a C-style callback.
 */
class PluginProgressReporter {
public:
	PluginProgressReporter() = default;

	virtual ~PluginProgressReporter() = default;

	virtual void set_progress(double value) = 0;

	virtual void show() = 0;

	virtual void close() = 0;

	/**
	 * @brief Register a callback to be invoked when the user cancels.
	 *
	 * Only one callback is supported; subsequent calls replace the
	 * previous registration. Pass nullptr to clear.
	 */
	void set_cancel_callback(std::function<void(void *)> cb, void *userdata)
	{
		cancel_callback_ = std::move(cb);
		cancel_callback_userdata_ = userdata;
	}

	/**
	 * @brief Mark this reporter as cancelled and notify the registered
	 * callback.
	 */
	void set_cancelled()
	{
		cancelled_ = true;
		if (cancel_callback_) {
			cancel_callback_(cancel_callback_userdata_);
		}
	}

	bool cancelled() const
	{
		return cancelled_;
	}

private:
	std::function<void(void *)> cancel_callback_;
	void *cancel_callback_userdata_ = nullptr;
	bool cancelled_ = false;
};

/**
 * @brief Factory creating a PluginProgressReporter for a progress session
 *
 * Registered by the UI layer at startup. The caller takes ownership of the
 * returned reporter.
 */
using PluginProgressReporterFactory =
	std::function<PluginProgressReporter *(const std::string &message,
										   const std::string &title)>;

void set_plugin_progress_reporter_factory(
	PluginProgressReporterFactory factory);

/**
 * @brief Create a progress reporter through the registered factory
 *
 * Without a factory, returns a no-op reporter so engine code can run
 * headless. The caller takes ownership of the returned reporter.
 */
PluginProgressReporter *
create_plugin_progress_reporter(const std::string &message,
								const std::string &title);

}
}

#endif // OAK_PLUGIN_PROGRESS_REPORTER_H
