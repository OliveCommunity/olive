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
#include "refcounted.h"

namespace
{

/**
 * @brief Recover the boxed olive::VideoParams from a handle (NULL-safe).
 */
olive::VideoParams *vp(OakVideoParams params)
{
	return oakcommon::handle_impl<olive::VideoParams>(params.ctx);
}

} // namespace

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

OakVideoParams oakcommon_videoparams_init(void)
{
	try {
		return oakcommon::make_handle<OakVideoParams>(
			olive::VideoParams());
	} catch (...) {
		OakVideoParams h = {};
		return h;
	}
}

OakVideoParams oakcommon_videoparams_init_basic(
	int width, int height, int pixel_format, int nb_channels,
	int pixel_aspect_num, int pixel_aspect_den, int interlacing, int divider)
{
	try {
		return oakcommon::make_handle<OakVideoParams>(
			olive::VideoParams(
				width, height,
				static_cast<olive::core::PixelFormat::Format>(pixel_format),
				nb_channels,
				olive::core::Rational(pixel_aspect_num, pixel_aspect_den),
				static_cast<olive::VideoParams::Interlacing>(interlacing),
				divider));
	} catch (...) {
		OakVideoParams h = {};
		return h;
	}
}

OakVideoParams oakcommon_videoparams_init_with_time_base(
	int width, int height, int time_base_num, int time_base_den,
	int pixel_format, int nb_channels, int pixel_aspect_num,
	int pixel_aspect_den, int interlacing, int divider)
{
	try {
		return oakcommon::make_handle<OakVideoParams>(
			olive::VideoParams(
				width, height,
				olive::core::Rational(time_base_num, time_base_den),
				static_cast<olive::core::PixelFormat::Format>(pixel_format),
				nb_channels,
				olive::core::Rational(pixel_aspect_num, pixel_aspect_den),
				static_cast<olive::VideoParams::Interlacing>(interlacing),
				divider));
	} catch (...) {
		OakVideoParams h = {};
		return h;
	}
}

OakVideoParams oakcommon_videoparams_init_from_native(
	const olive::VideoParams *src)
{
	if (!src) {
		OakVideoParams h = {};
		return h;
	}
	try {
		return oakcommon::make_handle<OakVideoParams>(
			olive::VideoParams(*src));
	} catch (...) {
		OakVideoParams h = {};
		return h;
	}
}

const olive::VideoParams *oakcommon_videoparams_get_native(
	OakVideoParams params)
{
	return vp(params);
}

void oakcommon_videoparams_free(OakVideoParams *params)
{
	oakcommon::free_handle(params);
}

#define OAKCOMMON_VIDEOPARAMS_INT_GETTER(name, expr)                          \
	int oakcommon_videoparams_get_##name(OakVideoParams params,        \
										 int *out)                            \
	{                                                                         \
		if (!vp(params) || !out)                                                  \
			return OAKCOMMON_E_INVALID;                                       \
		*out = (expr);                                                        \
		return OAKCOMMON_OK;                                                  \
	}

#define OAKCOMMON_VIDEOPARAMS_INT_SETTER(name, stmt)                          \
	int oakcommon_videoparams_set_##name(OakVideoParams params,        \
										 int value)                           \
	{                                                                         \
		if (!vp(params))                                                          \
			return OAKCOMMON_E_INVALID;                                       \
		stmt;                                                                 \
		return OAKCOMMON_OK;                                                  \
	}

