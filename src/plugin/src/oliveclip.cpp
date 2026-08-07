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

#include "oliveclip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include <ffmpeg_bridge/ffmpeg_bridge.h>

#include "avframeptr.h"
#include "common/ffmpegutils.h"
#include "ofxCore.h"
#include "ofxhClip.h"

namespace
{

// The bridge header only defines the little-endian pixel formats. FFmpeg
// numbers each big-endian variant immediately before its little-endian
// counterpart (BE == LE - 1), so derive the BE constants used below.
constexpr int fb_pix_fmt_gray_f32_be = fb_pix_fmt_gray_f32_le - 1;
constexpr int fb_pix_fmt_rgb_f32_be = fb_pix_fmt_rgb_f32_le - 1;
constexpr int fb_pix_fmt_rgba_f32_be = fb_pix_fmt_rgba_f32_le - 1;

const std::string k_bit_depth_none_str(kOfxBitDepthNone);
const std::string k_bit_depth_byte_str(kOfxBitDepthByte);
const std::string k_bit_depth_short_str(kOfxBitDepthShort);
const std::string k_bit_depth_half_str(kOfxBitDepthHalf);
const std::string k_bit_depth_float_str(kOfxBitDepthFloat);
const std::string k_image_component_none_str(kOfxImageComponentNone);
const std::string k_image_component_alpha_str(kOfxImageComponentAlpha);
const std::string k_image_component_rgb_str(kOfxImageComponentRGB);
const std::string k_image_component_rgba_str(kOfxImageComponentRGBA);
const std::string k_image_premult_str(kOfxImagePreMultiplied);
const std::string k_image_un_premult_str(kOfxImageUnPreMultiplied);
const std::string k_image_field_none_str(kOfxImageFieldNone);
const std::string k_image_field_upper_str(kOfxImageFieldUpper);
const std::string k_image_field_lower_str(kOfxImageFieldLower);

int params_width(OakVideoParams params)
{
	int v = 0;
	oakcommon_videoparams_get_width(params, &v);
	return v;
}

int params_height(OakVideoParams params)
{
	int v = 0;
	oakcommon_videoparams_get_height(params, &v);
	return v;
}

int params_format(OakVideoParams params)
{
	int v = OAKCOMMON_PIXEL_FORMAT_INVALID;
	oakcommon_videoparams_get_format(params, &v);
	return v;
}

int params_channels(OakVideoParams params)
{
	int v = 0;
	oakcommon_videoparams_get_channel_count(params, &v);
	return v;
}

double params_par(OakVideoParams params)
{
	int num = 1, den = 1;
	oakcommon_videoparams_get_pixel_aspect_ratio(params, &num, &den);
	if (den == 0) {
		return 1.0;
	}
	double par = double(num) / den;
	return par == 0.0 ? 1.0 : par;
}

double params_frame_rate(OakVideoParams params)
{
	int num = 0, den = 1;
	oakcommon_videoparams_get_frame_rate(params, &num, &den);
	return den ? double(num) / den : 0.0;
}

int64_t params_duration(OakVideoParams params)
{
	int64_t v = 0;
	oakcommon_videoparams_get_duration(params, &v);
	return v;
}

int64_t params_start_time(OakVideoParams params)
{
	int64_t v = 0;
	oakcommon_videoparams_get_start_time(params, &v);
	return v;
}

int params_interlacing(OakVideoParams params)
{
	int v = 0;
	oakcommon_videoparams_get_interlacing(params, &v);
	return v;
}

int bytes_per_component_of(int format)
{
	switch (format) {
	case OAKCOMMON_PIXEL_FORMAT_U8:
		return 1;
	case OAKCOMMON_PIXEL_FORMAT_U16:
		return 2;
	case OAKCOMMON_PIXEL_FORMAT_F16:
		return 2;
	case OAKCOMMON_PIXEL_FORMAT_F32:
		return 4;
	default:
		return 0;
	}
}

int bytes_to_pixels(int byte_linesize, OakVideoParams params)
{
	const int bytes_per_pixel =
		params_channels(params) * bytes_per_component_of(params_format(params));
	if (bytes_per_pixel <= 0) {
		return 0;
	}
	return byte_linesize / bytes_per_pixel;
}

int packed_float_channels(int fmt)
{
	switch (fmt) {
	case fb_pix_fmt_gray_f32_le:
	case fb_pix_fmt_gray_f32_be:
		return 1;
	case fb_pix_fmt_rgb_f32_le:
	case fb_pix_fmt_rgb_f32_be:
		return 3;
	case fb_pix_fmt_rgba_f32_le:
	case fb_pix_fmt_rgba_f32_be:
		return 4;
	default:
		return 0;
	}
}

bool packed_dst_info(int fmt, int *channels, int *bytes_per_component)
{
	switch (fmt) {
	case fb_pix_fmt_gra_y8:
		*channels = 1;
		*bytes_per_component = 1;
		return true;
	case fb_pix_fmt_rg_b24:
		*channels = 3;
		*bytes_per_component = 1;
		return true;
	case fb_pix_fmt_rgba:
		*channels = 4;
		*bytes_per_component = 1;
		return true;
	case fb_pix_fmt_gra_y16_le:
		*channels = 1;
		*bytes_per_component = 2;
		return true;
	case fb_pix_fmt_rg_b48_le:
		*channels = 3;
		*bytes_per_component = 2;
		return true;
	case fb_pix_fmt_rgb_a64_le:
		*channels = 4;
		*bytes_per_component = 2;
		return true;
	default:
		return false;
	}
}

olive::AVFramePtr readback_texture_to_frame(OakRenderTexture texture,
											OakVideoParams params)
{
	if (!texture.ctx || oakrender_display_texture_is_dummy(texture)) {
		return nullptr;
	}

	int pix_fmt = OAKCOMMON_E_INVALID;
	oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
		params_format(params), params_channels(params), &pix_fmt);
	if (pix_fmt == fb_pix_fmt_none) {
		return nullptr;
	}

