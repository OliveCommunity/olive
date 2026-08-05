#pragma once
// Transitional: engine/codec/frame.h de-Qt target form (oakcodec contract,
// M5). Covers the surface the render core uses. Based on
// src/node/transition/codec/frame.h, extended with the frame-allocation and
// linesize API renderprocessor/renderworkerpool call.
#include <cstddef>
#include <cstdint>
#include <memory>

#include "videoparams.h"
#include "olive/core/util/color.h"
#include "olive/core/util/rational.h"

namespace olive {

using core::Color;
using core::Rational;

class Frame;
using FramePtr = std::shared_ptr<Frame>;

class Frame {
public:
	static FramePtr create() { return std::make_shared<Frame>(); }

	static int generate_linesize_bytes(int width, PixelFormat format,
									   int channel_count)
	{
		(void) width; (void) format; (void) channel_count;
		return 0;
	}

	bool is_allocated() const { return false; }
	bool allocate() { return false; }
	char *data() { return nullptr; }
	const char *const_data() const { return nullptr; }
	int64_t allocated_size() const { return 0; }
	int width() const { return 0; }
	int height() const { return 0; }
	PixelFormat format() const { return PixelFormat::invalid; }
	int channel_count() const { return 0; }
	int linesize_bytes() const { return 0; }
	int linesize_pixels() const { return 0; }
	void set_pixel(int, int, const Color &) {}

	const Rational &timestamp() const
	{
		static const Rational r;
		return r;
	}
	void set_timestamp(const Rational &) {}

	const VideoParams &video_params() const
	{
		static VideoParams p;
		return p;
	}
	void set_video_params(const VideoParams &) {}
};

}
