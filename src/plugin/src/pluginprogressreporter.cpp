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
 */
#include "pluginprogressreporter.h"

namespace olive
{
namespace plugin
{
namespace
{

/**
 * @brief No-op reporter used when no UI factory is registered
 *
 * Never reports cancellation, so processing always continues.
 */
class NullPluginProgressReporter : public PluginProgressReporter {
public:
	void set_progress(double value) override
	{
		(void)value;
	}

	void show() override
	{
	}

	void close() override
	{
	}
};

PluginProgressReporterFactory reporter_factory_;

}

void set_plugin_progress_reporter_factory(
	PluginProgressReporterFactory factory)
{
	reporter_factory_ = std::move(factory);
}

PluginProgressReporter *
create_plugin_progress_reporter(const std::string &message,
								const std::string &title)
{
	if (reporter_factory_) {
		return reporter_factory_(message, title);
	}

	return new NullPluginProgressReporter();
}

}
}
