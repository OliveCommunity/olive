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

#include "common/videoparams.h"

#include <cstring>

#include "../src/videoparams.h"

struct OakCommonVideoParams {
	olive::VideoParams impl;
};

namespace
{

int copy_string(const std::string &value, char *buf, int buf_size)
{
	int needed = (int)value.size() + 1;
	if (buf && buf_size >= needed)
		memcpy(buf, value.c_str(), needed);
	return needed;
}

int get_rational(const olive::core::Rational &r, int *numerator,
				 int *denominator)
{
	if (!numerator || !denominator)
		return OAKCOMMON_E_INVALID;
	*numerator = r.numerator();
	*denominator = r.denominator();
	return OAKCOMMON_OK;
}

} // namespace

OakCommonVideoParams *oakcommon_videoparams_init(void)
{
	try {
		return new OakCommonVideoParams{olive::VideoParams()};
	} catch (...) {
		return nullptr;
	}
}

OakCommonVideoParams *oakcommon_videoparams_init_basic(
	int width, int height, int pixel_format, int nb_channels,
	int pixel_aspect_num, int pixel_aspect_den, int interlacing, int divider)
{
	try {
		return new OakCommonVideoParams{olive::VideoParams(
			width, height,
			static_cast<olive::core::PixelFormat::Format>(pixel_format),
			nb_channels,
			olive::core::Rational(pixel_aspect_num, pixel_aspect_den),
			static_cast<olive::VideoParams::Interlacing>(interlacing),
			divider)};
	} catch (...) {
		return nullptr;
	}
}

OakCommonVideoParams *oakcommon_videoparams_init_with_time_base(
	int width, int height, int time_base_num, int time_base_den,
	int pixel_format, int nb_channels, int pixel_aspect_num,
	int pixel_aspect_den, int interlacing, int divider)
{
	try {
		return new OakCommonVideoParams{olive::VideoParams(
			width, height,
			olive::core::Rational(time_base_num, time_base_den),
			static_cast<olive::core::PixelFormat::Format>(pixel_format),
			nb_channels,
			olive::core::Rational(pixel_aspect_num, pixel_aspect_den),
			static_cast<olive::VideoParams::Interlacing>(interlacing),
			divider)};
	} catch (...) {
		return nullptr;
	}
}

void oakcommon_videoparams_free(OakCommonVideoParams *params)
{
	delete params;
}

#define OAKCOMMON_VIDEOPARAMS_INT_GETTER(name, expr)                          \
	int oakcommon_videoparams_get_##name(OakCommonVideoParams *params,        \
										 int *out)                            \
	{                                                                         \
		if (!params || !out)                                                  \
			return OAKCOMMON_E_INVALID;                                       \
		*out = (expr);                                                        \
		return OAKCOMMON_OK;                                                  \
	}

#define OAKCOMMON_VIDEOPARAMS_INT_SETTER(name, stmt)                          \
	int oakcommon_videoparams_set_##name(OakCommonVideoParams *params,        \
										 int value)                           \
	{                                                                         \
		if (!params)                                                          \
			return OAKCOMMON_E_INVALID;                                       \
		stmt;                                                                 \
		return OAKCOMMON_OK;                                                  \
	}