	if (!fb_pix_fmt_is_planar(pix_fmt)) {
		olive::AVFramePtr frame = olive::create_av_frame_ptr();
		frame->set_format(pix_fmt);
		frame->set_width(params_width(params));
		frame->set_height(params_height(params));
		if (frame->get_buffer(0) < 0) {
			return nullptr;
		}
		const int linesize_pixels = bytes_to_pixels(frame->linesize(0), params);
		oakrender_display_texture_download(texture, frame->data(0),
										   linesize_pixels *
											   bytes_per_component_of(
												   params_format(params)));
		return frame;
	}

	// Planar formats read back through an RGBA intermediate and scale
	olive::AVFramePtr rgba_frame = olive::create_av_frame_ptr();
	rgba_frame->set_format(fb_pix_fmt_rgba);
	rgba_frame->set_width(params_width(params));
	rgba_frame->set_height(params_height(params));
	if (rgba_frame->get_buffer(0) < 0) {
		return nullptr;
	}

	const int rgba_linesize = rgba_frame->linesize(0);
	oakrender_display_texture_download(texture, rgba_frame->data(0),
									   rgba_linesize);

	olive::AVFramePtr dst = olive::create_av_frame_ptr();
	dst->set_format(pix_fmt);
	dst->set_width(params_width(params));
	dst->set_height(params_height(params));
	if (dst->get_buffer(0) < 0) {
		return rgba_frame;
	}

	FBScaler *scaler = fb_scaler_create(
		rgba_frame->width(), rgba_frame->height(), rgba_frame->format(),
		dst->width(), dst->height(), pix_fmt, FB_SCALER_POINT);
	if (!scaler) {
		return rgba_frame;
	}

	uint8_t *src_data[4];
	int src_linesize[4];
	uint8_t *dst_data[4];
	int dst_linesize[4];
	for (int i = 0; i < 4; ++i) {
		src_data[i] = rgba_frame->data(i);
		src_linesize[i] = rgba_frame->linesize(i);
		dst_data[i] = dst->data(i);
		dst_linesize[i] = dst->linesize(i);
	}
	fb_scaler_scale_slices(scaler, src_data, src_linesize,
						   rgba_frame->height(), dst_data, dst_linesize);
	fb_scaler_free(&scaler);
	return dst;
}

