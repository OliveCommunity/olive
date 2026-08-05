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

#ifndef OAK_VIDEOPARAMS_H
#define OAK_VIDEOPARAMS_H

#include <cstdint>
#include <string>
#include <vector>

#include <olive/core/render/pixelformat.h>
#include <olive/core/util/rational.h>

#include "xmlutils.h"

namespace olive
{

using namespace core;

/**
 * @brief Pure-data video parameter set (sunk from engine/render/videoparams.h).
 *
 * De-Qt port: QString -> std::string, QVector -> std::vector,
 * QXmlStreamReader/Writer -> olive::XmlStreamReader/Writer. The QVector2D
 * convenience getters (resolution/square_resolution/offset) were dropped;
 * use width()/height()/x()/y() directly. Translated UI strings are returned
 * as plain English text.
 */
class VideoParams {
public:
	enum Interlacing {
		k_interlace_none,
		k_interlaced_top_first,
		k_interlaced_bottom_first
	};

	enum Type { k_video_type_video, k_video_type_still, k_video_type_image_sequence };
	enum ColorRange {
		k_color_range_limited, // 16-235
		k_color_range_full, // 0-255

		k_color_range_default = k_color_range_limited
	};

	VideoParams();
	VideoParams(int width, int height, PixelFormat format, int nb_channels,
				const Rational &pixel_aspect_ratio = 1,
				Interlacing interlacing = k_interlace_none, int divider = 1);
	VideoParams(int width, int height, int depth, PixelFormat format,
				int nb_channels, const Rational &pixel_aspect_ratio = 1,
				Interlacing interlacing = k_interlace_none, int divider = 1);
	VideoParams(int width, int height, const Rational &time_base,
				PixelFormat format, int nb_channels,
				const Rational &pixel_aspect_ratio = 1,
				Interlacing interlacing = k_interlace_none, int divider = 1);

	int width() const
	{
		return width_;
	}

	void set_width(int width)
	{
		width_ = width;
		calculate_effective_size();
	}

	/**
	 * @brief Returns width multiplied by pixel aspect ratio where applicable
	 */
	int square_pixel_width() const
	{
		return par_width_;
	}

	int height() const
	{
		return height_;
	}

	void set_height(int height)
	{
		height_ = height;
		calculate_effective_size();
	}

	int depth() const
	{
		return depth_;
	}

	void set_depth(int depth)
	{
		depth_ = depth;
		calculate_effective_size();
	}

	bool is_3d() const
	{
		return depth_ > 1;
	}

	const Rational &time_base() const
	{
		return time_base_;
	}

	void set_time_base(const Rational &r)
	{
		time_base_ = r;
	}

	Rational frame_rate_as_time_base() const
	{
		return frame_rate_.flipped();
	}

	int divider() const
	{
		return divider_;
	}

	void set_divider(int d)
	{
		divider_ = d;
		calculate_effective_size();
	}

	int effective_width() const
	{
		return effective_width_;
	}

	int effective_height() const
	{
		return effective_height_;
	}

	int effective_depth() const
	{
		return effective_depth_;
	}

	PixelFormat format() const
	{
		return format_;
	}

	void set_format(PixelFormat f)
	{
		format_ = f;
	}

	int channel_count() const
	{
		return channel_count_;
	}

	void set_channel_count(int c)
	{
		channel_count_ = c;
	}
	void set_channel_count(const std::string &ofx_component);
	const Rational &pixel_aspect_ratio() const
	{
		return pixel_aspect_ratio_;
	}

	void set_pixel_aspect_ratio(const Rational &r)
	{
		pixel_aspect_ratio_ = r;
		validate_pixel_aspect_ratio();
	}

	Interlacing interlacing() const
	{
		return interlacing_;
	}

	void set_interlacing(Interlacing i)
	{
		interlacing_ = i;
	}

	static int generate_auto_divider(int64_t width, int64_t height);

	bool is_valid() const;

	bool operator==(const VideoParams &rhs) const;
	bool operator!=(const VideoParams &rhs) const;

	static int get_bytes_per_channel(PixelFormat format);
	int get_bytes_per_channel() const
	{
		return get_bytes_per_channel(format_);
	}

	static int get_bytes_per_pixel(PixelFormat format, int channels);
	int get_bytes_per_pixel() const
	{
		return get_bytes_per_pixel(format_, channel_count_);
	}

	static int get_buffer_size(int width, int height, PixelFormat format,
							   int channels)
	{
		return width * height * get_bytes_per_pixel(format, channels);
	}
	int get_buffer_size() const
	{
		return get_buffer_size(width_, height_, format_, channel_count_);
	}

	static std::string get_name_for_divider(int div);

