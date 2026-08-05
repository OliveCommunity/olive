#pragma once
// Transitional stub for engine/pluginSupport/oliveclip.h (real header still
// Qt-based, M9 处理). Only the surface render/plugin uses, with de-Qt'd
// boundary types (std::string / olive::VideoParams / TexturePtr). OFX
// HostSupport 交互签名一行不改。只增不删。
#include <mutex>
#include <string>

#include "ofxhImageEffect.h"
#include "ofxImageEffect.h"

#include "videoparams.h"
#include "render/texture.h"

namespace olive
{
namespace plugin
{

class OliveClipInstance : public OFX::Host::ImageEffect::ClipInstance {
public:
	OFX::Host::ImageEffect::Image *getOutputImage(OfxTime time)
	{
		(void) time;
		return nullptr;
	}

	const std::string &getUnmappedComponents() const override
	{
		static const std::string s;
		return s;
	}

	OFX::Host::ImageEffect::Image *
	getImage(OfxTime time, const OfxRectD *optional_bounds) override
	{
		(void) time;
		(void) optional_bounds;
		return nullptr;
	}

	void setRegionOfDefinition(OfxRectD region_of_definition, OfxTime time)
	{
		(void) region_of_definition;
		(void) time;
	}

	void setParams(const VideoParams &params)
	{
		(void) params;
	}

	void setInputTexture(TexturePtr texture, OfxTime time,
						 bool readback = true)
	{
		(void) texture;
		(void) time;
		(void) readback;
	}

	void setOutputTexture(TexturePtr texture, OfxTime time)
	{
		(void) texture;
		(void) time;
	}
};

}
}