olive::AVFramePtr convert_packed_float_frame(olive::AVFramePtr src,
											 int dst_fmt)
{
	if (!src || !src->data(0)) {
		return nullptr;
	}
	const int src_channels = packed_float_channels(src->format());
	if (src_channels == 0) {
		return nullptr;
	}

	int dst_channels = 0;
	int bytes_per_component = 0;
	if (!packed_dst_info(dst_fmt, &dst_channels, &bytes_per_component)) {
		return nullptr;
	}

	olive::AVFramePtr dst = olive::create_av_frame_ptr();
	dst->set_format(dst_fmt);
	dst->set_width(src->width());
	dst->set_height(src->height());
	if (dst->get_buffer(0) < 0) {
		return nullptr;
	}

	auto clamp01 = [](float v) -> float { return std::clamp(v, 0.0f, 1.0f); };

	for (int y = 0; y < src->height(); ++y) {
		const float *src_row = reinterpret_cast<const float *>(
			src->data(0) + y * src->linesize(0));
		uint8_t *dst_row = dst->data(0) + y * dst->linesize(0);

		if (bytes_per_component == 2) {
			auto *dst_row_u16 = reinterpret_cast<uint16_t *>(dst_row);
			for (int x = 0; x < src->width(); ++x) {
				const float *pix = src_row + x * src_channels;
				float r = pix[0];
				float g = (src_channels > 1) ? pix[1] : r;
				float b = (src_channels > 2) ? pix[2] : r;
				float a = (src_channels > 3) ? pix[3] : 1.0f;
				if (dst_channels == 1) {
					float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
					dst_row_u16[x] = static_cast<uint16_t>(
						std::lround(clamp01(luma) * 65535.0f));
					continue;
				}
				dst_row_u16[x * dst_channels + 0] = static_cast<uint16_t>(
					std::lround(clamp01(r) * 65535.0f));
				dst_row_u16[x * dst_channels + 1] = static_cast<uint16_t>(
					std::lround(clamp01(g) * 65535.0f));
				dst_row_u16[x * dst_channels + 2] = static_cast<uint16_t>(
					std::lround(clamp01(b) * 65535.0f));
				if (dst_channels == 4) {
					dst_row_u16[x * dst_channels + 3] = static_cast<uint16_t>(
						std::lround(clamp01(a) * 65535.0f));
				}
			}
		} else {
			for (int x = 0; x < src->width(); ++x) {
				const float *pix = src_row + x * src_channels;
				float r = pix[0];
				float g = (src_channels > 1) ? pix[1] : r;
				float b = (src_channels > 2) ? pix[2] : r;
				float a = (src_channels > 3) ? pix[3] : 1.0f;
				if (dst_channels == 1) {
					float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
					dst_row[x] = static_cast<uint8_t>(
						std::lround(clamp01(luma) * 255.0f));
					continue;
				}
				dst_row[x * dst_channels + 0] = static_cast<uint8_t>(
					std::lround(clamp01(r) * 255.0f));
				dst_row[x * dst_channels + 1] = static_cast<uint8_t>(
					std::lround(clamp01(g) * 255.0f));
				dst_row[x * dst_channels + 2] = static_cast<uint8_t>(
					std::lround(clamp01(b) * 255.0f));
				if (dst_channels == 4) {
					dst_row[x * dst_channels + 3] = static_cast<uint8_t>(
						std::lround(clamp01(a) * 255.0f));
				}
			}
		}
	}

	return dst;
}

} // namespace