	static bool format_is_float(PixelFormat format)
	{
		return format.is_float();
	}

	static std::string get_format_name(PixelFormat format);

	static int get_divider_for_target_resolution(int src_width, int src_height,
												 int dst_width, int dst_height);

	static const int k_internal_channel_count;

	static const Rational k_pixel_aspect_square;
	static const Rational k_pixel_aspect_ntsc_standard;
	static const Rational k_pixel_aspect_ntsc_widescreen;
	static const Rational k_pixel_aspect_pal_standard;
	static const Rational k_pixel_aspect_pal_widescreen;
	static const Rational k_pixel_aspect1080_anamorphic;

	static const std::vector<Rational> k_supported_frame_rates;
	static const std::vector<Rational> k_standard_pixel_aspects;
	static const std::vector<int> k_supported_dividers;

	static const int k_hsv_channel_count = 3;
	static const int k_rgb_channel_count = 3;
	static const int k_rgba_channel_count = 4;

	/**
	 * @brief Convert Rational frame rate (i.e. flipped timebase) to a user-friendly string
	 */
	static std::string frame_rate_to_string(const Rational &frame_rate);

	static std::vector<std::string> get_standard_pixel_aspect_ratio_names();
	static std::string format_pixel_aspect_ratio_string(const std::string &format,
														const Rational &ratio);

	static int get_scaled_dimension(int dim, int divider);

	bool enabled() const
	{
		return enabled_;
	}

	void set_enabled(bool e)
	{
		enabled_ = e;
	}

	float x() const
	{
		return x_;
	}
	void set_x(float x)
	{
		x_ = x;
	}
	float y() const
	{
		return y_;
	}
	void set_y(float y)
	{
		y_ = y;
	}

	int stream_index() const
	{
		return stream_index_;
	}

	void set_stream_index(int s)
	{
		stream_index_ = s;
	}

	Type video_type() const
	{
		return video_type_;
	}

	void set_video_type(Type t)
	{
		video_type_ = t;
	}

	const Rational &frame_rate() const
	{
		return frame_rate_;
	}

	void set_frame_rate(const Rational &frame_rate)
	{
		frame_rate_ = frame_rate;
	}

	int64_t start_time() const
	{
		return start_time_;
	}

	void set_start_time(int64_t start_time)
	{
		start_time_ = start_time;
	}

	int64_t duration() const
	{
		return duration_;
	}

	void set_duration(int64_t duration)
	{
		duration_ = duration;
	}

	bool premultiplied_alpha() const
	{
		return premultiplied_alpha_;
	}

	void set_premultiplied_alpha(bool premultiplied_alpha)
	{
		premultiplied_alpha_ = premultiplied_alpha;
	}

	const std::string &colorspace() const
	{
		return colorspace_;
	}

	void set_colorspace(const std::string &c)
	{
		colorspace_ = c;
	}

	const ColorRange &color_range() const
	{
		return color_range_;
	}
	void set_color_range(const ColorRange &color_range)
	{
		color_range_ = color_range;
	}

	/**
	 * @brief Color primaries/transfer as reported by the media
	 *
	 * Raw FFmpeg AVColorPrimaries/AVColorTransferCharacteristic values
	 * (0 = unset, 2 = unspecified). Used to auto-detect the input
	 * colorspace when no explicit colorspace has been set.
	 */
	int color_primaries() const
	{
		return color_primaries_;
	}

	void set_color_primaries(int p)
	{
		color_primaries_ = p;
	}

	int color_transfer() const
	{
		return color_transfer_;
	}

	void set_color_transfer(int t)
	{
		color_transfer_ = t;
	}

	int64_t get_time_in_timebase_units(const Rational &time) const;

	void load(XmlStreamReader *reader);

	void save(XmlStreamWriter *writer) const;

private:
	void calculate_effective_size();

	void validate_pixel_aspect_ratio();

	void set_defaults_for_footage();

	void calculate_square_pixel_width();

	int width_;
	int height_;
	int depth_;
	Rational time_base_;

	PixelFormat format_;

	int channel_count_;

	Rational pixel_aspect_ratio_;

	Interlacing interlacing_;

	int divider_;

	// Cached values
	int effective_width_;
	int effective_height_;
	int effective_depth_;
	int par_width_;

	bool enabled_;
	int stream_index_;
	Type video_type_;
	Rational frame_rate_;
	int64_t start_time_;
	int64_t duration_;
	bool premultiplied_alpha_;
	std::string colorspace_;
	float x_;
	float y_;
	ColorRange color_range_;
	int color_primaries_ = 0;
	int color_transfer_ = 0;
};

}

#endif // OAK_VIDEOPARAMS_H
