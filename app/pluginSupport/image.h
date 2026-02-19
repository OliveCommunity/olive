/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#ifndef OLIVE_EDITOR_PLUGIN_IMAGE_H
#define OLIVE_EDITOR_PLUGIN_IMAGE_H

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxhClip.h"
#include "olive/core/render/pixelformat.h"
#include "render/loopmode.h"
#include "render/videoparams.h"
#include <cstdint>
#include <qtypes.h>
#include <string>
#include <vector>
namespace olive
{
namespace plugin
{
class Image : public OFX::Host::ImageEffect::Image {
public:
	Image(OFX::Host::ImageEffect::ClipInstance &clip_instance);
	Image(OFX::Host::ImageEffect::ClipInstance &clip_instance,
		  const VideoParams &params,
		  const OfxRectI &bounds,
		  const OfxRectI &rod,
		  bool clear = true);
	~Image();
	uint8_t *data() {
		return (uint8_t *)getPointerProperty(kOfxImagePropData);
	}
	int width();
	int height();
	core::PixelFormat pixel_format();
	bool premultiplied_alpha();
	int channel_count();

	void AllocateFromParams(const VideoParams &params,
							const OfxRectI &bounds,
							const OfxRectI &rod,
							bool clear = true);
	void EnsureAllocatedFromParams(const VideoParams &params,
								   const OfxRectI &bounds,
								   const OfxRectI &rod,
								   bool clear = false);
	void Allocate(int width,
				  int height,
				  core::PixelFormat format,
				  int channel_count,
				  bool premultiplied_alpha,
				  const OfxRectI &bounds,
				  const OfxRectI &rod,
				  bool clear = true);
	int row_bytes() const
	{
		return row_bytes_;
	}
protected:
	std::vector<uint8_t> image_;
	int width_;
	int height_;
	core::PixelFormat format_;
	bool premultiplied_alpha_;
	int channel_count_;
	int row_bytes_;
	OfxRectI bounds_;
	OfxRectI rod_;
};
}
}

#endif //OLIVE_EDITOR_PLUGIN_IMAGE_H