namespace olive
{
namespace plugin
{

const std::string &OliveClipInstance::getUnmappedBitDepth() const
{
	// Return the plugin's preferred pixel depth from base class
	// This is set during getClipPreferences action via setPixelDepth()
	const std::string &depth = getPixelDepth();
	if (!depth.empty() && depth != k_bit_depth_none_str) {
		return depth;
	}
	// Fallback to params_ if base class value is not set
	switch (params_format(params_)) {
	case OAKCOMMON_PIXEL_FORMAT_U8:
		return k_bit_depth_byte_str;
	case OAKCOMMON_PIXEL_FORMAT_U16:
		return k_bit_depth_short_str;
	case OAKCOMMON_PIXEL_FORMAT_F16:
		return k_bit_depth_half_str;
	case OAKCOMMON_PIXEL_FORMAT_F32:
		return k_bit_depth_float_str;
	default:
		return k_bit_depth_none_str;
	}
}

const std::string &OliveClipInstance::getUnmappedComponents() const
{
	// Return the plugin's preferred components from base class
	// This is set during getClipPreferences action via setComponents()
	const std::string &comp = getComponents();
	if (!comp.empty() && comp != k_image_component_none_str) {
		return comp;
	}
	// Fallback to params_ if base class value is not set
	switch (params_channels(params_)) {
	case 1:
		return k_image_component_alpha_str;
	case 3:
		return k_image_component_rgb_str;
	case 4:
		return k_image_component_rgba_str;
	default:
		return k_image_component_none_str;
	}
}

const std::string &OliveClipInstance::getPremult() const
{
	int premult = 0;
	oakcommon_videoparams_get_premultiplied_alpha(params_, &premult);
	return premult ? k_image_premult_str : k_image_un_premult_str;
}

double OliveClipInstance::getAspectRatio() const
{
	return params_par(params_);
}

double OliveClipInstance::getFrameRate() const
{
	return params_frame_rate(params_);
}

void OliveClipInstance::getFrameRange(double &start_frame,
									  double &end_frame) const
{
	start_frame = params_frame_rate(params_) * double(params_start_time(params_));
	end_frame = start_frame +
				params_frame_rate(params_) * double(params_duration(params_));
}

const std::string &OliveClipInstance::getFieldOrder() const
{
	switch (params_interlacing(params_)) {
	case OAKCOMMON_VIDEO_INTERLACED_TOP_FIRST:
		return k_image_field_upper_str;
	case OAKCOMMON_VIDEO_INTERLACED_BOTTOM_FIRST:
		return k_image_field_lower_str;
	default:
		return k_image_field_none_str;
	}
}

bool OliveClipInstance::getConnected() const
{
	if (name_ == kOfxImageEffectOutputClipName) {
		if (!output_textures_.empty()) {
			return true;
		}
		return !images_.empty();
	}
	if (!input_textures_.empty()) {
		return true;
	}
	return !images_.empty();
}

double OliveClipInstance::getUnmappedFrameRate() const
{
	return getFrameRate();
}

void OliveClipInstance::getUnmappedFrameRange(double &start_frame,
											  double &end_frame) const
{
	getFrameRange(start_frame, end_frame);
}

bool OliveClipInstance::getContinuousSamples() const
{
	return false;
}

OFX::Host::ImageEffect::Image *
OliveClipInstance::getImage(OfxTime time, const OfxRectD *optional_bounds)
{
	OfxRectD rod_d = getRegionOfDefinition(time);
	OfxRectI rod = { static_cast<int>(std::floor(rod_d.x1)),
					 static_cast<int>(std::floor(rod_d.y1)),
					 static_cast<int>(std::ceil(rod_d.x2)),
					 static_cast<int>(std::ceil(rod_d.y2)) };
	(void)optional_bounds;
	// Always return full-frame images to keep input data consistent.
	OfxRectI bounds = rod;

	const olive::VideoParams *params_native =
		oakcommon_videoparams_get_native(params_);

	if (name_ == "Output") {
		Image *image;
		auto it = images_.find(time);
		if (it == images_.end()) {
			// make a new ref counted image
			image = new Image(*this, *params_native, bounds, rod, true);
			images_.insert({ time, image });
		} else {
			image = it->second;
		}

		// add another reference to the member image for this fetch
		// as we have a ref count of 1 due to construction, this will
		// cause the output image never to delete by the plugin
		// when it releases the image
		image->addReference();

		image->ensure_allocated_from_params(*params_native, bounds, rod, true);

		// return it
		return image;
	} else {
		auto it = images_.find(time);
		if (it != images_.end()) {
			Image *image = it->second;
			image->ensure_allocated_from_params(*params_native, bounds, rod,
												false);
			image->addReference();
			return image;
		}

		// Fetch on demand for the input clip.
		// Use plugin-preferred params to ensure the image format matches
		// what the plugin expects (may differ from input texture format)
		OakVideoParams preferred = getPluginPreferredParams();
		if (params_format(preferred) == OAKCOMMON_PIXEL_FORMAT_INVALID) {
			if (preferred.ctx) {
				oakcommon_videoparams_free(&preferred);
			}
			preferred = params_;
			preferred.addref(preferred.ctx);
		}
		// Keep dimensions and other settings from params_
		oakcommon_videoparams_set_width(preferred, params_width(params_));
		oakcommon_videoparams_set_height(preferred, params_height(params_));
		{
			int num = 1, den = 1;
			oakcommon_videoparams_get_pixel_aspect_ratio(params_, &num, &den);
			oakcommon_videoparams_set_pixel_aspect_ratio(preferred, num, den);
		}

		// Guard against zero-size or invalid-format images that would
		// cause EXC_BAD_ACCESS when the plugin accesses pixel data.
		if (params_width(preferred) <= 0 || params_height(preferred) <= 0 ||
			params_format(preferred) == OAKCOMMON_PIXEL_FORMAT_INVALID ||
			params_channels(preferred) <= 0) {
			oakcommon_videoparams_free(&preferred);
			return nullptr;
		}

		const olive::VideoParams *preferred_native =
			oakcommon_videoparams_get_native(preferred);

		// Cache the on-demand image like the output path does, so repeated
		// fetches at the same time reuse it and getConnected() reflects it.
		// The extra reference keeps the cached image alive when the plugin
		// releases its own.
		prune_images_cache();
		Image *image = new Image(*this, *preferred_native, bounds, rod, true);
		images_.insert({ time, image });
		image->addReference();
		oakcommon_videoparams_free(&preferred);
		return image;
	}
}

OFX::Host::ImageEffect::Image *
OliveClipInstance::getOutputImage(OfxTime time)
{
	auto it = images_.find(time);
	if (it != images_.end()) {
		return it->second;
	}

	OfxRectD rod_d = getRegionOfDefinition(time);
	OfxRectI rod = { static_cast<int>(std::floor(rod_d.x1)),
					 static_cast<int>(std::floor(rod_d.y1)),
					 static_cast<int>(std::ceil(rod_d.x2)),
					 static_cast<int>(std::ceil(rod_d.y2)) };
	OfxRectI bounds = rod;

	// Use plugin-preferred params instead of params_ to ensure the image
	// is created with the format the plugin expects
	OakVideoParams preferred = getPluginPreferredParams();
	if (params_format(preferred) == OAKCOMMON_PIXEL_FORMAT_INVALID) {
		if (preferred.ctx) {
			oakcommon_videoparams_free(&preferred);
		}
		preferred = params_;
		preferred.addref(preferred.ctx);
	}
	// Keep the dimensions and other settings from params_
	oakcommon_videoparams_set_width(preferred, params_width(params_));
	oakcommon_videoparams_set_height(preferred, params_height(params_));
	{
		int num = 1, den = 1;
		oakcommon_videoparams_get_pixel_aspect_ratio(params_, &num, &den);
		oakcommon_videoparams_set_pixel_aspect_ratio(preferred, num, den);
	}

	const olive::VideoParams *preferred_native =
		oakcommon_videoparams_get_native(preferred);

	auto image = new Image(*this, *preferred_native, bounds, rod, true);
	images_.insert({ time, image });
	oakcommon_videoparams_free(&preferred);
	return image;
}

OakVideoParams OliveClipInstance::getPluginPreferredParams() const
{
	OakVideoParams result = {};
	{
		const olive::VideoParams *native =
			oakcommon_videoparams_get_native(params_);
		result = oakcommon_videoparams_init_from_native(native);
	}

	// Get format from base class _pixelDepth (set by getClipPreferences)
	const std::string &depth = getPixelDepth();
	if (!depth.empty()) {
		if (depth == kOfxBitDepthByte) {
			oakcommon_videoparams_set_format(result, OAKCOMMON_PIXEL_FORMAT_U8);
		} else if (depth == kOfxBitDepthShort) {
			oakcommon_videoparams_set_format(result,
											 OAKCOMMON_PIXEL_FORMAT_U16);
		} else if (depth == kOfxBitDepthHalf) {
			oakcommon_videoparams_set_format(result,
											 OAKCOMMON_PIXEL_FORMAT_F16);
		} else if (depth == kOfxBitDepthFloat) {
			oakcommon_videoparams_set_format(result,
											 OAKCOMMON_PIXEL_FORMAT_F32);
		}
	}

	// Get channel count from base class _components (set by getClipPreferences)
	const std::string &comp = getComponents();
	if (!comp.empty()) {
		if (comp == kOfxImageComponentRGBA) {
			oakcommon_videoparams_set_channel_count(result, 4);
		} else if (comp == kOfxImageComponentRGB) {
			oakcommon_videoparams_set_channel_count(result, 3);
		} else if (comp == kOfxImageComponentAlpha) {
			oakcommon_videoparams_set_channel_count(result, 1);
		}
	}

	return result;
}

OfxRectD OliveClipInstance::getRegionOfDefinition(OfxTime time) const
{
	auto it = region_of_definitions_.find(time);
	if (it != region_of_definitions_.end()) {
		return it->second;
	}
	OfxRectD region_of_definition;
	region_of_definition.x1 = region_of_definition.y1 = 0;
	double par = params_par(params_);
	region_of_definition.x2 = params_width(params_) * par;
	region_of_definition.y2 = params_height(params_);
	if (region_of_definition.x2 <= 0 || region_of_definition.y2 <= 0) {
		// The params provide no usable region; fall back to the default set
		// via setDefaultRegionOfDefinition().
		return default_region_of_definition_;
	}
	return region_of_definition;
}

void OliveClipInstance::setRegionOfDefinition(
	OfxRectD region_of_definition, OfxTime time)
{
	region_of_definitions_[time] = region_of_definition;
}

void OliveClipInstance::setDefaultRegionOfDefinition(
	OfxRectD region_of_definition)
{
	default_region_of_definition_ = region_of_definition;
}

void OliveClipInstance::prune_images_cache()
{
	// Do not prune output clip images; they may have external references
	// added by getImage()/addReference() and are typically single-frame.
	if (name_ == kOfxImageEffectOutputClipName) {
		return;
	}
	while (images_.size() > k_max_input_image_cache) {
		auto it = images_.begin();
		delete it->second;
		images_.erase(it);
	}
}

void OliveClipInstance::setParams(const olive::VideoParams &params)
{
	if (params_.ctx) {
		oakcommon_videoparams_free(&params_);
	}
	params_ = oakcommon_videoparams_init_from_native(&params);
	// Sync with OpenFX Host Support's _pixelDepth and _components
	setPixelDepth(getUnmappedBitDepth());
	setComponents(getUnmappedComponents());
}

void OliveClipInstance::setInputTexture(OakRenderTexture texture,
										OfxTime time, bool readback_cpu)
{
	if (!texture.ctx) {
		return;
	}

	// Preserve time-related properties from the host/project.
	// The frame rate of an OFX clip should reflect the project's frame rate,
	// not the individual input texture's frame rate.
	int rate_num = 0, rate_den = 1;
	oakcommon_videoparams_get_frame_rate(params_, &rate_num, &rate_den);
	int tb_num = 0, tb_den = 1;
	oakcommon_videoparams_get_time_base(params_, &tb_num, &tb_den);

	// Adopt the incoming texture's params
	oakrender_video_params texture_params = {};
	oakrender_display_texture_get_params(texture, &texture_params);
	oakcommon_videoparams_set_width(params_, texture_params.width);
	oakcommon_videoparams_set_height(params_, texture_params.height);
	oakcommon_videoparams_set_format(params_, texture_params.format);
	oakcommon_videoparams_set_interlacing(params_,
										  texture_params.interlacing);
	oakcommon_videoparams_set_color_range(params_,
										  texture_params.color_range);
	oakcommon_videoparams_set_divider(params_, texture_params.divider);
	oakcommon_videoparams_set_premultiplied_alpha(
		params_, texture_params.premultiplied_alpha);

	oakcommon_videoparams_set_frame_rate(params_, rate_num, rate_den);
	oakcommon_videoparams_set_time_base(params_, tb_num, tb_den);

	// Note: We do NOT call setPixelDepth/setComponents here because
	// those should be set by getClipPreferences to reflect the PLUGIN's
	// preferred format, not the input texture's format.
	texture.addref(texture.ctx);
	auto it = input_textures_.find(time);
	if (it != input_textures_.end()) {
		oakrender_display_texture_free(&it->second);
	}
	input_textures_[time] = texture;

	// In OpenGL render path, skip CPU readback entirely.
	// The plugin will fetch input via loadTexture() using GPU texture IDs.
	// If the plugin falls back to getImage(), it will be created on-demand
	// in getImage() with zero-initialized data.
	if (!readback_cpu) {
		return;
	}

	OakCodecFrame frame = {};
	oakrender_display_texture_get_frame(texture, &frame);
	if (!frame.ctx) {
		frame = oakrender_codec_frame_create();
		olive::AVFramePtr readback = readback_texture_to_frame(texture, params_);
		if (readback) {
			// Copy readback pixels into the codec frame
			if (oakrender_codec_frame_allocate(frame) == OAKRENDER_OK) {
				uint8_t *dst =
					static_cast<uint8_t *>(oakrender_codec_frame_data(frame));
				const uint8_t *src = readback->data(0);
				int src_linesize = readback->linesize(0);
				int dst_linesize = oakrender_codec_frame_linesize_bytes(frame);
				int copy_bytes = std::min(src_linesize, dst_linesize);
				for (int y = 0; y < readback->height(); y++) {
					memcpy(dst + size_t(y) * dst_linesize,
						   src + size_t(y) * src_linesize,
						   size_t(copy_bytes));
				}
			}
		}
	}

	int expected_fmt = OAKCOMMON_E_INVALID;
	oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
		params_format(params_), params_channels(params_), &expected_fmt);
	if (expected_fmt == fb_pix_fmt_none) {
		if (frame.ctx) {
			oakrender_codec_frame_free(&frame);
		}
		return;
	}
	OfxRectI bounds = { 0, 0, params_width(params_), params_height(params_) };
	OfxRectD rod_d = getRegionOfDefinition(time);
	OfxRectI region_of_definition = { static_cast<int>(std::floor(rod_d.x1)),
									  static_cast<int>(std::floor(rod_d.y1)),
									  static_cast<int>(std::ceil(rod_d.x2)),
									  static_cast<int>(std::ceil(rod_d.y2)) };

