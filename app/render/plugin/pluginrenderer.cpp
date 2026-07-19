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

//
// Created by mikesolar on 25-10-19.
//
#include "ofxCore.h"
#include "ofxhPropertySuite.h"
#include "olive/core/render/pixelformat.h"
#include "render/texture.h"
#include "render/opengl/openglrenderer.h"
#include "node/value.h"
#include "render/videoparams.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <QDebug>
#include <QMessageBox>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include "pluginrenderer.h"
#include "core.h"
#include "undo/undostack.h"
#include "pluginSupport/oliveclip.h"
#include "pluginSupport/oliveplugininstance.h"
#include "common/ffmpegutils.h"
#include "ofxhParam.h"
#include "ofxImageEffect.h"
#include "ofxhUtilities.h"
#include "ofxGPURender.h"
#include "olive/core/util/color.h"
#include <ffmpeg_bridge/ffmpeg_bridge.h>

// The bridge header only defines the little-endian pixel formats. FFmpeg
// numbers each big-endian variant immediately before its little-endian
// counterpart (BE == LE - 1), so derive the BE constants used below.
constexpr int fb_pix_fmt_gra_y16_be = fb_pix_fmt_gra_y16_le - 1;
constexpr int fb_pix_fmt_rg_b48_be = fb_pix_fmt_rg_b48_le - 1;
constexpr int fb_pix_fmt_rgb_a64_be = fb_pix_fmt_rgb_a64_le - 1;
constexpr int fb_pix_fmt_gray_f32_be = fb_pix_fmt_gray_f32_le - 1;
constexpr int fb_pix_fmt_rgb_f32_be = fb_pix_fmt_rgb_f32_le - 1;
constexpr int fb_pix_fmt_rgba_f32_be = fb_pix_fmt_rgba_f32_le - 1;

// 作用：从 OFX Image 属性推导 FFmpeg 像素格式，并返回每像素字节数。
// Purpose: Infer FFmpeg pixel format from OFX image properties and return bytes-per-pixel.
static int
get_ofx_av_pixel_format(const OFX::Host::ImageEffect::Image &image,
					int *bytes_per_pixel)
{
	const std::string &depth =
		image.getStringProperty(kOfxImageEffectPropPixelDepth);
	const std::string &components =
		image.getStringProperty(kOfxImageEffectPropComponents);

	olive::core::PixelFormat pixel_format = olive::core::PixelFormat::invalid;
	if (depth == kOfxBitDepthByte) {
		pixel_format = olive::core::PixelFormat::u8;
	} else if (depth == kOfxBitDepthShort) {
		pixel_format = olive::core::PixelFormat::u16;
	} else if (depth == kOfxBitDepthHalf) {
		pixel_format = olive::core::PixelFormat::f16;
	} else if (depth == kOfxBitDepthFloat) {
		pixel_format = olive::core::PixelFormat::f32;
	}

	int channel_count = 0;
	if (components == kOfxImageComponentRGBA) {
		channel_count = 4;
	} else if (components == kOfxImageComponentRGB) {
		channel_count = 3;
	} else if (components == kOfxImageComponentAlpha) {
		channel_count = 1;
	}

	int pix_fmt =
		olive::FFmpegUtils::get_f_fmpeg_pixel_format(pixel_format, channel_count);
	if (pix_fmt == fb_pix_fmt_none && channel_count == 1) {
		if (pixel_format == olive::core::PixelFormat::u8) {
			pix_fmt = fb_pix_fmt_gra_y8;
		} else if (pixel_format == olive::core::PixelFormat::u16) {
			pix_fmt = fb_pix_fmt_gra_y16_le;
		} else if (pixel_format == olive::core::PixelFormat::f16) {
			pix_fmt = fb_pix_fmt_gray_f16_le;
		} else if (pixel_format == olive::core::PixelFormat::f32) {
			pix_fmt = fb_pix_fmt_gray_f32_le;
		}
	}

	if (pix_fmt == fb_pix_fmt_none) {
		return fb_pix_fmt_none;
	}

	// fb_pix_fmt_bits_per_pixel returns 0 for unknown formats, which also
	// covers the old "av_pix_fmt_desc_get returned nullptr" case.
	int bits_per_pixel = fb_pix_fmt_bits_per_pixel(pix_fmt);
	if (bits_per_pixel <= 0 || bits_per_pixel % 8 != 0) {
		return fb_pix_fmt_none;
	}

	*bytes_per_pixel = bits_per_pixel / 8;
	return pix_fmt;
}

// 作用：为插件实例注入当前帧的参数值，避免依赖节点实时回读。
static void apply_param_overrides(OFX::Host::ImageEffect::Instance &instance,
								const olive::NodeValueRow &values, OfxTime time)
{
	const auto &params = instance.getParams();
	for (const auto &entry : params) {
		if (!entry.second) {
			continue;
		}
		const QString key = QString::fromStdString(entry.first);
		if (!values.contains(key)) {
			continue;
		}
		const olive::NodeValue &value = values.value(key);
		if (value.type() == olive::NodeValue::k_none ||
			value.type() == olive::NodeValue::k_texture ||
			value.type() == olive::NodeValue::k_samples) {
			continue;
		}
		const std::string &type = entry.second->getType();

		if (type == kOfxParamTypeInteger) {
			if (auto *param = dynamic_cast<OFX::Host::Param::IntegerInstance *>(
					entry.second)) {
				param->set(time, value.data().toInt());
			}
			continue;
		}
		if (type == kOfxParamTypeDouble) {
			if (auto *param = dynamic_cast<OFX::Host::Param::DoubleInstance *>(
					entry.second)) {
				double v = value.data().toDouble();
				if (std::isnan(v) || std::isinf(v)) {
					qWarning() << "[PLUGIN] NaN/Inf in double param" << key
							   << "replacing with default";
					auto *default_prop =
						entry.second->getProperties().fetchDoubleProperty(
							kOfxParamPropDefault);
					v = default_prop ? default_prop->getValue() : 0.0;
				}
				auto *min_prop =
					entry.second->getProperties().fetchDoubleProperty(
						kOfxParamPropMin);
				if (min_prop && v < min_prop->getValue()) {
					v = min_prop->getValue();
				}
				auto *max_prop =
					entry.second->getProperties().fetchDoubleProperty(
						kOfxParamPropMax);
				if (max_prop && v > max_prop->getValue()) {
					v = max_prop->getValue();
				}
				param->set(time, v);
			}
			continue;
		}
		if (type == kOfxParamTypeBoolean) {
			if (auto *param = dynamic_cast<OFX::Host::Param::BooleanInstance *>(
					entry.second)) {
				param->set(time, value.data().toBool());
			}
			continue;
		}
		if (type == kOfxParamTypeChoice) {
			if (auto *param = dynamic_cast<OFX::Host::Param::ChoiceInstance *>(
					entry.second)) {
				param->set(time, value.data().toInt());
			}
			continue;
		}
		if (type == kOfxParamTypeString || type == kOfxParamTypeCustom ||
			type == kOfxParamTypeBytes || type == kOfxParamTypeStrChoice) {
			if (auto *param = dynamic_cast<OFX::Host::Param::StringInstance *>(
					entry.second)) {
				const QByteArray utf8 = value.data().toString().toUtf8();
				param->set(time, utf8.constData());
			}
			continue;
		}
		if (type == kOfxParamTypeRGBA) {
			if (auto *param = dynamic_cast<OFX::Host::Param::RGBAInstance *>(
					entry.second)) {
				if (value.data().canConvert<olive::core::Color>()) {
					const auto c = value.data().value<olive::core::Color>();
					param->set(time, c.red(), c.green(), c.blue(), c.alpha());
				} else if (value.data().canConvert<QVector4D>()) {
					const QVector4D v = value.data().value<QVector4D>();
					param->set(time, v.x(), v.y(), v.z(), v.w());
				} else if (value.data().canConvert<QVector3D>()) {
					const QVector3D v = value.data().value<QVector3D>();
					param->set(time, v.x(), v.y(), v.z(), 1.0);
				}
			}
			continue;
		}
		if (type == kOfxParamTypeRGB) {
			if (auto *param = dynamic_cast<OFX::Host::Param::RGBInstance *>(
					entry.second)) {
				if (value.data().canConvert<olive::core::Color>()) {
					const auto c = value.data().value<olive::core::Color>();
					param->set(time, c.red(), c.green(), c.blue());
				} else if (value.data().canConvert<QVector4D>()) {
					const QVector4D v = value.data().value<QVector4D>();
					param->set(time, v.x(), v.y(), v.z());
				} else if (value.data().canConvert<QVector3D>()) {
					const QVector3D v = value.data().value<QVector3D>();
					param->set(time, v.x(), v.y(), v.z());
				}
			}
			continue;
		}
		if (type == kOfxParamTypeDouble2D) {
			if (auto *param =
					dynamic_cast<OFX::Host::Param::Double2DInstance *>(
						entry.second)) {
				if (value.data().canConvert<QVector2D>()) {
					const QVector2D v = value.data().value<QVector2D>();
					param->set(time, v.x(), v.y());
				}
			}
			continue;
		}
		if (type == kOfxParamTypeInteger2D) {
			if (auto *param =
					dynamic_cast<OFX::Host::Param::Integer2DInstance *>(
						entry.second)) {
				if (value.data().canConvert<QVector2D>()) {
					const QVector2D v = value.data().value<QVector2D>();
					param->set(time, static_cast<int>(v.x()),
							   static_cast<int>(v.y()));
				}
			}
			continue;
		}
		if (type == kOfxParamTypeDouble3D) {
			if (auto *param =
					dynamic_cast<OFX::Host::Param::Double3DInstance *>(
						entry.second)) {
				if (value.data().canConvert<QVector3D>()) {
					const QVector3D v = value.data().value<QVector3D>();
					param->set(time, v.x(), v.y(), v.z());
				}
			}
			continue;
		}
		if (type == kOfxParamTypeInteger3D) {
			if (auto *param =
					dynamic_cast<OFX::Host::Param::Integer3DInstance *>(
						entry.second)) {
				if (value.data().canConvert<QVector3D>()) {
					const QVector3D v = value.data().value<QVector3D>();
					param->set(time, static_cast<int>(v.x()),
							   static_cast<int>(v.y()),
							   static_cast<int>(v.z()));
				}
			}
			continue;
		}
	}
}

