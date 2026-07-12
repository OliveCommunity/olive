#include <gtest/gtest.h>

#include "codec/frame.h"

TEST(CodecFrame, DefaultState)
{
	olive::Frame frame;
	EXPECT_EQ(frame.width(), 0);
	EXPECT_EQ(frame.height(), 0);
	EXPECT_EQ(frame.format(), olive::core::PixelFormat::INVALID);
	EXPECT_FALSE(frame.is_allocated());
	EXPECT_EQ(frame.data(), nullptr);
}

TEST(CodecFrame, CreateAllocatesForParams)
{
	olive::VideoParams params(64, 32, olive::core::PixelFormat::U8,
							  olive::VideoParams::kRGBAChannelCount);

	olive::FramePtr frame = olive::Frame::Create();
	frame->set_video_params(params);
	frame->allocate();

	EXPECT_TRUE(frame->is_allocated());
	EXPECT_NE(frame->data(), nullptr);
	EXPECT_EQ(frame->width(), 64);
	EXPECT_EQ(frame->height(), 32);
	EXPECT_EQ(frame->format(), olive::core::PixelFormat::U8);
}

TEST(CodecFrame, AllocateMatchesLineSize)
{
	olive::VideoParams params(64, 32, olive::core::PixelFormat::U8,
							  olive::VideoParams::kRGBAChannelCount);

	olive::FramePtr frame = olive::Frame::Create();
	frame->set_video_params(params);
	frame->allocate();

	EXPECT_EQ(frame->linesize_bytes(), 64 * 4);
	EXPECT_EQ(frame->allocated_size(), 64 * 4 * 32);
}

TEST(CodecFrame, DestroyDeallocatesData)
{
	olive::FramePtr frame = olive::Frame::Create();
	frame->set_video_params(olive::VideoParams(
		8, 8, olive::core::PixelFormat::U8,
		olive::VideoParams::kRGBAChannelCount));
	frame->allocate();
	EXPECT_TRUE(frame->is_allocated());

	frame->destroy();
	EXPECT_FALSE(frame->is_allocated());
	EXPECT_EQ(frame->data(), nullptr);
}
