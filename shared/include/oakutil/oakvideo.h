/***

  Oak - Non-Linear Video Editor
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

#ifndef OAKUTIL_OAKVIDEO_H
#define OAKUTIL_OAKVIDEO_H

#include <string>

#include <olive/core/util/rational.h>

#include "oakengine/videoparams.h"

namespace oak
{

/**
 * @file oakvideo.h
 * @brief App-side value types replacing engine render/ value classes
 *
 * During the R8 C ABI migration the application may no longer include the
 * engine's internal C++ headers (engine/render/videoparams.h,
 * engine/render/colortransform.h). This header provides drop-in value-type
 * replacements built on the pure C ABI (oakengine/videoparams.h), following
 * the same wrapper philosophy as oakutil/oaknode.h.
 */

/**
 * @brief Value-type replacement for olive::VideoParams
 * (engine/render/videoparams.h), backed by the oak_video_params POD.
 *
 * Only the query surface the application actually uses is mirrored:
 * width()/height()/time_base()/frame_rate()/frame_rate_as_time_base()/
 * format()/pixel_aspect_ratio()/interlacing()/divider()/video_type()/
 * is_valid() and value comparison. Semantics match the engine class:
 *   - time_base() is the frame duration (frame rate flipped), exactly like
 *     VideoParams::time_base(); frame_rate() is its flip, and
 *     frame_rate_as_time_base() flips it back (VideoParams keeps frame_rate_
 *     = time_base_.flipped(), so frame_rate_as_time_base() == time_base()).
 *   - format() is an olive::core::PixelFormat::Format value carried as int
 *     (the POD representation; engine PixelFormat converts to int freely).
 *   - interlacing()/video_type()/color_range() are the engine enum ordinals
 *     documented on oak_video_params (0 = progressive / video / limited).
 *   - A default-constructed VideoParams mirrors the engine's default
 *     constructor: zero dimensions, null time base, square pixel aspect,
 *     divider 1, format "invalid" (-1), progressive, limited range.
 *
 * The engine-only fields the POD does not carry (enabled, stream index,
 * colorspace, offsets, cached effective sizes) are intentionally absent;
 * stream-enabled state must be queried through
 * oakengine_viewer_get_stream_enabled() instead.
 *
 * Copying copies the POD (plain value semantics). When a real engine-side
 * olive::VideoParams object must cross the boundary (e.g. the engine's
 * Current singleton), create_engine_params() wraps
 * oakengine_video_params_create(); the caller owns the result and must
 * release it with oakengine_video_params_free().
 */
class VideoParams
{
public:
	/// Mirror of the engine's default VideoParams() field values.
	VideoParams()
		: pod_{ 0, 0, 0, 0, -1 /* PixelFormat::invalid */, 1, 1, 0, 0, 1, 0,
				0 }
	{
	}

	/// Wrap an existing POD (e.g. from oakengine_viewer_get_video_params()).
	explicit VideoParams(const oak_video_params &pod) : pod_(pod) {}

	/// The backing POD (for C ABI interop).
	const oak_video_params &pod() const { return pod_; }

	int width() const { return pod_.width; }
	int height() const { return pod_.height; }

	/// Frame duration as a rational (VideoParams::time_base()).
	olive::core::Rational time_base() const
	{
		return olive::core::Rational(pod_.time_base_num, pod_.time_base_den);
	}

	/// Frame rate (time_base flipped; VideoParams::frame_rate()).
	olive::core::Rational frame_rate() const { return time_base().flipped(); }

	/// VideoParams::frame_rate_as_time_base() (frame_rate flipped back).
	olive::core::Rational frame_rate_as_time_base() const
	{
		return frame_rate().flipped();
	}

	/// olive::core::PixelFormat::Format value.
	int format() const { return pod_.format; }

	olive::core::Rational pixel_aspect_ratio() const
	{
		return olive::core::Rational(pod_.pixel_aspect_num,
									 pod_.pixel_aspect_den);
	}

	/// olive::VideoParams::Interlacing ordinal (0 = none/progressive).
	int interlacing() const { return pod_.interlacing; }

	int divider() const { return pod_.divider; }

	/// olive::VideoParams::Type ordinal (0 = video). Only populated by the
	/// viewer family; see oak_video_params.
	int video_type() const { return pod_.video_type; }

	/// olive::VideoParams::ColorRange ordinal (0 = limited).
	int color_range() const { return pod_.color_range; }

	/// VideoParams::is_valid() through the C ABI.
	bool is_valid() const
	{
		return oakengine_video_params_is_valid(&pod_) != 0;
	}

	bool operator==(const VideoParams &o) const
	{
		return oakengine_video_params_equal(&pod_, &o.pod_) != 0;
	}
	bool operator!=(const VideoParams &o) const { return !(*this == o); }

	/**
	 * @brief Construct an engine-side olive::VideoParams from this value
	 * (oakengine_video_params_create()). The returned pointer is OWNED by
	 * the caller and must be released with oakengine_video_params_free().
	 */
	void *create_engine_params() const
	{
		return oakengine_video_params_create(&pod_);
	}

private:
	oak_video_params pod_;
};

/**
 * @brief Value-type replacement for olive::ColorTransform
 * (engine/render/colortransform.h).
 *
 * The engine header is fully inline, so this is a verbatim semantic copy:
 * either a display transform (display/view/look triple, is_display() true)
 * or a plain output colorspace (is_display() false, output()/display()
 * both return it).
 */
class ColorTransform
{
public:
	ColorTransform() = default;

	ColorTransform(const std::string &output) : output_(output) {}

	ColorTransform(const std::string &display, const std::string &view,
				   const std::string &look)
		: output_(display), is_display_(true), view_(view), look_(look)
	{
	}

	bool is_display() const { return is_display_; }

	const std::string &display() const { return output_; }

	const std::string &output() const { return output_; }

	const std::string &view() const { return view_; }

	const std::string &look() const { return look_; }

private:
	std::string output_;

	bool is_display_ = false;
	std::string view_;
	std::string look_;
};

} // namespace oak

#endif // OAKUTIL_OAKVIDEO_H