static int
get_destination_av_pixel_format(const olive::VideoParams &params);

// 作用：读取 clip 偏好（像素深度与分量）并更新 VideoParams。
// Purpose: Apply clip preferences (depth/components) into VideoParams.
static bool
apply_clip_preferences_to_params(const OFX::Host::ImageEffect::ClipInstance &clip,
							 olive::VideoParams *params)
{
	if (!params) {
		return false;
	}

	olive::core::PixelFormat format = olive::core::PixelFormat::invalid;
	const std::string &depth = clip.getPixelDepth();
	if (depth == kOfxBitDepthByte) {
		format = olive::core::PixelFormat::u8;
	} else if (depth == kOfxBitDepthShort) {
		format = olive::core::PixelFormat::u16;
	} else if (depth == kOfxBitDepthHalf) {
		format = olive::core::PixelFormat::f16;
	} else if (depth == kOfxBitDepthFloat) {
		format = olive::core::PixelFormat::f32;
	}

	int channels = 0;
	const std::string &components = clip.getComponents();
	if (components == kOfxImageComponentRGBA) {
		channels = 4;
	} else if (components == kOfxImageComponentRGB) {
		channels = 3;
	} else if (components == kOfxImageComponentAlpha) {
		channels = 1;
	}

	if (format == olive::core::PixelFormat::invalid || channels == 0) {
		return false;
	}

	params->set_format(format);
	params->set_channel_count(channels);
	return true;
}

// 作用：将 OFX bit depth 字符串映射为内部 PixelFormat。
// Purpose: Map OFX bit depth string to internal PixelFormat.
static olive::core::PixelFormat
pixel_format_from_ofx_depth(const std::string &depth)
{
	if (depth == kOfxBitDepthByte) {
		return olive::core::PixelFormat::u8;
	}
	if (depth == kOfxBitDepthShort) {
		return olive::core::PixelFormat::u16;
	}
	if (depth == kOfxBitDepthHalf) {
		return olive::core::PixelFormat::f16;
	}
	if (depth == kOfxBitDepthFloat) {
		return olive::core::PixelFormat::f32;
	}
	return olive::core::PixelFormat::invalid;
}

// 作用：将内部 PixelFormat 转为 OFX bit depth 字符串。
// Purpose: Map internal PixelFormat to OFX bit depth string.
static const char *ofx_depth_from_pixel_format(olive::core::PixelFormat format)
{
	switch (format) {
	case olive::core::PixelFormat::u8:
		return kOfxBitDepthByte;
	case olive::core::PixelFormat::u16:
		return kOfxBitDepthShort;
	case olive::core::PixelFormat::f16:
		return kOfxBitDepthHalf;
	case olive::core::PixelFormat::f32:
		return kOfxBitDepthFloat;
	case olive::core::PixelFormat::invalid:
	case olive::core::PixelFormat::count:
		break;
	}
	return kOfxBitDepthNone;
}

// 作用：将 OFX components 字符串映射为通道数。
// Purpose: Map OFX components string to channel count.
static int channel_count_from_ofx_component(const std::string &components)
{
	if (components == kOfxImageComponentRGBA) {
		return 4;
	}
	if (components == kOfxImageComponentRGB) {
		return 3;
	}
	if (components == kOfxImageComponentAlpha) {
		return 1;
	}
	return 0;
}

// 作用：将通道数映射为 OFX components 字符串。
// Purpose: Map channel count to OFX components string.
static const char *ofx_components_from_channels(int channel_count)
{
	switch (channel_count) {
	case 1:
		return kOfxImageComponentAlpha;
	case 3:
		return kOfxImageComponentRGB;
	case 4:
		return kOfxImageComponentRGBA;
	default:
		break;
	}
	return kOfxImageComponentNone;
}

// 作用：判断插件是否支持指定像素深度。
// Purpose: Check whether effect supports a given pixel depth.
static bool
effect_supports_pixel_depth(const OFX::Host::ImageEffect::Instance &instance,
						 const std::string &depth)
{
	const auto &effect_props = instance.getDescriptor().getProps();
	const int depth_count =
		effect_props.getDimension(kOfxImageEffectPropSupportedPixelDepths);
	for (int i = 0; i < depth_count; ++i) {
		if (effect_props.getStringProperty(
				kOfxImageEffectPropSupportedPixelDepths, i) == depth) {
			return true;
		}
	}
	return false;
}

// 作用：判断 clip 是否支持指定组件格式。
// Purpose: Check whether clip supports a given components string.
static bool
clip_supports_components(const OFX::Host::ImageEffect::ClipInstance &clip,
					   const std::string &components)
{
	const auto &supported_components = clip.getSupportedComponents();
	for (const auto &comp : supported_components) {
		if (comp == components) {
			return true;
		}
	}
	return false;
}

// 作用：估算从源参数到目标参数的转换代价，用于排序选择。
// Purpose: Estimate conversion cost from source to target params for ranking.
static int conversion_cost(const olive::VideoParams &src,
						  const olive::VideoParams &dst)
{
	const int src_bpp = src.channel_count() * src.format().byte_count();
	const int dst_bpp = dst.channel_count() * dst.format().byte_count();
	int cost = std::abs(dst_bpp - src_bpp);
	if (src.format() != dst.format()) {
		cost += 4;
	}
	if (src.channel_count() != dst.channel_count()) {
		cost += 2;
	}
	return cost;
}

// 作用：判断目标参数能否转换为可用的 AVPixelFormat。
// Purpose: Check if params map to a valid AVPixelFormat.
static bool params_convertible(const olive::VideoParams &params)
{
	return get_destination_av_pixel_format(params) != fb_pix_fmt_none;
}

// 作用：在 clip 偏好无效时，选择一个插件支持的输出格式。
// Purpose: Pick a supported output format when clip preferences are invalid.
static void
choose_supported_output_params(const OFX::Host::ImageEffect::Instance &instance,
							const OFX::Host::ImageEffect::ClipInstance &clip,
							const olive::VideoParams &preferred,
							olive::VideoParams *out)
{
	if (!out) {
		return;
	}

	*out = preferred;

	const char *preferred_components =
		ofx_components_from_channels(preferred.channel_count());
	if (std::strcmp(preferred_components, kOfxImageComponentNone) != 0 &&
		clip_supports_components(clip, preferred_components)) {
		out->set_channel_count(preferred.channel_count());
	} else if (clip_supports_components(clip, kOfxImageComponentRGBA)) {
		out->set_channel_count(4);
	} else if (clip_supports_components(clip, kOfxImageComponentRGB)) {
		out->set_channel_count(3);
	} else if (clip_supports_components(clip, kOfxImageComponentAlpha)) {
		out->set_channel_count(1);
	}

	const olive::core::PixelFormat preferred_format = preferred.format();
	const std::array<olive::core::PixelFormat, 5> candidates = {
		preferred_format,
		olive::core::PixelFormat::f16,
		olive::core::PixelFormat::f32,
		olive::core::PixelFormat::u16,
		olive::core::PixelFormat::u8,
	};
	for (const auto &candidate : candidates) {
		if (candidate == olive::core::PixelFormat::invalid) {
			continue;
		}
		if (!effect_supports_pixel_depth(instance,
									  ofx_depth_from_pixel_format(candidate))) {
			continue;
		}
		olive::VideoParams test_params = *out;
		test_params.set_format(candidate);
		if (!params_convertible(test_params)) {
			continue;
		}
		out->set_format(candidate);
		return;
	}
}

