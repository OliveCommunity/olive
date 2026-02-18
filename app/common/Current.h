/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef CURRENT_H
#define CURRENT_H
#include "pluginSupport/OliveHost.h"
#include "render/videoparams.h"
#include "render/job/pluginjob.h"

class Current {
public:
	static Current& getInstance()
	{
		return current;
	}
	olive::VideoParams& currentVideoParams()
	{
		return currentVideoParams_;
	}
	olive::AudioParams& currentAudioParams()
	{
		return currentAudioParams_;
	}
	void setCurrentVideoParams(olive::VideoParams& params)
	{
		currentVideoParams_ = params;
	}
	void setCurrentAudioParams(olive::AudioParams& params)
	{
		currentAudioParams_ = params;
	}
	void setCurrentVideoParams(olive::VideoParams&& params)
	{
		currentVideoParams_ = params;
	}
	void setCurrentAudioParams(olive::AudioParams&& params)
	{
		currentAudioParams_ = params;
	}
	bool interactive()
	{
		return true;
	}

	std::shared_ptr<olive::plugin::OliveHost> pluginHost()
	{
		return myHost;
	}

	void setPluginHost(std::shared_ptr<olive::plugin::OliveHost> host)
	{
		myHost = host;
	}

	std::shared_ptr<OFX::Host::ImageEffect::PluginCache> pluginCache()
	{
		return plugin_cache_;
	}

	void setPluginCache(std::shared_ptr<OFX::Host::ImageEffect::PluginCache> cache)
	{
		plugin_cache_ = cache;
	}
private:
	static Current current;
	olive::VideoParams currentVideoParams_;
	olive::AudioParams currentAudioParams_;
	std::shared_ptr<olive::plugin::OliveHost> myHost;
	std::shared_ptr<OFX::Host::ImageEffect::PluginCache> plugin_cache_;
};



#endif //CURRENT_H