	const olive::VideoParams *params_native =
		oakcommon_videoparams_get_native(params_);

	Image *image;
	auto img_it = images_.find(time);
	if (img_it != images_.end()) {
		image = img_it->second;
		image->ensure_allocated_from_params(*params_native, bounds,
											region_of_definition, false);
	} else {
		prune_images_cache();
		image = new Image(*this, *params_native, bounds, region_of_definition,
						  false);
		image->ensure_allocated_from_params(*params_native, bounds,
											region_of_definition, false);
		images_.insert({ time, image });
	}

	uint8_t *dst = static_cast<uint8_t *>(image->data());
	if (!dst) {
		if (frame.ctx) {
			oakrender_codec_frame_free(&frame);
		}
		return;
	}

	if (!frame.ctx || !oakrender_codec_frame_data(frame)) {
		std::memset(dst, 0, image->row_bytes() * image->height());
		if (frame.ctx) {
			oakrender_codec_frame_free(&frame);
		}
		return;
	}

	AVFramePtr src_frame;
	bool own_src = false;
	if (oakrender_codec_frame_fb_format(frame) >= 0) {
		// Wraps an AVFramePtr: access planes directly
		// (fb format/width/height available through accessors)
	}
	int frame_fmt = oakrender_codec_frame_fb_format(frame);
	int frame_width = oakrender_codec_frame_width(frame);
	int frame_height = oakrender_codec_frame_height(frame);