// Forward declarations for functions defined later in this file.
static olive::AVFramePtr
convert_frame_if_needed(olive::AVFramePtr src,
					 const olive::VideoParams &dst_params,
					 olive::Renderer *renderer);
static olive::TexturePtr
convert_texture_for_params(olive::TexturePtr src,
						const olive::VideoParams &dst_params);

// 作用：根据插件能力与偏好选择输入格式并执行转换。
// Purpose: Select a supported input format and convert texture for the clip.
static olive::TexturePtr
convert_texture_for_clip(const OFX::Host::ImageEffect::Instance &instance,
					  const OFX::Host::ImageEffect::ClipInstance &clip,
					  olive::TexturePtr src,
					  const olive::VideoParams &preferred_params,
					  bool force_preferred, olive::VideoParams *out_params)
{
	if (!src || !out_params) {
		return nullptr;
	}

	const olive::VideoParams &src_params = src->params();
	auto add_candidate = [](std::vector<olive::VideoParams> &list,
							const olive::VideoParams &params) {
		for (const auto &existing : list) {
			if (existing.format() == params.format() &&
				existing.channel_count() == params.channel_count()) {
				return;
			}
		}
		list.push_back(params);
	};

	std::vector<int> channel_candidates;
	const auto &supported_components = clip.getSupportedComponents();
	for (const auto &comp : supported_components) {
		int channels = channel_count_from_ofx_component(comp);
		if (channels > 0 &&
			std::find(channel_candidates.begin(), channel_candidates.end(),
					  channels) == channel_candidates.end()) {
			channel_candidates.push_back(channels);
		}
	}
	if (channel_candidates.empty() && preferred_params.channel_count() > 0) {
		channel_candidates.push_back(preferred_params.channel_count());
	}

	std::vector<olive::core::PixelFormat> format_candidates;
	const auto &effect_props = instance.getDescriptor().getProps();
	const int depth_count =
		effect_props.getDimension(kOfxImageEffectPropSupportedPixelDepths);
	for (int i = 0; i < depth_count; ++i) {
		olive::core::PixelFormat fmt =
			pixel_format_from_ofx_depth(effect_props.getStringProperty(
				kOfxImageEffectPropSupportedPixelDepths, i));
		if (fmt != olive::core::PixelFormat::invalid &&
			std::find(format_candidates.begin(), format_candidates.end(),
					  fmt) == format_candidates.end()) {
			format_candidates.push_back(fmt);
		}
	}
	if (format_candidates.empty() &&
		preferred_params.format() != olive::core::PixelFormat::invalid) {
		format_candidates.push_back(preferred_params.format());
	}

	std::vector<olive::VideoParams> candidates;
	add_candidate(candidates, preferred_params);

	const bool prefer_rgba8 =
		(preferred_params.format() == olive::core::PixelFormat::u8 ||
		 preferred_params.format() == olive::core::PixelFormat::invalid) &&
		clip_supports_components(clip, kOfxImageComponentRGBA) &&
		effect_supports_pixel_depth(instance, kOfxBitDepthByte);
	if (prefer_rgba8) {
		olive::VideoParams rgba_candidate = src_params;
		rgba_candidate.set_format(olive::core::PixelFormat::u8);
		rgba_candidate.set_channel_count(4);
		if (params_convertible(rgba_candidate)) {
			add_candidate(candidates, rgba_candidate);
		}
	}

	for (olive::core::PixelFormat fmt : format_candidates) {
		for (int channels : channel_candidates) {
			if (fmt == olive::core::PixelFormat::invalid || channels <= 0) {
				continue;
			}
			olive::VideoParams candidate = src_params;
			candidate.set_format(fmt);
			candidate.set_channel_count(channels);
			if (!params_convertible(candidate)) {
				continue;
			}
			add_candidate(candidates, candidate);
		}
	}

	if (candidates.empty()) {
		return nullptr;
	}

	std::stable_sort(
		candidates.begin(), candidates.end(),
		[&src_params, &preferred_params, prefer_rgba8,
		 force_preferred](const auto &a, const auto &b) {
			if (force_preferred) {
				const bool a_pref =
					(a.format() == preferred_params.format() &&
					 a.channel_count() == preferred_params.channel_count());
				const bool b_pref =
					(b.format() == preferred_params.format() &&
					 b.channel_count() == preferred_params.channel_count());
				if (a_pref != b_pref) {
					return a_pref;
				}
			}
			if (prefer_rgba8) {
				const bool a_rgba8 = a.format() ==
										 olive::core::PixelFormat::u8 &&
									 a.channel_count() == 4;
				const bool b_rgba8 = b.format() ==
										 olive::core::PixelFormat::u8 &&
									 b.channel_count() == 4;
				if (a_rgba8 != b_rgba8) {
					return a_rgba8;
				}
			}
			const int cost_a = conversion_cost(src_params, a);
			const int cost_b = conversion_cost(src_params, b);
			if (cost_a != cost_b) {
				return cost_a < cost_b;
			}
			if (a.format() == preferred_params.format() &&
				a.channel_count() == preferred_params.channel_count()) {
				return true;
			}
			return false;
		});

	for (const auto &candidate : candidates) {
		if (candidate.format() == src_params.format() &&
			candidate.channel_count() == src_params.channel_count()) {
			*out_params = src_params;
			return src;
		}
		olive::TexturePtr converted = convert_texture_for_params(src, candidate);
		if (converted) {
			*out_params = candidate;
			return converted;
		}
	}

	return nullptr;
}

// 作用：从 OFX Image 复制数据到 AVFrame（按图像属性推导格式）。
// Purpose: Copy OFX Image data into an AVFrame with inferred format.
static olive::AVFramePtr
create_avframe_from_ofx_image(OFX::Host::ImageEffect::Image &image)
{
	void *data_ptr = image.getPointerProperty(kOfxImagePropData);
	if (!data_ptr) {
		qWarning().noquote() << "OFX output image missing data pointer";
		return nullptr;
	}

	int bounds[4] = { 0, 0, 0, 0 };
	image.getIntPropertyN(kOfxImagePropBounds, bounds, 4);
	int width = bounds[2] - bounds[0];
	int height = bounds[3] - bounds[1];
	if (width <= 0 || height <= 0) {
		qWarning().noquote()
			<< "OFX output image has invalid bounds" << bounds[0] << bounds[1]
			<< bounds[2] << bounds[3];
		return nullptr;
	}

	int bytes_per_pixel = 0;
	int pix_fmt = get_ofx_av_pixel_format(image, &bytes_per_pixel);
	if (pix_fmt == fb_pix_fmt_none || bytes_per_pixel <= 0) {
		qWarning().noquote()
			<< "OFX output image has unsupported pixel format depth="
			<< QString::fromStdString(
				   image.getStringProperty(kOfxImageEffectPropPixelDepth))
			<< "components="
			<< QString::fromStdString(
				   image.getStringProperty(kOfxImageEffectPropComponents));
		return nullptr;
	}

	int row_bytes = image.getIntProperty(kOfxImagePropRowBytes);
	if (row_bytes <= 0) {
		row_bytes = width * bytes_per_pixel;
	}

	uint8_t *src = static_cast<uint8_t *>(data_ptr);
	src += bounds[1] * row_bytes + bounds[0] * bytes_per_pixel;

	olive::AVFramePtr frame = olive::create_av_frame_ptr();
	frame->set_width(width);
	frame->set_height(height);
	frame->set_format(pix_fmt);

	if (frame->get_buffer(0) < 0) {
		return nullptr;
	}

	const int copy_bytes = width * bytes_per_pixel;
	if (frame->linesize(0) == row_bytes && row_bytes == copy_bytes) {
		std::memcpy(frame->data(0), src, copy_bytes * height);
	} else {
		for (int y = 0; y < height; ++y) {
			std::memcpy(frame->data(0) + y * frame->linesize(0),
						src + y * row_bytes, copy_bytes);
		}
	}

	return frame;
}

// 前置声明：必要时将 AVFrame 转换为目标 VideoParams 对应格式。
// Forward declaration: Convert AVFrame to match destination VideoParams when needed.