OAKCOMMON_VIDEOPARAMS_INT_GETTER(width, params->impl.width())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(width, params->impl.set_width(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(height, params->impl.height())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(height, params->impl.set_height(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(depth, params->impl.depth())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(depth, params->impl.set_depth(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(is_3d, params->impl.is_3d() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_GETTER(format, static_cast<int>(params->impl.format()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	format, params->impl.set_format(
				static_cast<olive::core::PixelFormat::Format>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(channel_count, params->impl.channel_count())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(channel_count,
								 params->impl.set_channel_count(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(interlacing,
								 static_cast<int>(params->impl.interlacing()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	interlacing,
	params->impl.set_interlacing(
		static_cast<olive::VideoParams::Interlacing>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(divider, params->impl.divider())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(divider, params->impl.set_divider(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(enabled, params->impl.enabled() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_SETTER(enabled, params->impl.set_enabled(value != 0))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(stream_index, params->impl.stream_index())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(stream_index,
								 params->impl.set_stream_index(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(video_type,
								 static_cast<int>(params->impl.video_type()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	video_type,
	params->impl.set_video_type(static_cast<olive::VideoParams::Type>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(premultiplied_alpha,
								 params->impl.premultiplied_alpha() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_SETTER(premultiplied_alpha,
								 params->impl.set_premultiplied_alpha(
									 value != 0))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(color_range,
								 static_cast<int>(params->impl.color_range()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	color_range, params->impl.set_color_range(
					 static_cast<olive::VideoParams::ColorRange>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(color_primaries,
								 params->impl.color_primaries())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(color_primaries,
								 params->impl.set_color_primaries(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(color_transfer, params->impl.color_transfer())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(color_transfer,
								 params->impl.set_color_transfer(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(square_pixel_width,
								 params->impl.square_pixel_width())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(effective_width,
								 params->impl.effective_width())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(effective_height,
								 params->impl.effective_height())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(effective_depth,
								 params->impl.effective_depth())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(is_valid, params->impl.is_valid() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_GETTER(bytes_per_channel,
								 params->impl.get_bytes_per_channel())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(bytes_per_pixel,
								 params->impl.get_bytes_per_pixel())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(buffer_size, params->impl.get_buffer_size())

int oakcommon_videoparams_get_x(OakCommonVideoParams *params, float *x)
{
	if (!params || !x)
		return OAKCOMMON_E_INVALID;
	*x = params->impl.x();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_x(OakCommonVideoParams *params, float x)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_x(x);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_y(OakCommonVideoParams *params, float *y)
{
	if (!params || !y)
		return OAKCOMMON_E_INVALID;
	*y = params->impl.y();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_y(OakCommonVideoParams *params, float y)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_y(y);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_start_time(OakCommonVideoParams *params,
										 int64_t *start_time)
{
	if (!params || !start_time)
		return OAKCOMMON_E_INVALID;
	*start_time = params->impl.start_time();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_start_time(OakCommonVideoParams *params,
										 int64_t start_time)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_start_time(start_time);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_duration(OakCommonVideoParams *params,
									   int64_t *duration)
{
	if (!params || !duration)
		return OAKCOMMON_E_INVALID;
	*duration = params->impl.duration();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_duration(OakCommonVideoParams *params,
									   int64_t duration)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_duration(duration);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_time_base(OakCommonVideoParams *params,
										int *numerator, int *denominator)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	return get_rational(params->impl.time_base(), numerator, denominator);
}

int oakcommon_videoparams_set_time_base(OakCommonVideoParams *params,
										int numerator, int denominator)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_time_base(olive::core::Rational(numerator, denominator));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_frame_rate(OakCommonVideoParams *params,
										 int *numerator, int *denominator)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	return get_rational(params->impl.frame_rate(), numerator, denominator);
}

int oakcommon_videoparams_set_frame_rate(OakCommonVideoParams *params,
										 int numerator, int denominator)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_frame_rate(olive::core::Rational(numerator, denominator));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_frame_rate_as_time_base(OakCommonVideoParams *params,
												  int *numerator,
												  int *denominator)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	return get_rational(params->impl.frame_rate_as_time_base(), numerator,
						denominator);
}

int oakcommon_videoparams_get_pixel_aspect_ratio(OakCommonVideoParams *params,
												 int *numerator,
												 int *denominator)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	return get_rational(params->impl.pixel_aspect_ratio(), numerator,
						denominator);
}

int oakcommon_videoparams_set_pixel_aspect_ratio(OakCommonVideoParams *params,
												 int numerator, int denominator)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	params->impl.set_pixel_aspect_ratio(
		olive::core::Rational(numerator, denominator));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_colorspace(OakCommonVideoParams *params,
										 char *buf, int buf_size)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(params->impl.colorspace(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_videoparams_set_colorspace(OakCommonVideoParams *params,
										 const char *colorspace)
{
	if (!params || !colorspace)
		return OAKCOMMON_E_INVALID;
	params->impl.set_colorspace(colorspace);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_time_in_timebase_units(
	OakCommonVideoParams *params, int time_num, int time_den,
	int64_t *timestamp)
{
	if (!params || !timestamp)
		return OAKCOMMON_E_INVALID;
	*timestamp = params->impl.get_time_in_timebase_units(
		olive::core::Rational(time_num, time_den));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_equals(OakCommonVideoParams *params,
								 OakCommonVideoParams *other, int *equal)
{
	if (!params || !other || !equal)
		return OAKCOMMON_E_INVALID;
	*equal = (params->impl == other->impl) ? 1 : 0;
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_load_xml(OakCommonVideoParams *params,
								   const char *xml)
{
	if (!params || !xml)
		return OAKCOMMON_E_INVALID;
	try {
		olive::XmlStreamReader reader(xml);
		if (reader.has_error())
			return OAKCOMMON_E_FAILED;
		// Position on the root element; load() consumes its children.
		if (!olive::xml_read_next_start_element(&reader))
			return OAKCOMMON_E_FAILED;
		params->impl.load(&reader);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_videoparams_save_xml(OakCommonVideoParams *params, char *buf,
								   int buf_size)
{
	if (!params)
		return OAKCOMMON_E_INVALID;
	try {
		olive::XmlStreamWriter writer;
		writer.write_start_element("videoparams");
		params->impl.save(&writer);
		writer.write_end_element();
		return copy_string(writer.output(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_videoparams_get_bytes_per_channel_for_format(int pixel_format)
{
	return olive::VideoParams::get_bytes_per_channel(
		static_cast<olive::core::PixelFormat::Format>(pixel_format));
}

int oakcommon_videoparams_get_bytes_per_pixel_for_format(int pixel_format,
														 int channels)
{
	return olive::VideoParams::get_bytes_per_pixel(
		static_cast<olive::core::PixelFormat::Format>(pixel_format), channels);
}

int oakcommon_videoparams_calculate_buffer_size(int width, int height,
												int pixel_format, int channels)
{
	return olive::VideoParams::get_buffer_size(
		width, height,
		static_cast<olive::core::PixelFormat::Format>(pixel_format), channels);
}

int oakcommon_videoparams_format_is_float(int pixel_format)
{
	return olive::VideoParams::format_is_float(
			   static_cast<olive::core::PixelFormat::Format>(pixel_format)) ?
			   1 :
			   0;
}

int oakcommon_videoparams_generate_auto_divider(int64_t width, int64_t height)
{
	return olive::VideoParams::generate_auto_divider(width, height);
}

int oakcommon_videoparams_get_scaled_dimension(int dimension, int divider)
{
	return olive::VideoParams::get_scaled_dimension(dimension, divider);
}

int oakcommon_videoparams_get_divider_for_target_resolution(int src_width,
															int src_height,
															int dst_width,
															int dst_height)
{
	return olive::VideoParams::get_divider_for_target_resolution(
		src_width, src_height, dst_width, dst_height);
}

int oakcommon_videoparams_get_name_for_divider(int divider, char *buf,
											   int buf_size)
{
	try {
		return copy_string(olive::VideoParams::get_name_for_divider(divider),
						   buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_videoparams_get_format_name(int pixel_format, char *buf,
										  int buf_size)
{
	try {
		return copy_string(olive::VideoParams::get_format_name(
							   static_cast<olive::core::PixelFormat::Format>(
								   pixel_format)),
						   buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_videoparams_frame_rate_to_string(int numerator, int denominator,
											   char *buf, int buf_size)
{
	try {
		return copy_string(olive::VideoParams::frame_rate_to_string(
							   olive::core::Rational(numerator, denominator)),
						   buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}