	if (frame_fmt != expected_fmt || frame_width != params_width(params_) ||
		frame_height != params_height(params_)) {
		if (packed_float_channels(frame_fmt) > 0) {
			// Packed float -> packed int conversion
			AVFramePtr converted_input = create_av_frame_ptr();
			converted_input->set_format(frame_fmt);
			converted_input->set_width(frame_width);
			converted_input->set_height(frame_height);
			if (converted_input->get_buffer(0) >= 0) {
				uint8_t *cdst = converted_input->data(0);
				const uint8_t *csrc = static_cast<const uint8_t *>(
					oakrender_codec_frame_const_data(frame));
				int csrc_ls = oakrender_codec_frame_linesize_bytes(frame);
				int cdst_ls = converted_input->linesize(0);
				int ccopy = std::min(csrc_ls, cdst_ls);
				for (int y = 0; y < frame_height; y++) {
					memcpy(cdst + size_t(y) * cdst_ls,
						   csrc + size_t(y) * csrc_ls, size_t(ccopy));
				}
				AVFramePtr converted =
					convert_packed_float_frame(converted_input, expected_fmt);
				if (converted) {
					src_frame = converted;
					own_src = true;
				}
			}
		}
		if (!own_src) {
			AVFramePtr converted = create_av_frame_ptr();
			converted->set_format(expected_fmt);
			converted->set_width(params_width(params_));
			converted->set_height(params_height(params_));
			if (converted->get_buffer(0) < 0) {
				oakrender_codec_frame_free(&frame);
				return;
			}

			AVFramePtr input = create_av_frame_ptr();
			input->set_format(frame_fmt);
			input->set_width(frame_width);
			input->set_height(frame_height);
			if (input->get_buffer(0) < 0) {
				oakrender_codec_frame_free(&frame);
				return;
			}
			uint8_t *idst = input->data(0);
			const uint8_t *isrc = static_cast<const uint8_t *>(
				oakrender_codec_frame_const_data(frame));
			int isrc_ls = oakrender_codec_frame_linesize_bytes(frame);
			int idst_ls = input->linesize(0);
			int icopy = std::min(isrc_ls, idst_ls);
			for (int y = 0; y < frame_height; y++) {
				memcpy(idst + size_t(y) * idst_ls, isrc + size_t(y) * isrc_ls,
					   size_t(icopy));
			}

			FBScaler *scaler = fb_scaler_create(
				input->width(), input->height(), input->format(),
				converted->width(), converted->height(), converted->format(),
				FB_SCALER_POINT);
			if (!scaler) {
				oakrender_codec_frame_free(&frame);
				return;
			}

			uint8_t *src_data[4];
			int src_linesize[4];
			uint8_t *dst_data[4];
			int dst_linesize[4];
			for (int i = 0; i < 4; ++i) {
				src_data[i] = input->data(i);
				src_linesize[i] = input->linesize(i);
				dst_data[i] = converted->data(i);
				dst_linesize[i] = converted->linesize(i);
			}
			fb_scaler_scale_slices(scaler, src_data, src_linesize,
								   input->height(), dst_data, dst_linesize);
			fb_scaler_free(&scaler);

			src_frame = converted;
			own_src = true;
		}
	}