// 作用：按指定 VideoParams 复制 OFX Image 到 AVFrame，必要时做格式转换。
// Purpose: Copy OFX Image data into an AVFrame using target VideoParams with format conversion.
static olive::AVFramePtr
create_avframe_from_ofx_image_with_params(OFX::Host::ImageEffect::Image &image,
										  const olive::VideoParams &params,
										  olive::Renderer *renderer = nullptr)
{
	void *data_ptr = image.getPointerProperty(kOfxImagePropData);
	if (!data_ptr) {
		return nullptr;
	}

	int bounds[4] = { 0, 0, 0, 0 };
	image.getIntPropertyN(kOfxImagePropBounds, bounds, 4);
	int width = bounds[2] - bounds[0];
	int height = bounds[3] - bounds[1];
	if (width <= 0 || height <= 0) {
		return nullptr;
	}

	// Get ACTUAL source format from image properties
	std::string image_depth =
		image.getStringProperty(kOfxImageEffectPropPixelDepth);
	std::string image_comp =
		image.getStringProperty(kOfxImageEffectPropComponents);

	int src_channel_count = 4;
	if (image_comp == kOfxImageComponentRGB)
		src_channel_count = 3;
	else if (image_comp == kOfxImageComponentAlpha)
		src_channel_count = 1;

	// NOTE: FP16 (Half) format handling has been removed.
	// FP16 data is now treated as U16 (2 bytes per component) and converted via FFmpeg.
	int src_bytes_per_component = 1;
	if (image_depth == kOfxBitDepthShort)
		src_bytes_per_component = 2;
	else if (image_depth == kOfxBitDepthHalf)
		src_bytes_per_component = 2; // Treat as U16
	else if (image_depth == kOfxBitDepthFloat)
		src_bytes_per_component = 4;

	const int src_bytes_per_pixel = src_channel_count * src_bytes_per_component;
	const int dst_bytes_per_pixel =
		params.channel_count() * params.format().byte_count();

	int row_bytes = image.getIntProperty(kOfxImagePropRowBytes);
	if (row_bytes <= 0) {
		row_bytes = width * src_bytes_per_pixel;
	}

	uint8_t *src = static_cast<uint8_t *>(data_ptr);
	src += bounds[1] * row_bytes + bounds[0] * src_bytes_per_pixel;

	// Check if format conversion is needed
	bool needs_conversion = (src_bytes_per_pixel != dst_bytes_per_pixel) ||
							(src_channel_count != params.channel_count());

	if (needs_conversion) {
		// Create source frame with actual format
		int src_fmt = fb_pix_fmt_none;

		if (src_channel_count == 4) {
			if (src_bytes_per_component == 1)
				src_fmt = fb_pix_fmt_rgba;
			else if (src_bytes_per_component == 2)
				src_fmt = fb_pix_fmt_rgb_a64_le;
			else if (src_bytes_per_component == 4)
				src_fmt = fb_pix_fmt_rgba_f32_le;
		} else if (src_channel_count == 3) {
			if (src_bytes_per_component == 1)
				src_fmt = fb_pix_fmt_rg_b24;
			else if (src_bytes_per_component == 2)
				src_fmt = fb_pix_fmt_rg_b48_le;
			else if (src_bytes_per_component == 4)
				src_fmt = fb_pix_fmt_rgb_f32_le;
		} else if (src_channel_count == 1) {
			if (src_bytes_per_component == 1)
				src_fmt = fb_pix_fmt_gra_y8;
			else if (src_bytes_per_component == 2)
				src_fmt = fb_pix_fmt_gra_y16_le;
			else if (src_bytes_per_component == 4)
				src_fmt = fb_pix_fmt_gray_f32_le;
		}

		if (src_fmt != fb_pix_fmt_none) {
			olive::AVFramePtr src_frame = olive::create_av_frame_ptr();
			src_frame->set_width(width);
			src_frame->set_height(height);
			src_frame->set_format(src_fmt);
			if (src_frame->get_buffer(0) >= 0) {
				// Copy source data row by row (or as a single block if strides match)
				const int copy_bytes = width * src_bytes_per_pixel;
				if (src_frame->linesize(0) == row_bytes &&
					row_bytes == copy_bytes) {
					memcpy(src_frame->data(0), src, copy_bytes * height);
				} else {
					for (int y = 0; y < height; ++y) {
						memcpy(src_frame->data(0) + y * src_frame->linesize(0),
							   src + y * row_bytes, copy_bytes);
					}
				}
				// Convert to destination format
				return convert_frame_if_needed(src_frame, params, renderer);
			} else {
				qWarning().noquote()
					<< "[WARN] av_frame_get_buffer failed for src_fmt="
					<< src_fmt;
			}
		} else {
			qWarning().noquote() << "[WARN] src_fmt is NONE for depth="
								 << QString::fromStdString(image_depth);
		}
	}

	// Same format - direct copy
	int pix_fmt = get_destination_av_pixel_format(params);
	if (pix_fmt == fb_pix_fmt_none) {
		return nullptr;
	}

	olive::AVFramePtr frame = olive::create_av_frame_ptr();
	frame->set_width(width);
	frame->set_height(height);
	frame->set_format(pix_fmt);

	if (frame->get_buffer(0) < 0) {
		return nullptr;
	}

	const int copy_bytes =
		width * std::min(src_bytes_per_pixel, dst_bytes_per_pixel);
	for (int y = 0; y < height; ++y) {
		memcpy(frame->data(0) + y * frame->linesize(0), src + y * row_bytes,
			   copy_bytes);
	}

	return frame;
}

// 作用：将 VideoParams 映射为最终输出的 AVPixelFormat。
// Purpose: Map VideoParams to the final AVPixelFormat.
static int
get_destination_av_pixel_format(const olive::VideoParams &params)
{
	int pix_fmt = olive::FFmpegUtils::get_f_fmpeg_pixel_format(
		params.format(), params.channel_count());
	if (pix_fmt == fb_pix_fmt_none && params.channel_count() == 1) {
		if (params.format() == olive::core::PixelFormat::u8) {
			pix_fmt = fb_pix_fmt_gra_y8;
		} else if (params.format() == olive::core::PixelFormat::u16) {
			pix_fmt = fb_pix_fmt_gra_y16_le;
		} else if (params.format() == olive::core::PixelFormat::f16) {
			pix_fmt = fb_pix_fmt_gray_f16_le;
		} else if (params.format() == olive::core::PixelFormat::f32) {
			pix_fmt = fb_pix_fmt_gray_f32_le;
		}
	}
	return pix_fmt;
}

// 作用：根据交错设置返回 OFX render field 字符串。
// Purpose: Return OFX render field string based on interlacing.
static const char *get_render_field_for_params(const olive::VideoParams &params)
{
	switch (params.interlacing()) {
	case olive::VideoParams::k_interlace_none:
		return kOfxImageFieldNone;
	case olive::VideoParams::k_interlaced_top_first:
	case olive::VideoParams::k_interlaced_bottom_first:
		return kOfxImageFieldBoth;
	}
	return kOfxImageFieldNone;
}