OAKCOMMON_VIDEOPARAMS_INT_GETTER(width, vp(params)->width())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(width, vp(params)->set_width(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(height, vp(params)->height())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(height, vp(params)->set_height(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(depth, vp(params)->depth())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(depth, vp(params)->set_depth(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(is_3d, vp(params)->is_3d() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_GETTER(format, static_cast<int>(vp(params)->format()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	format, vp(params)->set_format(
				static_cast<olive::core::PixelFormat::Format>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(channel_count, vp(params)->channel_count())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(channel_count,
								 vp(params)->set_channel_count(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(interlacing,
								 static_cast<int>(vp(params)->interlacing()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	interlacing,
	vp(params)->set_interlacing(
		static_cast<olive::VideoParams::Interlacing>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(divider, vp(params)->divider())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(divider, vp(params)->set_divider(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(enabled, vp(params)->enabled() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_SETTER(enabled, vp(params)->set_enabled(value != 0))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(stream_index, vp(params)->stream_index())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(stream_index,
								 vp(params)->set_stream_index(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(video_type,
								 static_cast<int>(vp(params)->video_type()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	video_type,
	vp(params)->set_video_type(static_cast<olive::VideoParams::Type>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(premultiplied_alpha,
								 vp(params)->premultiplied_alpha() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_SETTER(premultiplied_alpha,
								 vp(params)->set_premultiplied_alpha(
									 value != 0))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(color_range,
								 static_cast<int>(vp(params)->color_range()))
OAKCOMMON_VIDEOPARAMS_INT_SETTER(
	color_range, vp(params)->set_color_range(
					 static_cast<olive::VideoParams::ColorRange>(value)))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(color_primaries,
								 vp(params)->color_primaries())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(color_primaries,
								 vp(params)->set_color_primaries(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(color_transfer, vp(params)->color_transfer())
OAKCOMMON_VIDEOPARAMS_INT_SETTER(color_transfer,
								 vp(params)->set_color_transfer(value))
OAKCOMMON_VIDEOPARAMS_INT_GETTER(square_pixel_width,
								 vp(params)->square_pixel_width())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(effective_width,
								 vp(params)->effective_width())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(effective_height,
								 vp(params)->effective_height())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(effective_depth,
								 vp(params)->effective_depth())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(is_valid, vp(params)->is_valid() ? 1 : 0)
OAKCOMMON_VIDEOPARAMS_INT_GETTER(bytes_per_channel,
								 vp(params)->get_bytes_per_channel())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(bytes_per_pixel,
								 vp(params)->get_bytes_per_pixel())
OAKCOMMON_VIDEOPARAMS_INT_GETTER(buffer_size, vp(params)->get_buffer_size())

int oakcommon_videoparams_get_x(OakVideoParams params, float *x)
{
	if (!vp(params) || !x)
		return OAKCOMMON_E_INVALID;
	*x = vp(params)->x();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_x(OakVideoParams params, float x)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	vp(params)->set_x(x);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_y(OakVideoParams params, float *y)
{
	if (!vp(params) || !y)
		return OAKCOMMON_E_INVALID;
	*y = vp(params)->y();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_y(OakVideoParams params, float y)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	vp(params)->set_y(y);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_start_time(OakVideoParams params,
										 int64_t *start_time)
{
	if (!vp(params) || !start_time)
		return OAKCOMMON_E_INVALID;
	*start_time = vp(params)->start_time();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_start_time(OakVideoParams params,
										 int64_t start_time)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	vp(params)->set_start_time(start_time);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_duration(OakVideoParams params,
									   int64_t *duration)
{
	if (!vp(params) || !duration)
		return OAKCOMMON_E_INVALID;
	*duration = vp(params)->duration();
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_set_duration(OakVideoParams params,
									   int64_t duration)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	vp(params)->set_duration(duration);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_time_base(OakVideoParams params,
										int *numerator, int *denominator)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	return get_rational(vp(params)->time_base(), numerator, denominator);
}

int oakcommon_videoparams_set_time_base(OakVideoParams params,
										int numerator, int denominator)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	vp(params)->set_time_base(olive::core::Rational(numerator, denominator));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_frame_rate(OakVideoParams params,
										 int *numerator, int *denominator)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	return get_rational(vp(params)->frame_rate(), numerator, denominator);
}

int oakcommon_videoparams_set_frame_rate(OakVideoParams params,
										 int numerator, int denominator)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	vp(params)->set_frame_rate(olive::core::Rational(numerator, denominator));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_frame_rate_as_time_base(OakVideoParams params,
												  int *numerator,
												  int *denominator)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	return get_rational(vp(params)->frame_rate_as_time_base(), numerator,
						denominator);
}

int oakcommon_videoparams_get_pixel_aspect_ratio(OakVideoParams params,
												 int *numerator,
												 int *denominator)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	return get_rational(vp(params)->pixel_aspect_ratio(), numerator,
						denominator);
}

int oakcommon_videoparams_set_pixel_aspect_ratio(OakVideoParams params,
												 int numerator, int denominator)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	vp(params)->set_pixel_aspect_ratio(
		olive::core::Rational(numerator, denominator));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_colorspace(OakVideoParams params,
										 char *buf, int buf_size)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	try {
		return copy_string(vp(params)->colorspace(), buf, buf_size);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_videoparams_set_colorspace(OakVideoParams params,
										 const char *colorspace)
{
	if (!vp(params) || !colorspace)
		return OAKCOMMON_E_INVALID;
	vp(params)->set_colorspace(colorspace);
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_get_time_in_timebase_units(
	OakVideoParams params, int time_num, int time_den,
	int64_t *timestamp)
{
	if (!vp(params) || !timestamp)
		return OAKCOMMON_E_INVALID;
	*timestamp = vp(params)->get_time_in_timebase_units(
		olive::core::Rational(time_num, time_den));
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_equals(OakVideoParams params,
								 OakVideoParams other, int *equal)
{
	if (!vp(params) || !vp(other) || !equal)
		return OAKCOMMON_E_INVALID;
	*equal = (*vp(params) == *vp(other)) ? 1 : 0;
	return OAKCOMMON_OK;
}

int oakcommon_videoparams_load_xml(OakVideoParams params,
								   const char *xml)
{
	if (!vp(params) || !xml)
		return OAKCOMMON_E_INVALID;
	try {
		olive::XmlStreamReader reader(xml);
		if (reader.has_error())
			return OAKCOMMON_E_FAILED;
		// Position on the root element; load() consumes its children.
		if (!olive::xml_read_next_start_element(&reader))
			return OAKCOMMON_E_FAILED;
		vp(params)->load(&reader);
		return OAKCOMMON_OK;
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}
}

int oakcommon_videoparams_save_xml(OakVideoParams params, char *buf,
								   int buf_size)
{
	if (!vp(params))
		return OAKCOMMON_E_INVALID;
	try {
		olive::XmlStreamWriter writer;
		writer.write_start_element("videoparams");
		vp(params)->save(&writer);
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

static olive::PixelFormat convert_to_olive_format(OakPixelFormat format)
{
	switch (format) {
	case OAKCOMMON_PIXEL_FORMAT_INVALID:
		return olive::PixelFormat::invalid;
	case OAKCOMMON_PIXEL_FORMAT_COUNT:
		return olive::PixelFormat::count;
	case OAKCOMMON_PIXEL_FORMAT_U8:
		return olive::PixelFormat::u8;
	case OAKCOMMON_PIXEL_FORMAT_U10:
		return olive::PixelFormat::u10;
	case OAKCOMMON_PIXEL_FORMAT_U16:
		return olive::PixelFormat::u16;
	case OAKCOMMON_PIXEL_FORMAT_F16:
		return olive::PixelFormat::f16;
	case OAKCOMMON_PIXEL_FORMAT_F32:
		return olive::PixelFormat::f32;
	}
	return olive::PixelFormat::invalid;
}
int oakcommon_videoparams_static_get_bytes_per_channel(OakPixelFormat format)
{
	return olive::VideoParams::get_bytes_per_channel(convert_to_olive_format(format));
}

int oakcommon_videoparams_static_get_bytes_per_pixel(OakPixelFormat format, int channels)
{
	return olive::VideoParams::get_bytes_per_pixel(convert_to_olive_format(format), channels);
}