	const uint8_t *src;
	int src_row_bytes;
	if (own_src) {
		src = src_frame->data(0);
		src_row_bytes = src_frame->linesize(0);
	} else {
		src = static_cast<const uint8_t *>(oakrender_codec_frame_const_data(frame));
		src_row_bytes = oakrender_codec_frame_linesize_bytes(frame);
	}

	int bytes_per_component =
		bytes_per_component_of(params_format(params_));
	int bytes_per_row = params_width(params_) * params_channels(params_) *
						bytes_per_component;
	int dst_row_bytes = image->row_bytes();
	int copy_bytes =
		std::min(bytes_per_row, std::min(src_row_bytes, dst_row_bytes));
	int copy_height =
		std::min(image->height(), own_src ? src_frame->height() : frame_height);

	// Detect NaN/Inf in float input data before passing to CImg.
	if (params_format(params_) == OAKCOMMON_PIXEL_FORMAT_F32) {
		const float *src_f = reinterpret_cast<const float *>(src);
		int row_floats = src_row_bytes / static_cast<int>(sizeof(float));
		bool has_nan = false;
		for (int y = 0; y < params_height(params_) && !has_nan; ++y) {
			for (int x = 0;
				 x < params_width(params_) * params_channels(params_); ++x) {
				float v = src_f[y * row_floats + x];
				if (std::isnan(v) || std::isinf(v)) {
					has_nan = true;
					break;
				}
			}
		}
		if (has_nan) {
			fprintf(stderr,
					"[PLUGIN] NaN/Inf detected in input frame, filling with "
					"black to avoid CImg crash\n");
			std::memset(dst, 0, image->row_bytes() * image->height());
			oakrender_codec_frame_free(&frame);
			return;
		}
	}