// 作用：从 GPU 纹理回读到 AVFrame（必要时做格式转换）。
// Purpose: Read back GPU texture into AVFrame with format conversion if needed.
static olive::AVFramePtr
readback_texture_to_frame(olive::TexturePtr texture,
					   const olive::VideoParams &params)
{
	if (!texture || texture->is_dummy()) {
		return nullptr;
	}

	int pix_fmt = get_destination_av_pixel_format(params);
	if (pix_fmt == fb_pix_fmt_none) {
		return nullptr;
	}

	if (!fb_pix_fmt_is_planar(pix_fmt)) {
		olive::AVFramePtr frame = olive::create_av_frame_ptr();
		frame->set_format(pix_fmt);
		frame->set_width(params.width());
		frame->set_height(params.height());
		if (frame->get_buffer(0) < 0) {
			return nullptr;
		}

		if (texture->renderer()) {
			const int linesize_pixels = olive::plugin::detail::bytes_to_pixels(
				frame->linesize(0), params);
			texture->renderer()->download_from_texture(
				texture->id(), params, frame->data(0), linesize_pixels);
		}
		return frame;
	}

	// Planar formats: read back as RGBA and convert.
	olive::VideoParams rgba_params(params.width(), params.height(),
								   olive::core::PixelFormat::u8, 4,
								   params.pixel_aspect_ratio(),
								   params.interlacing(), params.divider());

	olive::AVFramePtr rgba_frame = olive::create_av_frame_ptr();
	rgba_frame->set_format(fb_pix_fmt_rgba);
	rgba_frame->set_width(params.width());
	rgba_frame->set_height(params.height());
	if (rgba_frame->get_buffer(0) < 0) {
		return nullptr;
	}

	if (texture->renderer()) {
		const int linesize_pixels = olive::plugin::detail::bytes_to_pixels(
			rgba_frame->linesize(0), rgba_params);
		texture->renderer()->download_from_texture(
			texture->id(), rgba_params, rgba_frame->data(0), linesize_pixels);
	}

	olive::AVFramePtr dst = olive::create_av_frame_ptr();
	dst->set_format(pix_fmt);
	dst->set_width(params.width());
	dst->set_height(params.height());
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

// 作用：将字节行跨度转换为像素行跨度。
// Purpose: Convert byte stride to pixel stride.
int olive::plugin::detail::bytes_to_pixels(int byte_linesize,
										 const olive::VideoParams &params)
{
	const int bytes_per_pixel = olive::VideoParams::get_bytes_per_pixel(
		params.format(), params.channel_count());
	if (byte_linesize <= 0 || bytes_per_pixel <= 0) {
		return 0;
	}
	return byte_linesize / bytes_per_pixel;
}

// 作用：将 AVPixelFormat 映射为 Olive 的 PixelFormat 和通道数（仅常见 packed 格式）。
static void get_olive_format_from_av(int fmt,
								 olive::core::PixelFormat *out_fmt, int *out_ch)
{
	switch (fmt) {
	case fb_pix_fmt_gra_y8:
		*out_fmt = olive::core::PixelFormat::u8;
		*out_ch = 1;
		return;
	case fb_pix_fmt_rg_b24:
		*out_fmt = olive::core::PixelFormat::u8;
		*out_ch = 3;
		return;
	case fb_pix_fmt_rgba:
		*out_fmt = olive::core::PixelFormat::u8;
		*out_ch = 4;
		return;
	case fb_pix_fmt_gra_y16_le:
	case fb_pix_fmt_gra_y16_be:
		*out_fmt = olive::core::PixelFormat::u16;
		*out_ch = 1;
		return;
	case fb_pix_fmt_rg_b48_le:
	case fb_pix_fmt_rg_b48_be:
		*out_fmt = olive::core::PixelFormat::u16;
		*out_ch = 3;
		return;
	case fb_pix_fmt_rgb_a64_le:
	case fb_pix_fmt_rgb_a64_be:
		*out_fmt = olive::core::PixelFormat::u16;
		*out_ch = 4;
		return;
	case fb_pix_fmt_gray_f32_le:
	case fb_pix_fmt_gray_f32_be:
		*out_fmt = olive::core::PixelFormat::f32;
		*out_ch = 1;
		return;
	case fb_pix_fmt_rgb_f32_le:
	case fb_pix_fmt_rgb_f32_be:
		*out_fmt = olive::core::PixelFormat::f32;
		*out_ch = 3;
		return;
	case fb_pix_fmt_rgba_f32_le:
	case fb_pix_fmt_rgba_f32_be:
		*out_fmt = olive::core::PixelFormat::f32;
		*out_ch = 4;
		return;
	default:
		*out_fmt = olive::core::PixelFormat::invalid;
		*out_ch = 0;
		return;
	}
}

// 作用：必要时将 AVFrame 转换为目标 VideoParams 对应格式。
//       优先使用 FFmpeg sws_scale；若不支持且 renderer 可用，则走 GPU 路径。
//       删除所有手写 CPU 像素循环，避免精度损失与性能瓶颈。
static olive::AVFramePtr
convert_frame_if_needed(olive::AVFramePtr src,
					 const olive::VideoParams &dst_params,
					 olive::Renderer *renderer = nullptr)
{
	if (!src) {
		return nullptr;
	}

	int dst_fmt = get_destination_av_pixel_format(dst_params);
	if (dst_fmt == fb_pix_fmt_none) {
		return src;
	}

	// Same format & size, no conversion needed
	if (src->format() == dst_fmt && src->width() == dst_params.width() &&
		src->height() == dst_params.height()) {
		return src;
	}

	olive::AVFramePtr dst = olive::create_av_frame_ptr();
	dst->set_format(dst_fmt);
	dst->set_width(dst_params.width());
	dst->set_height(dst_params.height());
	if (dst->get_buffer(0) < 0) {
		qWarning().noquote()
			<< "[WARN] av_frame_get_buffer failed for dst_fmt=" << dst_fmt;
		return src;
	}

	// Try FFmpeg sws_scale first
	FBScaler *scaler = fb_scaler_create(
		src->width(), src->height(), src->format(),
		dst->width(), dst->height(), dst_fmt, FB_SCALER_POINT);
	if (scaler) {
		uint8_t *src_data[4];
		int src_linesize[4];
		uint8_t *dst_data[4];
		int dst_linesize[4];
		for (int i = 0; i < 4; ++i) {
			src_data[i] = src->data(i);
			src_linesize[i] = src->linesize(i);
			dst_data[i] = dst->data(i);
			dst_linesize[i] = dst->linesize(i);
		}
		int ret = fb_scaler_scale_slices(scaler, src_data, src_linesize,
										 src->height(), dst_data, dst_linesize);
		fb_scaler_free(&scaler);
		if (ret > 0) {
			return dst;
		}
	}

	// sws_scale failed (e.g. RGBAF32 not supported). Use GPU if renderer available.
	if (renderer && src->data(0)) {
		olive::core::PixelFormat src_fmt;
		int src_ch;
		get_olive_format_from_av(src->format(), &src_fmt,
							 &src_ch);
		if (src_fmt != olive::core::PixelFormat::invalid && src_ch > 0) {
			// Ensure renderer's OpenGL context is current before GPU operations.
			// The context may have been switched by upstream DownloadFromTexture calls.
			auto *gl_renderer = dynamic_cast<olive::OpenGLRenderer *>(renderer);
			if (gl_renderer) {
				gl_renderer->ensure_context_current(__FUNCTION__);
			}

			olive::VideoParams src_vp(src->width(), src->height(), src_fmt, src_ch);
			int src_bpp = olive::VideoParams::get_bytes_per_pixel(src_fmt, src_ch);
			int src_linesize_pixels =
				(src_bpp > 0) ? src->linesize(0) / src_bpp : src->width();

			olive::TexturePtr src_tex = renderer->create_texture(
				src_vp, src->data(0), src_linesize_pixels);
			if (src_tex) {
				olive::TexturePtr dst_tex = renderer->create_texture(dst_params);
				if (dst_tex) {
					olive::ShaderJob job;
					job.insert(QStringLiteral("ove_maintex"),
							   olive::NodeValue(olive::NodeValue::k_texture,
												QVariant::fromValue(src_tex)));
					renderer->blit_to_texture(renderer->get_default_shader(), job,
											dst_tex.get(), false);

					// Download result back to AVFrame
					int dst_bpp = dst_params.get_bytes_per_pixel();
					int dst_linesize_pixels =
						(dst_bpp > 0) ? dst->linesize(0) / dst_bpp : dst->width();
					dst_tex->download(dst->data(0), dst_linesize_pixels);
					return dst;
				}
			}
		}
	}

	qWarning().noquote()
		<< "[WARN] ConvertFrameIfNeeded failed (sws_scale + GPU both unavailable). "
		<< "Returning unconverted source. src_fmt=" << src->format()
		<< " dst_fmt=" << dst_fmt;
	return src;
}

// 作用：从字节行跨度换算像素行跨度。
// Purpose: Convert byte line size to pixel line size.
static int linesize_to_pixels(const olive::VideoParams &params,
							int linesize_bytes)
{
	const int bytes_per_pixel =
		params.channel_count() * params.format().byte_count();
	if (bytes_per_pixel <= 0) {
		return 0;
	}
	return linesize_bytes / bytes_per_pixel;
}

// 作用：将纹理转换为指定 VideoParams。优先使用 GPU shader 做格式转换，
//       避免 CPU 回读/转换/上传的性能损失和精度损失。
static olive::TexturePtr
convert_texture_for_params(olive::TexturePtr src,
						const olive::VideoParams &dst_params)
{
	if (!src) {
		return nullptr;
	}
	const olive::VideoParams &src_params = src->params();
	if (src_params.format() == dst_params.format() &&
		src_params.channel_count() == dst_params.channel_count() &&
		src_params.width() == dst_params.width() &&
		src_params.height() == dst_params.height()) {
		return src;
	}

	// GPU path: blit directly to destination format using default shader.
	// OpenGL texture sampling automatically normalizes U8/U16 to float,
	// and write-out quantizes float back to U8/U16 when needed.
	if (auto *renderer = src->renderer()) {
		olive::TexturePtr dst = renderer->create_texture(dst_params);
		if (dst) {
			olive::ShaderJob job;
			job.insert(QStringLiteral("ove_maintex"),
					   olive::NodeValue(olive::NodeValue::k_texture,
										QVariant::fromValue(src)));
			renderer->blit_to_texture(renderer->get_default_shader(), job,
									dst.get(), false);
			return dst;
		}
	}

	// CPU fallback: readback, sws_scale, re-upload
	olive::AVFramePtr frame = src->frame();
	if (!frame || !frame->data(0)) {
		frame = readback_texture_to_frame(src, src_params);
	}
	if (!frame || !frame->data(0)) {
		return nullptr;
	}

	olive::AVFramePtr converted =
		convert_frame_if_needed(frame, dst_params, nullptr);
	if (!converted || !converted->data(0)) {
		return nullptr;
	}
	if (converted->linesize(0) <= 0) {
		return nullptr;
	}

	olive::TexturePtr dst;
	if (auto *renderer = src->renderer()) {
		int linesize_pixels =
			linesize_to_pixels(dst_params, converted->linesize(0));
		if (linesize_pixels <= 0) {
			linesize_pixels = dst_params.effective_width();
		}
		dst = renderer->create_texture(dst_params, converted->data(0),
									  linesize_pixels);
	} else {
		dst = std::make_shared<olive::Texture>(dst_params);
		int linesize_pixels =
			linesize_to_pixels(dst_params, converted->linesize(0));
		if (linesize_pixels <= 0) {
			linesize_pixels = dst_params.effective_width();
		}
		dst->upload(converted->data(0), linesize_pixels);
	}
	if (dst) {
		dst->handle_frame(converted);
	}
	return dst;
}

// 作用：安全获取插件标识符，便于日志输出。
// Purpose: Safely fetch plugin identifier for logging.
static QString
plugin_id_for_instance(const OFX::Host::ImageEffect::Instance *instance)
{
	if (!instance) {
		return QStringLiteral("<null>");
	}
	auto *plugin = instance->getPlugin();
	if (!plugin) {
		return QStringLiteral("<unknown>");
	}
	return QString::fromStdString(plugin->getIdentifier());
}

// 作用：统一 OFX 调用失败日志输出。
// Purpose: Centralized logging for OFX action failures.
static void log_ofx_failure(const char *action, OfxStatus stat,
						  const OFX::Host::ImageEffect::Instance *instance)
{
	if (stat == kOfxStatOK || stat == kOfxStatReplyDefault) {
		return;
	}
	qWarning().noquote()
		<< "OFX action failed:" << action
		<< "plugin=" << plugin_id_for_instance(instance)
		<< "status=" << OFX::StatStr(stat) << "(" << stat << ")";
}

// 作用：输出 clip 的声明属性与关联 VideoParams，辅助定位格式不一致。
// Purpose: Log clip declared properties and VideoParams for debugging.
static void log_clip_state(const char *label,
						 const OFX::Host::ImageEffect::ClipInstance *clip,
						 const olive::VideoParams *params)
{
	if (!clip) {
		qWarning().noquote() << "OFX clip state" << label << "<null>";
		return;
	}
	qWarning().noquote()
		<< "OFX clip state" << label
		<< "name=" << QString::fromStdString(clip->getName())
		<< "pixelDepth=" << QString::fromStdString(clip->getPixelDepth())
		<< "components=" << QString::fromStdString(clip->getComponents());
	if (params) {
		qWarning().noquote()
			<< "OFX clip params" << label << "width=" << params->width()
			<< "height=" << params->height()
			<< "format=" << static_cast<int>(params->format())
			<< "channels=" << params->channel_count();
	}
}

// 作用：输出 OFX Image 的属性（深度/组件/行跨度/边界）。
// Purpose: Log OFX image properties (depth/components/stride/bounds).
static void log_image_props(const char *label,
						  OFX::Host::ImageEffect::Image *image)
{
	if (!image) {
		/*qWarning().noquote() << "OFX image props" << label << "<null>";
		return;*/
	}
	int bounds[4] = { 0, 0, 0, 0 };
	int rod[4] = { 0, 0, 0, 0 };
	image->getIntPropertyN(kOfxImagePropBounds, bounds, 4);
	image->getIntPropertyN(kOfxImagePropRegionOfDefinition, rod, 4);
	const int row_bytes = image->getIntProperty(kOfxImagePropRowBytes);
	const std::string &depth =
		image->getStringProperty(kOfxImageEffectPropPixelDepth);
	const std::string &components =
		image->getStringProperty(kOfxImageEffectPropComponents);
	/*qWarning().noquote()
		<< "OFX image props" << label
		<< "pixelDepth=" << QString::fromStdString(depth)
		<< "components=" << QString::fromStdString(components)
		<< "rowBytes=" << row_bytes
		<< "bounds=" << bounds[0] << bounds[1] << bounds[2] << bounds[3]
		<< "rod=" << rod[0] << rod[1] << rod[2] << rod[3];*/
}

// 作用：渲染失败时标记目标画面（紫色）提示错误。
// Purpose: Mark render failure on destination (magenta).
static void mark_render_failure(olive::TexturePtr destination)
{
	if (destination && destination->renderer()) {
		destination->renderer()->clear_destination(destination.get(), 1.0, 0.0,
												  1.0, 1.0);
	}
}

/// Show an error dialog and undo the last operation. Must be called from the GUI thread.
static void show_error_dialog_and_undo(const QString &message)
{
	if (auto *core = olive::Core::instance()) {
		if (auto *stack = core->undo_stack()) {
			if (stack->can_undo()) {
				stack->undo();
			}
		}
	}
	QMessageBox::critical(nullptr, QObject::tr("Plugin Error"), message);
}

/// Schedule an error dialog + undo on the GUI thread from a render thread.
static void schedule_error_dialog_and_undo(const QString &message)
{
	if (auto *app = QCoreApplication::instance()) {
		QMetaObject::invokeMethod(
			app, [message]() { show_error_dialog_and_undo(message); },
			Qt::QueuedConnection);
	}
}

static olive::AVFramePtr download_texture_to_frame(const olive::TexturePtr &tex)
{
	if (!tex || tex->is_dummy() || !tex->renderer()) {
		return nullptr;
	}
	const olive::VideoParams &params = tex->params();
	return readback_texture_to_frame(tex, params);
}
inline std::vector<std::string>
get_plugin_supported_depths(const OFX::Host::ImageEffect::Descriptor &desc)
{
	std::vector<std::string> depths;
	const OFX::Host::Property::Set &props = desc.getProps();

	// 获取数组维度（支持几种深度）
	int dim = props.getDimension(kOfxImageEffectPropSupportedPixelDepths);
	for (int i = 0; i < dim; ++i) {
		// Host Support Library 返回 const std::string&
		const std::string &val =
			props.getStringProperty(kOfxImageEffectPropSupportedPixelDepths, i);
		if (!val.empty() && val != kOfxBitDepthNone) {
			depths.push_back(val);
		}
	}
	return depths;
}

// 查询插件/宿主是否支持「各 clip 不同深度」
inline bool
supports_multiple_clip_depths(const OFX::Host::ImageEffect::Descriptor &desc)
{
	const OFX::Host::Property::Set &props = desc.getProps();
	// 这是单值 int 属性（0 或 1），n = 0
	int val =
		props.getIntProperty(kOfxImageEffectPropSupportsMultipleClipDepths, 0);
	return val != 0;
}

// 作用：根据插件描述符声明的 kOfxImageEffectPropSupportedPixelDepths，
//       按优先级 F32 > U16 > U8 > F16 选择最佳输入像素格式。
//       参考 OpenFX API: OfxImageEffectPropSupportedPixelDepths
// Purpose: Select best input pixel format from plugin descriptor's supported
//          depth list. Priority: F32 > U16 > U8 > F16.
static PixelFormat
select_best_plugin_input_format(const OFX::Host::ImageEffect::Descriptor &desc)
{
	const OFX::Host::Property::Set &props = desc.getProps();
	int dim = props.getDimension(kOfxImageEffectPropSupportedPixelDepths);

	bool supports_f32 = false;
	bool supports_u16 = false;
	bool supports_u8 = false;
	bool supports_f16 = false;

	for (int i = 0; i < dim; ++i) {
		const std::string &depth =
			props.getStringProperty(kOfxImageEffectPropSupportedPixelDepths, i);
		if (depth == kOfxBitDepthFloat) {
			supports_f32 = true;
		} else if (depth == kOfxBitDepthShort) {
			supports_u16 = true;
		} else if (depth == kOfxBitDepthByte) {
			supports_u8 = true;
		} else if (depth == kOfxBitDepthHalf) {
			supports_f16 = true;
		}
	}

	// 优先级：F32 > U16 > U8 > F16
	if (supports_f32)
		return PixelFormat::f32;
	if (supports_u16)
		return PixelFormat::u16;
	if (supports_u8)
		return PixelFormat::u8;
	if (supports_f16)
		return PixelFormat::f16;
	return PixelFormat::invalid;
}
// 作用：执行 OFX 插件渲染全流程（准备输入、调用动作、处理输出）。
// Purpose: Run full OFX plugin render flow (inputs, actions, outputs).
void olive::plugin::PluginRenderer::render_plugin(
	TexturePtr src, olive::plugin::PluginJob &job,
	olive::TexturePtr destination, olive::VideoParams destination_params,
	bool clear_destination, bool interactive)
{
	auto instance = job.plugin_instance();
	if (!instance) {
		return;
	}

	// Lock the plugin instance to prevent concurrent access from multiple
	// RenderProcessors. OlivePluginInstance (and its OliveClipInstance) are not
	// thread-safe; concurrent calls to setInputTexture/renderAction can corrupt
	// internal QMap/images_ and params_, leading to invalid pointers being
	// passed to CImg and subsequent SIGSEGV.
	std::mutex *instance_mutex = nullptr;
	if (auto *olive_inst =
			dynamic_cast<olive::plugin::OlivePluginInstance *>(instance)) {
		instance_mutex = &olive_inst->mutex();
	} else {
		static std::mutex fallback_mutex;
		instance_mutex = &fallback_mutex;
	}
	std::lock_guard<std::mutex> instance_lock(*instance_mutex);

	bool supports_opengl = false;
#ifdef OFX_SUPPORTS_OPENGLRENDER
	const std::string &gl_supported =
		instance->getDescriptor().getProps().getStringProperty(
			kOfxImageEffectPropOpenGLRenderSupported);
	supports_opengl = (gl_supported == "true" || gl_supported == "1");
#endif
	auto *olive_instance =
		dynamic_cast<olive::plugin::OlivePluginInstance *>(instance);
	const bool use_opengl =
		supports_opengl && renderer_ && renderer_->is_open_gl() && destination &&
		destination->renderer() == renderer_ && destination->id().isValid();
	if (olive_instance) {
		olive_instance->setOpenGLEnabled(use_opengl);
		olive_instance->setVideoParam(destination_params);
		// Ensure all clip instances inherit the project's params so that
		// getAspectRatio/getFrameRate etc. return valid values before
		// createInstanceAction (which may call fetchClip and query them).
		for (int i = 0; i < olive_instance->getNClips(); ++i) {
			OliveClipInstance *clip = dynamic_cast<OliveClipInstance *>(
				olive_instance->getNthClip(i));
			if (clip) {
				clip->setParams(destination_params);
			}
		}
	}

	// current render scale of 1
	OfxPointD render_scale;
	render_scale.x = render_scale.y = 1.0;

	int num_frames_to_render = 1;

	// Output Clip
	OliveClipInstance *output_clip =
		dynamic_cast<plugin::OliveClipInstance *>(instance->getClip("Output"));
	if (!output_clip) {
		return;
	}

	// ensure the instance was created
	OfxStatus stat = kOfxStatOK;
	if (olive_instance && !olive_instance->isCreated()) {
		stat = instance->createInstanceAction();
		if (stat != kOfxStatOK && stat != kOfxStatReplyDefault) {
			log_ofx_failure("createInstance", stat, instance);
			mark_render_failure(destination);
			return;
		}
	}

	OfxTime frame = job.time_seconds();

	const auto &clips = olive_instance->getDescriptor().getClips();
	QString effect_input_id;
	if (const auto *node = job.node()) {
		effect_input_id = node->get_effect_input_id();
	}
	auto is_usable_input = [](const TexturePtr &tex) {
		if (!tex) {
			return false;
		}
		if (!tex->is_dummy() && tex->renderer()) {
			return true;
		}
		AVFramePtr frame = tex->frame();
		return frame && frame->data(0);
	};
	std::map<std::string, TexturePtr> input_textures;
	std::map<std::string, OliveClipInstance *> input_clips;
	std::map<std::string, olive::VideoParams> input_params;
	auto values = job.get_values();
	for (const auto &entry : clips) {
		if (entry.first == kOfxImageEffectOutputClipName) {
			continue;
		}
		OliveClipInstance *input_clip =
			dynamic_cast<OliveClipInstance *>(instance->getClip(entry.first));
		if (!input_clip) {
			continue;
		}
		const QString clip_key = QString::fromStdString(entry.first);
		TexturePtr input_tex = nullptr;
		if (!effect_input_id.isEmpty() && clip_key == effect_input_id &&
			is_usable_input(src)) {
			input_tex = src;
		} else {
			input_tex = values.value(clip_key).to_texture();
			if (!input_tex &&
				entry.first == kOfxImageEffectSimpleSourceClipName) {
				input_tex = values.value(k_texture_input).to_texture();
			}
		}
		if (!is_usable_input(input_tex) &&
			entry.first == kOfxImageEffectSimpleSourceClipName &&
			is_usable_input(src)) {
			input_tex = src;
		}
		if (is_usable_input(input_tex)) {
			input_textures[entry.first] = input_tex;
			olive::VideoParams params = input_tex->params();
			// First pass: set params only, no CPU readback yet.
			// Readback will happen below (CPU path) or be skipped entirely (GL path).
			input_clip->setInputTexture(input_tex, frame, false);
			input_clips[entry.first] = input_clip;
		}
	}

	// call getClipPreferences to know which format plugin requires.
	// getClipPreferences internally calls setupClipPreferencesArgs, which
	// validates that all connected input clips have the same frame rate.
	// Wrap in try/catch because setupClipPreferencesArgs throws on validation
	// failure and would otherwise crash the render thread.
	bool ok = false;
	try {
		if (instance->areClipPrefsDirty()) {
			ok = instance->getClipPreferences();
		} else {
			ok = true;
		}
	} catch (const OFX::Host::Property::Exception &e) {
		qWarning().noquote()
			<< "OFX getClipPreferences threw exception for plugin="
			<< plugin_id_for_instance(instance) << "stat=" << e.getStatus();
		mark_render_failure(destination);
		schedule_error_dialog_and_undo(
			QObject::tr(
				"Plugin %1 failed because connected inputs have different frame rates.\n"
				"The last operation has been undone.")
				.arg(plugin_id_for_instance(instance)));
		return;
	} catch (const std::exception &e) {
		qWarning().noquote()
			<< "OFX getClipPreferences threw exception for plugin="
			<< plugin_id_for_instance(instance) << "what=" << e.what();
		mark_render_failure(destination);
		schedule_error_dialog_and_undo(
			QObject::tr("Plugin %1 encountered an error: %2\n"
						"The last operation has been undone.")
				.arg(plugin_id_for_instance(instance),
					 QString::fromUtf8(e.what())));
		return;
	}
	if (!ok) {
		qWarning().noquote() << "OFX getClipPreferences failed for plugin="
							 << plugin_id_for_instance(instance);
		mark_render_failure(destination);
		schedule_error_dialog_and_undo(
			QObject::tr("Plugin %1 failed to get clip preferences.\n"
						"The last operation has been undone.")
				.arg(plugin_id_for_instance(instance)));
		return;
	}
	/// RoI is in canonical coords.
	OfxRectD region_of_interest;
	region_of_interest.x1 = 0.0;
	region_of_interest.y1 = 0.0;
	region_of_interest.x2 = destination_params.width() *
						  destination_params.pixel_aspect_ratio().to_double();
	region_of_interest.y2 = destination_params.height();

	OfxRectD region_of_definition = region_of_interest;

	output_clip->setRegionOfDefinition(region_of_definition, frame);
	output_clip->setOutputTexture(destination, frame);

	// get the RoI for each input clip
	// the regions of interest for each input clip are returned in a std::map
	// on a real host, these will be the regions of each input clip that the
	// effect needs to render a given frame (clipped to the RoD).
	//
	// In our example we are doing full frame fetches regardless.

	// set correct format for input

	// Ensure all input textures are fully rendered before CPU readback.
	// BlitColorManaged may have executed in a different shared OpenGL context;
	// glFinish() in our context does NOT wait for commands in that context,
	// so we must flush the renderer that actually produced the texture.
	for (const auto &entry : input_textures) {
		if (entry.second && entry.second->renderer()) {
			entry.second->renderer()->flush();
		}
	}

	auto &descriptor = instance->getDescriptor();
	for (const auto &entry : input_clips) {
		if (entry.first == kOfxImageEffectOutputClipName) {
			continue;
		}
		OliveClipInstance *input_clip = entry.second;
		if (!input_clip) {
			continue;
		}
		const QString clip_key = QString::fromStdString(entry.first);
		TexturePtr input_tex = input_textures[entry.first];
		if (is_usable_input(input_tex)) {
			// Query the plugin descriptor for supported pixel depths and pick
			// the best one according to our priority: F32 > U16 > U8 > F16.
			// OpenFX reference: kOfxImageEffectPropSupportedPixelDepths on
			// the image effect descriptor lists all depths the plugin can handle.
			PixelFormat chosen_fmt = select_best_plugin_input_format(descriptor);
			VideoParams params = input_tex->params();

			if (chosen_fmt != PixelFormat::invalid &&
				params.format() != chosen_fmt) {
				params.set_format(chosen_fmt);
				TexturePtr converted_tex =
					convert_texture_for_params(input_tex, params);
				if (converted_tex && is_usable_input(converted_tex)) {
					input_tex = converted_tex;
					input_textures[entry.first] = input_tex;
				}
			}

			input_clip->setInputTexture(input_tex, frame, !use_opengl);
			OfxRectD rod;
			rod.x1 = 0;
			rod.y1 = 0;
			rod.x2 = params.width() * params.pixel_aspect_ratio().to_double();
			rod.y2 = params.height();
			input_clip->setRegionOfDefinition(rod, frame);
			input_clips[entry.first] = input_clip;
		}
	}
	std::map<OFX::Host::ImageEffect::ClipInstance *, OfxRectD> rois;
	stat = instance->getRegionOfInterestAction(frame, render_scale,
											   region_of_interest, rois);
	if (stat != kOfxStatOK && stat != kOfxStatReplyDefault) {
		// Some plugins (e.g. CImg filters) return BadHandle from getRegionOfInterest
		// when internal clip/property handles are not fully initialized.
		// Treat this as a non-fatal error and fall back to default RoI.
		if (stat == kOfxStatErrBadHandle) {
			qWarning().noquote()
				<< "OFX getRegionOfInterest returned BadHandle for plugin="
				<< plugin_id_for_instance(instance) << "- using default RoI";
		} else {
			log_ofx_failure("getRegionOfInterest", stat, instance);
			mark_render_failure(destination);
			return;
		}
	}
	// set correct format for output
	// Query plugin-supported depths and pick best according to our priority:
	// F32 > U16 > U8 > F16. If plugin supports F32 we render directly to F32
	// (zero conversion). If plugin only supports U8/U16 we let it render in
	// that format and ConvertFrameIfNeeded will convert back to F32 afterwards.
	VideoParams output_params = destination_params;
	PixelFormat best_fmt = select_best_plugin_input_format(descriptor);
	if (best_fmt != PixelFormat::invalid) {
		output_params.set_format(best_fmt);
	}
	output_clip->setParams(output_params);
	std::string component = output_clip->getUnmappedComponents();
	if (!component.empty() && component != kOfxImageComponentNone) {
		output_params.set_channel_count(component);
	}
	// Re-set params so the clip knows the possibly-changed channel count
	output_clip->setParams(output_params);

	// The render window is in pixel coordinates
	// ie: render scale and a PAR of not 1
	OfxRectI render_window;
	render_window.x1 = render_window.y1 = 0;
	render_window.x2 = destination_params.width();
	render_window.y2 = destination_params.height();

	stat = instance->beginRenderAction(frame, num_frames_to_render, 1.0, false,
									   render_scale, true, interactive);
	if (stat != kOfxStatOK && stat != kOfxStatReplyDefault) {
		log_ofx_failure("beginRender", stat, instance);
		mark_render_failure(destination);
		return;
	}

#ifdef OFX_SUPPORTS_OPENGLRENDER
	if (use_opengl) {
		instance->contextAttachedAction();
		attach_output_texture(destination);
	}
#endif

	if (!output_params.is_valid()) {
		qWarning().noquote()
			<< "OFX render skipped due to invalid output params for plugin="
			<< plugin_id_for_instance(instance);
		mark_render_failure(destination);
		instance->endRenderAction(frame, num_frames_to_render, 1.0, interactive,
								  render_scale, true, interactive);
		return;
	}

	// Inject current parameter values into the OFX instance before rendering.
	// Parameters are bound to PluginNode inputs, so they change every frame.
	apply_param_overrides(*instance, job.get_values(), frame);

	// render a frame
	const char *render_field = get_render_field_for_params(output_params);
	stat = instance->renderAction(frame, render_field, render_window,
								  render_scale, true, interactive, interactive);
	if (stat != kOfxStatOK && stat != kOfxStatReplyDefault) {
		log_ofx_failure("render", stat, instance);
		log_clip_state("output", output_clip, &output_params);
		for (const auto &entry : input_clips) {
			const auto params_it = input_params.find(entry.first);
			const olive::VideoParams *params =
				(params_it != input_params.end()) ? &params_it->second :
													nullptr;
			log_clip_state("input", entry.second, params);
			OFX::Host::ImageEffect::Image *image =
				entry.second->getImage(frame, nullptr);
			log_image_props("input", image);
			//if (image) {
			//image->releaseReference();
			//}
		}
		OFX::Host::ImageEffect::Image *output_image =
			output_clip->getOutputImage(frame);
		log_image_props("output", output_image);
		mark_render_failure(destination);
		instance->endRenderAction(frame, num_frames_to_render, 1.0, interactive,
								  render_scale, true, interactive);
		return;
	}

	// get the output image buffer (CPU path only)
	OFX::Host::ImageEffect::Image *output_image;
	if (!use_opengl) {
		output_image = output_clip->getOutputImage(frame);
		if (!output_image) {
			qWarning().noquote()
				<< "OFX getOutputImage returned null for plugin="
				<< plugin_id_for_instance(instance);
			mark_render_failure(destination);
			instance->endRenderAction(frame, num_frames_to_render, 1.0,
									  interactive, render_scale, true,
									  interactive);
			return;
		}
		// Diagnostic: peek at first few pixels
		void *img_data = output_image->getPointerProperty(kOfxImagePropData);
		bool img_black = true;
		if (img_data) {
			float *f = static_cast<float *>(img_data);
			for (int i = 0; i < 16; ++i) {
				if (f[i] != 0.0f) {
					img_black = false;
					break;
				}
			}
		}
	} else {
		if (!destination || !destination->id().isValid()) {
#ifdef OFX_SUPPORTS_OPENGLRENDER
			detach_output_texture();
			instance->contextDetachedAction();
#endif
			instance->endRenderAction(frame, num_frames_to_render, 1.0,
									  interactive, render_scale, true,
									  interactive);
			return;
		}
	}

	if (!use_opengl) {
		AVFramePtr frame_ptr = create_avframe_from_ofx_image_with_params(
			*output_image, output_params);
		if (!frame_ptr) {
			qWarning().noquote()
				<< "OFX output image conversion failed for plugin="
				<< plugin_id_for_instance(instance);
			instance->endRenderAction(frame, num_frames_to_render, 1.0,
									  interactive, render_scale, true,
									  interactive);
			return;
		}
		AVFramePtr converted =
			convert_frame_if_needed(frame_ptr, destination_params, renderer_);
		const int expected_fmt =
			get_destination_av_pixel_format(destination_params);
		destination->handle_frame(converted);
		if (destination->renderer() && converted && converted->data(0) &&
			(expected_fmt == fb_pix_fmt_none ||
			 converted->format() == expected_fmt)) {
			int linesize_pixels =
				linesize_to_pixels(destination_params, converted->linesize(0));
			if (linesize_pixels <= 0) {
				linesize_pixels = destination_params.effective_width();
			}
			destination->upload(converted->data(0), linesize_pixels);
		} else if (destination->renderer() && converted && converted->data(0)) {
			qWarning().noquote()
				<< "OFX output pixel format mismatch for plugin="
				<< plugin_id_for_instance(instance);
		}
	} else {
		// OpenGL path: plugin has already rendered directly into the destination
		// texture via FBO/GL. No CPU readback or conversion needed.
#ifdef OFX_SUPPORTS_OPENGLRENDER
		detach_output_texture();
		instance->contextDetachedAction();
#endif
		instance->endRenderAction(frame, num_frames_to_render, 1.0, interactive,
								  render_scale, true, interactive);
		return;
	}

	instance->endRenderAction(frame, num_frames_to_render, 1.0, interactive,
							  render_scale, true, interactive);
}

// 作用：绑定输出纹理到 OFX 的 GL 输出路径。
// Purpose: Attach output texture for OFX GL rendering.
void olive::plugin::PluginRenderer::attach_output_texture(
	olive::TexturePtr texture)
{
	if (renderer_) {
		renderer_->attach_output_texture(texture.get());
	}
}

// 作用：解除 OFX 的 GL 输出绑定。
// Purpose: Detach OFX GL output binding.
void olive::plugin::PluginRenderer::detach_output_texture()
{
	if (renderer_) {
		renderer_->detach_output_texture();
	}
}
