#pragma once
// Minimal syntax-check stub for engine/pluginSupport/oliveplugininstance.h
// (real header is Qt-based, handled in a later milestone). Not in repo.
#include <memory>

#include "ofxhImageEffect.h"

namespace olive
{
namespace plugin
{
class PluginNode;
}

class OlivePluginInstance : public OFX::Host::ImageEffect::Instance {
public:
	void setNode(std::shared_ptr<plugin::PluginNode> node)
	{
		node_ = node;
	}
	std::shared_ptr<plugin::PluginNode> node() const
	{
		return node_;
	}

private:
	std::shared_ptr<plugin::PluginNode> node_;
};

}
