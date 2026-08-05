#pragma once
// Transitional stub for engine/pluginSupport/oliveplugininstance.h (real
// header still Qt-based, M9 处理). Only the surface render/plugin uses, with
// de-Qt'd boundary types. OFX HostSupport 交互签名一行不改。只增不删。
#include <memory>
#include <mutex>

#include "ofxhImageEffect.h"

#include "videoparams.h"

namespace olive
{
namespace plugin
{

class PluginNode;

class OlivePluginInstance : public OFX::Host::ImageEffect::Instance {
public:
	void setVideoParam(VideoParams params)
	{
		(void) params;
	}

	// oaknode 侧契约（plugin.cpp）
	void setNode(std::shared_ptr<PluginNode> node)
	{
		node_ = node;
	}

	std::shared_ptr<PluginNode> node() const
	{
		return node_;
	}

	void setOpenGLEnabled(bool enabled)
	{
		(void) enabled;
	}

	bool isCreated() const
	{
		return false;
	}

	int getNClips() const
	{
		return 0;
	}

	OFX::Host::ImageEffect::ClipInstance *getNthClip(int i)
	{
		(void) i;
		return nullptr;
	}

	std::mutex &mutex()
	{
		return mutex_;
	}

private:
	std::mutex mutex_;
	std::shared_ptr<PluginNode> node_;
};

}
}
