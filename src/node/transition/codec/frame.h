#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include "videoparams.h"
#include "olive/core/util/color.h"
namespace olive {
using core::Color;
class Frame;
using FramePtr = std::shared_ptr<Frame>;
class Frame {
public:
	bool is_allocated() const { return false; }
	void allocate() {}
	char *data() { return nullptr; }
	int64_t allocated_size() const { return 0; }
	int width() const { return 0; }
	int height() const { return 0; }
	int linesize_bytes() const { return 0; }
	int linesize_pixels() const { return 0; }
	void set_pixel(int, int, const Color &) {}
	const VideoParams &video_params() const
	{
		static VideoParams p;
		return p;
	}
	void set_video_params(const VideoParams &) {}
};
}
