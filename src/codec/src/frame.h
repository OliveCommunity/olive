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

#ifndef OAK_FRAME_H
#define OAK_FRAME_H

#include <memory>

#include <olive/core/render/pixelformat.h>
#include <olive/core/util/color.h>
#include <olive/core/util/rational.h>

#include "common/videoparams.h"

namespace olive
{

class Frame;
using FramePtr = std::shared_ptr<Frame>;

/**
 * @brief Video frame data or audio sample data from a Decoder
 *
 * Qt-free (M5). The parameter set is held as an OakVideoParams handle
 * (oakcommon C ABI); every access goes through the oakcommon_videoparams_*
 * functions. Pixel formats cross the boundary as OakPixelFormat int codes
 * (numerically identical to olive::core::PixelFormat::Format).
 */
class Frame {
public:
	Frame();

	~Frame();

	Frame(const Frame &) = delete;

	static FramePtr create();

	/**
	 * @brief Return a copy of the frame's parameter handle
	 *
	 * The returned handle has had its reference count incremented; the
	 * caller must release it (oakcommon_videoparams_free()).
	 */
	OakVideoParams video_params() const;
	void set_video_params(const OakVideoParams &params);

	static FramePtr interlace(FramePtr top, FramePtr bottom);

	static int generate_linesize_bytes(int width, int format,
								   int channel_count);

	int linesize_pixels() const
	{
		return linesize_pixels_;
	}

	int linesize_bytes() const
	{
		return linesize_;
	}

	int width() const
	{
		int width = 0;
		oakcommon_videoparams_get_effective_width(params_, &width);
		return width;
	}

	int height() const
	{
		int height = 0;
		oakcommon_videoparams_get_effective_height(params_, &height);
		return height;
	}

	/**
	 * @brief Pixel format as an OakPixelFormat value
	 *        (== olive::core::PixelFormat::Format ordinal).
	 */
	int format() const
	{
		int format = OAKCOMMON_PIXEL_FORMAT_INVALID;
		oakcommon_videoparams_get_format(params_, &format);
		return format;
	}

	int channel_count() const
	{
		int count = 0;
		oakcommon_videoparams_get_channel_count(params_, &count);
		return count;
	}

	core::Color get_pixel(int x, int y) const;
	bool contains_pixel(int x, int y) const;
	void set_pixel(int x, int y, const core::Color &c);

	/**
	 * @brief Get frame's timestamp.
	 *
	 * This timestamp is always a Rational that will equate to the time in seconds.
	 */
	const core::Rational &timestamp() const
	{
		return timestamp_;
	}

	void set_timestamp(const core::Rational &timestamp)
	{
		timestamp_ = timestamp;
	}

	/**
	 * @brief Get the data buffer of this frame
	 */
	char *data()
	{
		return data_;
	}

	/**
	 * @brief Get the const data buffer of this frame
	 */
	const char *const_data() const
	{
		return data_;
	}

	/**
	 * @brief Allocate memory buffer to store data based on parameters
	 *
	 * For video frames, the width(), height(), and format() must be set for this function to work.
	 *
	 * If a memory buffer has been previously allocated without destroying, this function will destroy it.
	 */
	bool allocate();

	/**
	 * @brief Return whether the frame is allocated or not
	 */
	bool is_allocated() const
	{
		return data_;
	}

	/**
	 * @brief Destroy a memory buffer allocated with allocate()
	 */
	void destroy();

	/**
	 * @brief Returns the size of the array returned in data() in bytes
	 *
	 * Returns 0 if nothing is allocated.
	 */
	int allocated_size() const
	{
		return data_size_;
	}

	FramePtr convert(int format) const;

private:
	int bytes_per_pixel() const
	{
		int bytes = 0;
		oakcommon_videoparams_get_bytes_per_pixel(params_, &bytes);
		return bytes;
	}

	core::PixelFormat core_format() const
	{
		return core::PixelFormat(
			static_cast<core::PixelFormat::Format>(format()));
	}

	OakVideoParams params_;

	char *data_;
	int data_size_;

	core::Rational timestamp_;

	int linesize_;

	int linesize_pixels_;
};

}


#endif // OAK_FRAME_H
