/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include "videoparams.h"

#include <cmath>
#include <cstdio>

#include <olive/core/util/timecodefunctions.h>

#include "ofxImageEffect.h"

namespace olive
{

const int VideoParams::k_internal_channel_count = k_rgba_channel_count;

const Rational VideoParams::k_pixel_aspect_square(1);
const Rational VideoParams::k_pixel_aspect_ntsc_standard(8, 9);
const Rational VideoParams::k_pixel_aspect_ntsc_widescreen(32, 27);
const Rational VideoParams::k_pixel_aspect_pal_standard(16, 15);
const Rational VideoParams::k_pixel_aspect_pal_widescreen(64, 45);
const Rational VideoParams::k_pixel_aspect1080_anamorphic(4, 3);

const std::vector<Rational> VideoParams::k_supported_frame_rates = {
	Rational(10, 1), // 10 FPS
	Rational(15, 1), // 15 FPS
	Rational(24000, 1001), // 23.976 FPS
	Rational(24, 1), // 24 FPS
	Rational(25, 1), // 25 FPS
	Rational(30000, 1001), // 29.97 FPS
	Rational(30, 1), // 30 FPS
	Rational(48000, 1001), // 47.952 FPS
	Rational(48, 1), // 48 FPS
	Rational(50, 1), // 50 FPS
	Rational(60000, 1001), // 59.94 FPS
	Rational(60, 1) // 60 FPS
};

const std::vector<int> VideoParams::k_supported_dividers = {
	1, 2, 3, 4, 6, 8, 12, 16
};

const std::vector<Rational> VideoParams::k_standard_pixel_aspects = {
	VideoParams::k_pixel_aspect_square,
	VideoParams::k_pixel_aspect_ntsc_standard,
	VideoParams::k_pixel_aspect_ntsc_widescreen,
	VideoParams::k_pixel_aspect_pal_standard,
	VideoParams::k_pixel_aspect_pal_widescreen,
	VideoParams::k_pixel_aspect1080_anamorphic
};

VideoParams::VideoParams()
	: width_(0)
	, height_(0)
	, depth_(0)
	, time_base_(0)
	, format_(PixelFormat::invalid)
	, channel_count_(0)
	, pixel_aspect_ratio_(1)
	, interlacing_(Interlacing::k_interlace_none)
	, divider_(1)
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

VideoParams::VideoParams(int width, int height, PixelFormat format,
						 int nb_channels, const Rational &pixel_aspect_ratio,
						 Interlacing interlacing, int divider)
	: width_(width)
	, height_(height)
	, depth_(1)
	, format_(format)
	, channel_count_(nb_channels)
	, pixel_aspect_ratio_(pixel_aspect_ratio)
	, interlacing_(interlacing)
	, divider_(divider)
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

VideoParams::VideoParams(int width, int height, int depth, PixelFormat format,
						 int nb_channels, const Rational &pixel_aspect_ratio,
						 VideoParams::Interlacing interlacing, int divider)
	: width_(width)
	, height_(height)
	, depth_(depth)
	, format_(format)
	, channel_count_(nb_channels)
	, pixel_aspect_ratio_(pixel_aspect_ratio)
	, interlacing_(interlacing)
	, divider_(divider)
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

void VideoParams::set_channel_count(const std::string &ofx_component)
{
	if (ofx_component == kOfxImageComponentAlpha) {
		channel_count_ = 1;
	} else if (ofx_component == kOfxImageComponentRGB) {
		channel_count_ = k_rgb_channel_count;
	} else if (ofx_component == kOfxImageComponentRGBA) {
		channel_count_ = k_rgba_channel_count;
	}
}

VideoParams::VideoParams(int width, int height, const Rational &time_base,
						 PixelFormat format, int nb_channels,
						 const Rational &pixel_aspect_ratio,
						 Interlacing interlacing, int divider)
	: width_(width)
	, height_(height)
	, depth_(1)
	, time_base_(time_base)
	, format_(format)
	, channel_count_(nb_channels)
	, pixel_aspect_ratio_(pixel_aspect_ratio)
	, interlacing_(interlacing)
	, divider_(divider)
	, frame_rate_(time_base.flipped())
{
	calculate_effective_size();
	validate_pixel_aspect_ratio();
	set_defaults_for_footage();
}

int VideoParams::generate_auto_divider(int64_t width, int64_t height)
{
	const int target_res = 1280 * 720;

	int64_t megapixels = width * height;

	double squared_divider = double(megapixels) / double(target_res);
	double divider = std::sqrt(squared_divider);

	if (divider <= k_supported_dividers.front()) {
		return k_supported_dividers.front();
	} else if (divider >= k_supported_dividers.back()) {
		return k_supported_dividers.back();
	} else {
		for (size_t i = 1; i < k_supported_dividers.size(); i++) {
			int prev_divider = k_supported_dividers.at(i - 1);
			int next_divider = k_supported_dividers.at(i);

			if (divider >= prev_divider && divider <= next_divider) {
				double prev_diff = std::fabs(prev_divider - divider);
				double next_diff = std::fabs(next_divider - divider);

				if (prev_diff < next_diff) {
					return prev_divider;
				} else {
					return next_divider;
				}
			}
		}

		// Fallback
		return 1;
	}
}

bool VideoParams::operator==(const VideoParams &rhs) const
{
	return width() == rhs.width() && height() == rhs.height() &&
		   depth() == rhs.depth() && interlacing() == rhs.interlacing() &&
		   time_base() == rhs.time_base() && format() == rhs.format() &&
		   pixel_aspect_ratio() == rhs.pixel_aspect_ratio() &&
		   divider() == rhs.divider() && channel_count() == rhs.channel_count();
}

bool VideoParams::operator!=(const VideoParams &rhs) const
{
	return !(*this == rhs);
}

int VideoParams::get_bytes_per_channel(PixelFormat format)
{
	switch (format) {
	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	case PixelFormat::u8:
		return 1;
	case PixelFormat::u10:
		return 0; // packed format, use get_bytes_per_pixel instead
	case PixelFormat::u16:
	case PixelFormat::f16:
		return 2;
	case PixelFormat::f32:
		return 4;
	}

	return 0;
}

int VideoParams::get_bytes_per_pixel(PixelFormat format, int channels)
{
	if (format == PixelFormat::u10) {
		// Packed 10-bit RGBA10A2: 4 bytes per RGBA pixel regardless of channel count
		return channels == VideoParams::k_rgba_channel_count ? 4 : 0;
	}
	return get_bytes_per_channel(format) * channels;
}

std::string VideoParams::get_name_for_divider(int div)
{
	if (div == 1) {
		return std::string("Full");
	} else {
		return std::string("1/") + std::to_string(div);
	}
}

std::string VideoParams::get_format_name(PixelFormat format)
{
	switch (format) {
	case PixelFormat::u8:
		return "8-bit";
	case PixelFormat::u10:
		return "10-bit Packed";
	case PixelFormat::u16:
		return "16-bit Integer";
	case PixelFormat::f16:
		return "Half-Float (16-bit)";
	case PixelFormat::f32:
		return "Full-Float (32-bit)";
	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	}

	char buf[64];
	std::snprintf(buf, sizeof(buf), "Unknown (0x%X)",
				  static_cast<int>(format));
	return buf;
}

int VideoParams::get_divider_for_target_resolution(int src_width, int src_height,
												   int dst_width, int dst_height)
{
	int divider = 0;
	int test_width, test_height;

	do {
		divider++;

		test_width = VideoParams::get_scaled_dimension(src_width, divider);
		test_height = VideoParams::get_scaled_dimension(src_height, divider);
	} while (test_width > dst_width || test_height > dst_height);

	return divider;
}

void VideoParams::calculate_effective_size()
{
	effective_width_ = get_scaled_dimension(width(), divider_);
	effective_height_ = get_scaled_dimension(height(), divider_);
	effective_depth_ = (depth() == 1) ? depth() :
										get_scaled_dimension(depth(), divider_);
	calculate_square_pixel_width();
}

void VideoParams::validate_pixel_aspect_ratio()
{
	if (pixel_aspect_ratio_.isNull()) {
		pixel_aspect_ratio_ = 1;
	}
	calculate_square_pixel_width();
}

void VideoParams::set_defaults_for_footage()
{
	enabled_ = true;
	stream_index_ = 0;
	video_type_ = k_video_type_video;
	start_time_ = 0;
	duration_ = 0;
	premultiplied_alpha_ = false;
	x_ = 0;
	y_ = 0;
	color_range_ = k_color_range_default;
}

void VideoParams::calculate_square_pixel_width()
{
	if (pixel_aspect_ratio_.denominator() != 0) {
		par_width_ = int(std::lround(width_ * pixel_aspect_ratio_.to_double()));
	} else {
		par_width_ = width_;
	}
}

bool VideoParams::is_valid() const
{
	return (width() > 0 && height() > 0 && !pixel_aspect_ratio_.isNull() &&
			format_ > PixelFormat::invalid && format_ < PixelFormat::count &&
			channel_count_ > 0);
}

std::string VideoParams::frame_rate_to_string(const Rational &frame_rate)
{
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%g FPS", frame_rate.to_double());
	return buf;
}

std::vector<std::string> VideoParams::get_standard_pixel_aspect_ratio_names()
{
	std::vector<std::string> strings = {
		"Square Pixels (%1)",
		"NTSC Standard (%1)",
		"NTSC Widescreen (%1)",
		"PAL Standard (%1)",
		"PAL Widescreen (%1)",
		"HD Anamorphic 1080 (%1)"
	};

	// Format each
	for (size_t i = 0; i < strings.size(); i++) {
		strings.at(i) = format_pixel_aspect_ratio_string(
			strings.at(i), k_standard_pixel_aspects.at(i));
	}

	return strings;
}

std::string VideoParams::format_pixel_aspect_ratio_string(
	const std::string &format, const Rational &ratio)
{
	char number[64];
	std::snprintf(number, sizeof(number), "%.4f", ratio.to_double());

	std::string result = format;
	std::string::size_type pos = result.find("%1");
	if (pos != std::string::npos) {
		result.replace(pos, 2, number);
	}
	return result;
}

int VideoParams::get_scaled_dimension(int dim, int divider)
{
	return dim / divider;
}

int64_t VideoParams::get_time_in_timebase_units(const Rational &time) const
{
	if (time_base_.isNull()) {
		return INT64_MIN; // AV_NOPTS_VALUE
	}

	return Timecode::time_to_timestamp(time, time_base_) + start_time_;
}

void VideoParams::load(XmlStreamReader *reader)
{
	while (xml_read_next_start_element(reader)) {
		const std::string &name = reader->name();
		if (name == "width") {
			set_width(std::stoi(reader->read_element_text()));
		} else if (name == "height") {
			set_height(std::stoi(reader->read_element_text()));
		} else if (name == "depth") {
			set_depth(std::stoi(reader->read_element_text()));
		} else if (name == "timebase") {
			set_time_base(Rational::from_string(reader->read_element_text()));
		} else if (name == "format") {
			set_format(static_cast<PixelFormat::Format>(
				std::stoi(reader->read_element_text())));
		} else if (name == "channelcount") {
			set_channel_count(std::stoi(reader->read_element_text()));
		} else if (name == "pixelaspectratio") {
			set_pixel_aspect_ratio(
				Rational::from_string(reader->read_element_text()));
		} else if (name == "interlacing") {
			set_interlacing(static_cast<VideoParams::Interlacing>(
				std::stoi(reader->read_element_text())));
		} else if (name == "divider") {
			set_divider(std::stoi(reader->read_element_text()));
		} else if (name == "enabled") {
			set_enabled(std::stoi(reader->read_element_text()));
		} else if (name == "x") {
			set_x(std::stof(reader->read_element_text()));
		} else if (name == "y") {
			set_y(std::stof(reader->read_element_text()));
		} else if (name == "streamindex") {
			set_stream_index(std::stoi(reader->read_element_text()));
		} else if (name == "videotype") {
			set_video_type(static_cast<VideoParams::Type>(
				std::stoi(reader->read_element_text())));
		} else if (name == "framerate") {
			set_frame_rate(Rational::from_string(reader->read_element_text()));
		} else if (name == "starttime") {
			set_start_time(std::stoll(reader->read_element_text()));
		} else if (name == "duration") {
			set_duration(std::stoll(reader->read_element_text()));
		} else if (name == "premultipliedalpha") {
			set_premultiplied_alpha(std::stoi(reader->read_element_text()));
		} else if (name == "colorspace") {
			set_colorspace(reader->read_element_text());
		} else if (name == "colorrange") {
			set_color_range(
				static_cast<ColorRange>(std::stoi(reader->read_element_text())));
		} else if (name == "colorprimaries") {
			set_color_primaries(std::stoi(reader->read_element_text()));
		} else if (name == "colortransfer") {
			set_color_transfer(std::stoi(reader->read_element_text()));
		} else {
			reader->skip_current_element();
		}
	}
}

void VideoParams::save(XmlStreamWriter *writer) const
{
	writer->write_text_element("width", std::to_string(width_));
	writer->write_text_element("height", std::to_string(height_));
	writer->write_text_element("depth", std::to_string(depth_));
	writer->write_text_element("timebase", time_base_.to_string());
	writer->write_text_element("format", std::to_string(format_));
	writer->write_text_element("channelcount", std::to_string(channel_count_));
	writer->write_text_element("pixelaspectratio",
							   pixel_aspect_ratio_.to_string());
	writer->write_text_element("interlacing", std::to_string(interlacing_));
	writer->write_text_element("divider", std::to_string(divider_));
	writer->write_text_element("enabled", std::to_string(enabled_));
	writer->write_text_element("x", std::to_string(x_));
	writer->write_text_element("y", std::to_string(y_));
	writer->write_text_element("streamindex", std::to_string(stream_index_));
	writer->write_text_element("videotype", std::to_string(video_type_));
	writer->write_text_element("framerate", frame_rate_.to_string());
	writer->write_text_element("starttime", std::to_string(start_time_));
	writer->write_text_element("duration", std::to_string(duration_));
	writer->write_text_element("premultipliedalpha",
							   std::to_string(premultiplied_alpha_));
	writer->write_text_element("colorspace", colorspace_);
	writer->write_text_element("colorrange", std::to_string(color_range_));
	writer->write_text_element("colorprimaries", std::to_string(color_primaries_));
	writer->write_text_element("colortransfer", std::to_string(color_transfer_));
}

}
