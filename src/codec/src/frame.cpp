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

#include "frame.h"

#include <cstring>
#include <iostream>

#include <OpenImageIO/imagebuf.h>

#include "common/oiioutils.h"
#include "framemanager.h"
#include "oiioframebridge.h"

namespace olive
{

Frame::Frame()
	: params_(oakcommon_videoparams_init())
	, data_(nullptr)
	, data_size_(0)
	, timestamp_(0)
	, linesize_(0)
	, linesize_pixels_(0)
{
}

Frame::~Frame()
{
	destroy();

	if (params_.ctx && params_.release) {
		params_.release(params_.ctx);
		params_.ctx = nullptr;
	}
}

FramePtr Frame::create()
{
	return std::make_shared<Frame>();
}

OakVideoParams Frame::video_params() const
{
	OakVideoParams copy = params_;
	if (copy.ctx && copy.addref) {
		copy.addref(copy.ctx);
	}
	return copy;
}

void Frame::set_video_params(const OakVideoParams &params)
{
	if (params_.ctx && params_.release) {
		params_.release(params_.ctx);
	}

	params_ = params;

	if (params_.ctx && params_.addref) {
		params_.addref(params_.ctx);
	}

	linesize_ = generate_linesize_bytes(width(), format(), channel_count());

	int bpp = bytes_per_pixel();
	linesize_pixels_ = bpp > 0 ? linesize_ / bpp : 0;
}

FramePtr Frame::interlace(FramePtr top, FramePtr bottom)
{
	OakVideoParams top_params = top->video_params();
	OakVideoParams bottom_params = bottom->video_params();

	int equal = 0;
	oakcommon_videoparams_equals(top_params, bottom_params, &equal);

	oakcommon_videoparams_free(&bottom_params);

	if (!equal) {
		fprintf(stderr,
				"Tried to interlace two frames that had incompatible parameters\n");
		oakcommon_videoparams_free(&top_params);
		return nullptr;
	}

	FramePtr interlaced = Frame::create();
	interlaced->set_video_params(top_params);
	oakcommon_videoparams_free(&top_params);
	interlaced->allocate();

	int linesize = interlaced->linesize_bytes();

	for (int i = 0; i < interlaced->height(); i++) {
		FramePtr which = (i % 2 == 0) ? top : bottom;

		memcpy(interlaced->data() + i * linesize,
			   which->const_data() + i * linesize, linesize);
	}

	return interlaced;
}

int Frame::generate_linesize_bytes(int width, int format, int channel_count)
{
	// Align to 32 bytes (not sure if this is necessary?)
	int bytes_per_pixel = oakcommon_videoparams_static_get_bytes_per_pixel(
		static_cast<OakPixelFormat>(format), channel_count);
	return bytes_per_pixel * ((width + 31) & ~31);
}

core::Color Frame::get_pixel(int x, int y) const
{
	if (!contains_pixel(x, y)) {
		return core::Color();
	}

	int byte_offset = y * linesize_bytes() + x * bytes_per_pixel();

	return core::Color(reinterpret_cast<const char *>(data_ + byte_offset),
				   core_format(), channel_count());
}

bool Frame::contains_pixel(int x, int y) const
{
	return (is_allocated() && x >= 0 && x < width() && y >= 0 && y < height());
}

void Frame::set_pixel(int x, int y, const core::Color &c)
{
	if (!contains_pixel(x, y)) {
		return;
	}

	int byte_offset = y * linesize_bytes() + x * bytes_per_pixel();

	c.to_data(reinterpret_cast<char *>(data_ + byte_offset), core_format(),
		  channel_count());
}

bool Frame::allocate()
{
	// Assume this frame is intended to be a video frame
	int is_valid = 0;
	oakcommon_videoparams_get_is_valid(params_, &is_valid);
	if (!is_valid) {
		std::cerr << "Tried to allocate a frame with invalid parameters";
		return false;
	}

	if (is_allocated()) {
		// Already allocated
		return true;
	}

	data_size_ = linesize_ * height();
	data_ = FrameManager::allocate(data_size_);

	return true;
}

void Frame::destroy()
{
	if (is_allocated()) {
		FrameManager::deallocate(data_size_, data_);

		data_size_ = 0;
		data_ = nullptr;
	}
}

/**
 * @brief OIIO base type for a native pixel format, via the oakcommon C ABI
 *
 * The OakOIIOUtils object is stateless; a process-lifetime handle is kept
 * here to avoid re-boxing it on every conversion.
 */
static OIIO::TypeDesc::BASETYPE oiio_base_type_for_format(int format)
{
	static OakOIIOUtils utils = oakcommon_oiioutils_init();

	int base_type = 0; // OIIO::TypeDesc::UNKNOWN
	oakcommon_oiioutils_get_oiio_base_type_from_format(utils, format,
												   &base_type);
	return static_cast<OIIO::TypeDesc::BASETYPE>(base_type);
}

FramePtr Frame::convert(int format) const
{
	// Create new params with destination format
	OakVideoParams params = video_params();
	oakcommon_videoparams_set_format(params, format);

	// Create new frame
	FramePtr converted = Frame::create();
	converted->set_video_params(params);
	oakcommon_videoparams_free(&params);
	converted->set_timestamp(timestamp_);
	converted->allocate();

	// Do the conversion through OIIO for convenience
	OIIO::ImageBuf src(OIIO::ImageSpec(width(), height(), channel_count(),
								   oiio_base_type_for_format(this->format())));

	oiio_frame_to_buffer(const_data(), linesize_bytes(), &src);

	OIIO::ImageBuf dst(OIIO::ImageSpec(converted->width(), converted->height(),
								   channel_count(),
								   oiio_base_type_for_format(format)));

	if (dst.copy_pixels(src)) {
		oiio_buffer_to_frame(&dst, converted->data(),
						 converted->linesize_bytes());
		return converted;
	} else {
		return nullptr;
	}
}

}
