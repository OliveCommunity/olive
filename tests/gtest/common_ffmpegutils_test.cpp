#include <gtest/gtest.h>

#include "common/ffmpegutils.h"

using namespace olive;

TEST(CommonFFmpegUtils, GetNativeSampleFormatMapsCorrectly)
{
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_U8),
			  SampleFormat::U8);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_S16),
			  SampleFormat::S16);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_S32),
			  SampleFormat::S32);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_S64),
			  SampleFormat::S64);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_FLT),
			  SampleFormat::F32);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_DBL),
			  SampleFormat::F64);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_U8P),
			  SampleFormat::U8P);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_S16P),
			  SampleFormat::S16P);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_S32P),
			  SampleFormat::S32P);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_S64P),
			  SampleFormat::S64P);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_FLTP),
			  SampleFormat::F32P);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_DBLP),
			  SampleFormat::F64P);
	EXPECT_EQ(FFmpegUtils::GetNativeSampleFormat(AV_SAMPLE_FMT_NONE),
			  SampleFormat::INVALID);
}

TEST(CommonFFmpegUtils, GetFFmpegSampleFormatMapsCorrectly)
{
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::U8),
			  AV_SAMPLE_FMT_U8);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::S16),
			  AV_SAMPLE_FMT_S16);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::S32),
			  AV_SAMPLE_FMT_S32);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::S64),
			  AV_SAMPLE_FMT_S64);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::F32),
			  AV_SAMPLE_FMT_FLT);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::F64),
			  AV_SAMPLE_FMT_DBL);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::U8P),
			  AV_SAMPLE_FMT_U8P);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::S16P),
			  AV_SAMPLE_FMT_S16P);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::S32P),
			  AV_SAMPLE_FMT_S32P);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::S64P),
			  AV_SAMPLE_FMT_S64P);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::F32P),
			  AV_SAMPLE_FMT_FLTP);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::F64P),
			  AV_SAMPLE_FMT_DBLP);
	EXPECT_EQ(FFmpegUtils::GetFFmpegSampleFormat(SampleFormat::INVALID),
			  AV_SAMPLE_FMT_NONE);
}

TEST(CommonFFmpegUtils, GetSwsColorspaceFromAVColorSpace)
{
	EXPECT_EQ(FFmpegUtils::GetSwsColorspaceFromAVColorSpace(AVCOL_SPC_BT709),
			  SWS_CS_ITU709);
	EXPECT_EQ(FFmpegUtils::GetSwsColorspaceFromAVColorSpace(AVCOL_SPC_FCC),
			  SWS_CS_FCC);
	EXPECT_EQ(FFmpegUtils::GetSwsColorspaceFromAVColorSpace(AVCOL_SPC_BT470BG),
			  SWS_CS_ITU624);
	EXPECT_EQ(
		FFmpegUtils::GetSwsColorspaceFromAVColorSpace(AVCOL_SPC_SMPTE170M),
		SWS_CS_SMPTE170M);
	EXPECT_EQ(
		FFmpegUtils::GetSwsColorspaceFromAVColorSpace(AVCOL_SPC_SMPTE240M),
		SWS_CS_SMPTE240M);
	EXPECT_EQ(
		FFmpegUtils::GetSwsColorspaceFromAVColorSpace(AVCOL_SPC_BT2020_NCL),
		SWS_CS_BT2020);
	EXPECT_EQ(
		FFmpegUtils::GetSwsColorspaceFromAVColorSpace(AVCOL_SPC_UNSPECIFIED),
		SWS_CS_DEFAULT);
}