	if (params_format(params_) == OAKCOMMON_PIXEL_FORMAT_F32) {
		const float *src_f = reinterpret_cast<const float *>(src);
		float *dst_f = reinterpret_cast<float *>(dst);
		int src_stride = src_row_bytes / static_cast<int>(sizeof(float));
		int dst_stride = dst_row_bytes / static_cast<int>(sizeof(float));
		int floats_per_row = copy_bytes / static_cast<int>(sizeof(float));
		for (int y = 0; y < copy_height; ++y) {
			for (int i = 0; i < floats_per_row; ++i) {
				float v = src_f[y * src_stride + i];
				if (std::isnan(v) || std::isinf(v)) {
					v = 0.0f;
				}
				dst_f[y * dst_stride + i] = v;
			}
		}
	} else if (dst_row_bytes == src_row_bytes && src_row_bytes == copy_bytes) {
		std::memcpy(dst, src, size_t(copy_bytes) * size_t(copy_height));
	} else {
		for (int y = 0; y < copy_height; ++y) {
			std::memcpy(dst + size_t(y) * dst_row_bytes,
						src + size_t(y) * src_row_bytes, size_t(copy_bytes));
		}
	}

	oakrender_codec_frame_free(&frame);
}

void OliveClipInstance::setOutputTexture(OakRenderTexture texture,
										 OfxTime time)
{
	if (!texture.ctx) {
		return;
	}
	texture.addref(texture.ctx);
	auto it = output_textures_.find(time);
	if (it != output_textures_.end()) {
		oakrender_display_texture_free(&it->second);
	}
	output_textures_[time] = texture;
}

#ifdef OFX_SUPPORTS_OPENGLRENDER
OFX::Host::ImageEffect::Texture *
OliveClipInstance::loadTexture(OfxTime time, const char *format,
							   const OfxRectD *optional_bounds)
{
	(void)format;

	OakRenderTexture gl_texture = {};
	if (isOutput()) {
		auto it = output_textures_.find(time);
		if (it != output_textures_.end()) {
			gl_texture = it->second;
		}
	} else {
		auto it = input_textures_.find(time);
		if (it != input_textures_.end()) {
			gl_texture = it->second;
		}
	}

	if (!gl_texture.ctx || oakrender_display_texture_is_dummy(gl_texture)) {
		return nullptr;
	}

	OfxRectD rod_d = getRegionOfDefinition(time);
	OfxRectI rod = { static_cast<int>(std::floor(rod_d.x1)),
					 static_cast<int>(std::floor(rod_d.y1)),
					 static_cast<int>(std::ceil(rod_d.x2)),
					 static_cast<int>(std::ceil(rod_d.y2)) };
	OfxRectI bounds = rod;
	if (optional_bounds) {
		bounds.x1 = static_cast<int>(std::floor(optional_bounds->x1));
		bounds.y1 = static_cast<int>(std::floor(optional_bounds->y1));
		bounds.x2 = static_cast<int>(std::ceil(optional_bounds->x2));
		bounds.y2 = static_cast<int>(std::ceil(optional_bounds->y2));
	}
	bounds.x1 = std::max(bounds.x1, rod.x1);
	bounds.y1 = std::max(bounds.y1, rod.y1);
	bounds.x2 = std::min(bounds.x2, rod.x2);
	bounds.y2 = std::min(bounds.y2, rod.y2);

	const int bytes_per_row = params_width(params_) * params_channels(params_) *
							  bytes_per_component_of(params_format(params_));
	const std::string &field = getFieldOrder();
	const std::string unique_id =
		std::to_string(reinterpret_cast<uintptr_t>(gl_texture.ctx)) + "_" +
		std::to_string(static_cast<long long>(time));

	const int texture_id = oakrender_display_texture_id(gl_texture);
	if (texture_id == 0) {
		return nullptr;
	}
	OFX::Host::ImageEffect::Texture *texture =
		new OFX::Host::ImageEffect::Texture(*this, 1.0, 1.0, texture_id,
											GL_TEXTURE_2D, bounds, rod,
											bytes_per_row, field, unique_id);
	texture->addReference();
	return texture;
}
#endif

}
}
