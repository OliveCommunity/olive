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

#ifndef OAK_OLIVECLIP_H
#define OAK_OLIVECLIP_H

#include <map>
#include <memory>

#include "ofxCore.h"
#include "ofxhClip.h"

#include "common/videoparams.h"
#include "image.h"
#include "render/renderer.h"

namespace olive
{
namespace plugin
{

class OliveClipInstance : public OFX::Host::ImageEffect::ClipInstance {
public:
	OliveClipInstance(OFX::Host::ImageEffect::Instance *effect_instance,
					  OFX::Host::ImageEffect::ClipDescriptor &desc,
					  OakVideoParams params)
		: ClipInstance(effect_instance, desc)
		, params_(params)
		, name_(desc.getName())
	{
		if (params_.ctx) {
			params_.addref(params_.ctx);
		}
		default_region_of_definition_ = { 0, 0, 0, 0 };
	}

	~OliveClipInstance() override
	{
		if (params_.ctx) {
			oakcommon_videoparams_free(&params_);
		}
		for (auto &pair : images_) {
			delete pair.second;
		}
		for (auto &pair : input_textures_) {
			oakrender_display_texture_free(&pair.second);
		}
		for (auto &pair : output_textures_) {
			oakrender_display_texture_free(&pair.second);
		}
	}

	OFX::Host::ImageEffect::Image *getOutputImage(OfxTime time);

	const std::string &getUnmappedBitDepth() const override;
	const std::string &getUnmappedComponents() const override;
	const std::string &getPremult() const override;
	double getAspectRatio() const override;
	double getFrameRate() const override;
	void getFrameRange(double &start_frame, double &end_frame) const override;
	const std::string &getFieldOrder() const override;
	bool getConnected() const override;
	double getUnmappedFrameRate() const override;
	void getUnmappedFrameRange(double &start_frame,
							   double &end_frame) const override;
	bool getContinuousSamples() const override;
	OFX::Host::ImageEffect::Image *
	getImage(OfxTime time, const OfxRectD *optional_bounds) override;
	OfxRectD getRegionOfDefinition(OfxTime time) const override;

	void setRegionOfDefinition(OfxRectD region_of_definition, OfxTime time);
	void setDefaultRegionOfDefinition(OfxRectD region_of_definition);
	void setParams(const olive::VideoParams &params);
#ifdef OFX_SUPPORTS_OPENGLRENDER
	OFX::Host::ImageEffect::Texture *
	loadTexture(OfxTime time, const char *format,
				const OfxRectD *optional_bounds) override;
#endif

	void setInputTexture(OakRenderTexture texture, OfxTime time,
						 bool readback_cpu = true);
	void setOutputTexture(OakRenderTexture texture, OfxTime time);

	// Get the plugin-preferred VideoParams based on base class _pixelDepth/_components
	OakVideoParams getPluginPreferredParams() const;

	// Prune old entries from the images_ cache to prevent unbounded growth.
	// Output clip images are not pruned (they are typically single-frame).
	void prune_images_cache();

	static constexpr int k_max_input_image_cache = 8;

private:
	OakVideoParams params_;

	std::map<OfxTime, OfxRectD> region_of_definitions_;

	OfxRectD default_region_of_definition_;

	std::string name_;
	std::map<OfxTime, Image *> images_;
	std::map<OfxTime, OakRenderTexture> input_textures_;
	std::map<OfxTime, OakRenderTexture> output_textures_;
};
}
}

#endif //OAK_OLIVECLIP_H
