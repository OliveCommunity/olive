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

#ifndef OAK_PLUGINJOB_H
#define OAK_PLUGINJOB_H
#include "acceleratedjob.h"
#include "pluginSupport/oliveplugininstance.h"
#include "olive/core/util/rational.h"

#include <any>
#include <chrono>

namespace olive
{
namespace plugin
{

class PluginJob : public AcceleratedJob {
public:
	explicit PluginJob(const OFX::Host::ImageEffect::Instance *plugin_instance,
					   const PluginNode *node, NodeValueRow row,
					   const olive::core::Rational &time)
		: AcceleratedJob()
		, time_seconds_(time.to_double())
	{
		this->pluginInstance_ = plugin_instance;
		this->node_ = node;
		insert(row);
	}
	explicit PluginJob(const OFX::Host::ImageEffect::Instance *plugin_instance,
					   const PluginNode *node, NodeValueRow row)
		: PluginJob(plugin_instance, node, row, olive::core::Rational(0))
	{
	}

	PluginNode *node() const
	{
		return const_cast<PluginNode *>(node_);
	}

	OFX::Host::ImageEffect::Instance *plugin_instance()
	{
		return const_cast<OFX::Host::ImageEffect::Instance *>(pluginInstance_);
	}

	double time_seconds() const
	{
		return time_seconds_;
	}

private:
	const OFX::Host::ImageEffect::Instance *pluginInstance_ = nullptr;

	QHash<OfxTime, QHash<QString, std::any>> paramsOnTime_;

	QHash<QString, std::any> params_;

	const PluginNode *node_ = nullptr;
	double time_seconds_ = 0.0;
};

} // plugin
} // olive

#endif //OAK_PLUGINJOB_H
