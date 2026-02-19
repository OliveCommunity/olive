/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef PLUGINJOB_H
#define PLUGINJOB_H
#include "acceleratedjob.h"
#include "pluginSupport/OlivePluginInstance.h"
#include "olive/core/util/rational.h"

#include <any>
#include <chrono>

namespace olive {
namespace plugin {

class PluginJob :public AcceleratedJob{
public:
	explicit PluginJob(const OFX::Host::ImageEffect::Instance* pluginInstance,
					   const PluginNode* node, NodeValueRow row,
					   const olive::core::rational &time)
		: AcceleratedJob()
		, time_seconds_(time.toDouble())
	{
		this->pluginInstance_ = pluginInstance;
		this->node_=node;
		Insert(row);
	}
	explicit PluginJob(const OFX::Host::ImageEffect::Instance* pluginInstance,
					   const PluginNode* node, NodeValueRow row)
		: PluginJob(pluginInstance, node, row, olive::core::rational(0))
	{
	}

	PluginNode *node() const {
		return const_cast<PluginNode *>(node_);
	}

	OFX::Host::ImageEffect::Instance* pluginInstance() {
		return const_cast<OFX::Host::ImageEffect::Instance*>(pluginInstance_);
	}

	double time_seconds() const {
		return time_seconds_;
	}

private:
	const OFX::Host::ImageEffect::Instance *pluginInstance_=nullptr;

	QHash<OfxTime, QHash<QString, std::any>> paramsOnTime;

	QHash<QString, std::any> params;

	const PluginNode *node_=nullptr;
	double time_seconds_ = 0.0;
};

} // plugin
} // olive

#endif //PLUGINJOB_H
