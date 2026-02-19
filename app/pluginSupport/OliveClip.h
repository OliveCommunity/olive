/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef OLIVECLIP_H
#define OLIVECLIP_H
#include "image.h"
#include "ofxCore.h"
#include "ofxhClip.h"
#include "render/texture.h"
#include "render/videoparams.h"

#include <QMap>
#include <memory>
namespace olive
{
namespace plugin
{
class OliveClipInstance: public OFX::Host::ImageEffect::ClipInstance {
public:
	OliveClipInstance(OFX::Host::ImageEffect::Instance* effectInstance,
		OFX::Host::ImageEffect::ClipDescriptor& desc,VideoParams &params)
		: ClipInstance(effectInstance, desc)
		, name_(desc.getName())
	{
		params_ = params;
	}
    OFX::Host::ImageEffect::Image* getOutputImage(OfxTime time);

	const std::string &getUnmappedBitDepth() const override;
	const std::string &getUnmappedComponents() const override;
	const std::string &getPremult() const override;
	double getAspectRatio() const override;
	double getFrameRate() const override;
	void getFrameRange(double &startFrame, double &endFrame) const override;
	const std::string &getFieldOrder() const override;
	bool getConnected() const override;
	double getUnmappedFrameRate() const override;
	void getUnmappedFrameRange(double &startFrame, double &endFrame) const override;
	bool getContinuousSamples() const override;
	OFX::Host::ImageEffect::Image* getImage(OfxTime time, const OfxRectD *optionalBounds) override;
	OfxRectD getRegionOfDefinition(OfxTime time) const override;

	void setRegionOfDefinition(OfxRectD regionOfDefinition, OfxTime time);
	void setDefaultRegionOfDefinition(OfxRectD regionOfDefinition);
	void setParams(const VideoParams &params)
	{
		params_ = params;
	}
#   ifdef OFX_SUPPORTS_OPENGLRENDER
	OFX::Host::ImageEffect::Texture* loadTexture(OfxTime time,
												 const char *format,
												 const OfxRectD *optionalBounds) override;
#   endif

	void setInputTexture(TexturePtr texture, OfxTime time);
	void setOutputTexture(TexturePtr texture, OfxTime time);

private:
	VideoParams params_;

	QMap<OfxTime, OfxRectD> regionOfDefinitions_;

	OfxRectD defaultRegionOfDefinitions_;

	std::string name_;
	QMap<OfxTime, Image*> images_;
#ifdef OFX_SUPPORTS_OPENGLRENDER
	QMap<OfxTime, TexturePtr> input_textures_;
	QMap<OfxTime, TexturePtr> output_textures_;
#endif
};
}
}



#endif //OLIVECLIP_H