TEST(CommonFFmpegUtils, ConvertJPEGSpaceToRegularSpace)
{
	EXPECT_EQ(FFmpegUtils::ConvertJPEGSpaceToRegularSpace(AV_PIX_FMT_YUVJ420P),
			  AV_PIX_FMT_YUV420P);
	EXPECT_EQ(FFmpegUtils::ConvertJPEGSpaceToRegularSpace(AV_PIX_FMT_YUVJ422P),
			  AV_PIX_FMT_YUV422P);
	EXPECT_EQ(FFmpegUtils::ConvertJPEGSpaceToRegularSpace(AV_PIX_FMT_YUVJ444P),
			  AV_PIX_FMT_YUV444P);
	EXPECT_EQ(FFmpegUtils::ConvertJPEGSpaceToRegularSpace(AV_PIX_FMT_YUVJ440P),
			  AV_PIX_FMT_YUV440P);
	EXPECT_EQ(FFmpegUtils::ConvertJPEGSpaceToRegularSpace(AV_PIX_FMT_YUVJ411P),
			  AV_PIX_FMT_YUV411P);
	EXPECT_EQ(FFmpegUtils::ConvertJPEGSpaceToRegularSpace(AV_PIX_FMT_YUV420P),
			  AV_PIX_FMT_YUV420P);
}

TEST(CommonFFmpegUtils, GetCompatiblePixelFormatNative)
{
	EXPECT_EQ(FFmpegUtils::GetCompatiblePixelFormat(PixelFormat::U8),
			  PixelFormat::U8);
	EXPECT_EQ(FFmpegUtils::GetCompatiblePixelFormat(PixelFormat::U10),
			  PixelFormat::U8);
	EXPECT_EQ(FFmpegUtils::GetCompatiblePixelFormat(PixelFormat::U16),
			  PixelFormat::U16);
	EXPECT_EQ(FFmpegUtils::GetCompatiblePixelFormat(PixelFormat::F16),
			  PixelFormat::U16);
	EXPECT_EQ(FFmpegUtils::GetCompatiblePixelFormat(PixelFormat::F32),
			  PixelFormat::U16);
	EXPECT_EQ(FFmpegUtils::GetCompatiblePixelFormat(PixelFormat::INVALID),
			  PixelFormat::INVALID);
}

TEST(CommonFFmpegUtils, GetFFmpegPixelFormat)
{
	EXPECT_EQ(FFmpegUtils::GetFFmpegPixelFormat(PixelFormat::U8,
												VideoParams::kRGBChannelCount),
			  AV_PIX_FMT_RGB24);
	EXPECT_EQ(FFmpegUtils::GetFFmpegPixelFormat(PixelFormat::U16,
												VideoParams::kRGBChannelCount),
			  AV_PIX_FMT_RGB48);
	EXPECT_EQ(FFmpegUtils::GetFFmpegPixelFormat(PixelFormat::F32,
												VideoParams::kRGBChannelCount),
			  AV_PIX_FMT_RGBF32);
	EXPECT_EQ(FFmpegUtils::GetFFmpegPixelFormat(PixelFormat::U8,
												VideoParams::kRGBAChannelCount),
			  AV_PIX_FMT_RGBA);
	EXPECT_EQ(FFmpegUtils::GetFFmpegPixelFormat(PixelFormat::U16,
												VideoParams::kRGBAChannelCount),
			  AV_PIX_FMT_RGBA64);
	EXPECT_EQ(FFmpegUtils::GetFFmpegPixelFormat(PixelFormat::F32,
												VideoParams::kRGBAChannelCount),
			  AV_PIX_FMT_RGBAF32);
	EXPECT_EQ(FFmpegUtils::GetFFmpegPixelFormat(PixelFormat::INVALID, 0),
			  AV_PIX_FMT_NONE);
}

TEST(CommonFFmpegUtils, GetCompatiblePixelFormatAV)
{
	AVPixelFormat fmt =
		FFmpegUtils::GetCompatiblePixelFormat(AV_PIX_FMT_YUV420P);
	EXPECT_NE(fmt, AV_PIX_FMT_NONE);

	fmt = FFmpegUtils::GetCompatiblePixelFormat(AV_PIX_FMT_YUV420P,
												PixelFormat::U8);
	EXPECT_EQ(fmt, AV_PIX_FMT_RGBA);
}
