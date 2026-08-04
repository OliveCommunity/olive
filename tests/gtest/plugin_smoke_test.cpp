/*
 * Oak Video Editor - Plugin Subsystem Smoke Tests
 * Copyright (C) 2025 Olive CE Team
 *
 * Smoke tests for the OFX plugin subsystem covering:
 * - Plugin host initialization
 * - Plugin job execution
 * - Concurrent image allocation
 *
 * Parameter instance, clip, image and renderer coverage lives in the
 * dedicated plugin_paraminstance_test.cpp, plugin_support_param_test.cpp,
 * plugin_support_clip_test.cpp, plugin_support_image_test.cpp and
 * plugin_renderer_readback_test.cpp files.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include <QCoreApplication>
#include <QThread>

// OFX headers
#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxhClip.h"
#include "ofxhImageEffect.h"

// Plugin support headers
#include "pluginSupport/olivehost.h"
#include "pluginSupport/oliveclip.h"
#include "pluginSupport/oliveplugininstance.h"
#include "pluginSupport/image.h"

// Node and render headers
#include "render/job/pluginjob.h"
#include "render/videoparams.h"
#include "render/texture.h"

#include "node/value.h"
#include "common/ffmpegutils.h"

namespace olive
{
namespace plugin
{
namespace test
{

// ============================================================================
// Helper Functions
// ============================================================================

static VideoParams make_video_params(int width, int height,
								   core::PixelFormat format, int channels,
								   bool premultiplied = false)
{
	VideoParams params;
	params.set_width(width);
	params.set_height(height);
	params.set_format(format);
	params.set_channel_count(channels);
	params.set_premultiplied_alpha(premultiplied);
	params.set_pixel_aspect_ratio(core::Rational(1, 1));
	params.set_frame_rate(core::Rational(30, 1));
	return params;
}

static TexturePtr create_test_texture(const VideoParams &params,
									uint8_t fill_value = 0x7f)
{
	AVFramePtr frame = create_av_frame_ptr();
	frame->set_format(FFmpegUtils::get_f_fmpeg_pixel_format(params.format(),
														params.channel_count()));
	frame->set_width(params.width());
	frame->set_height(params.height());
	if (frame->format() == fb_pix_fmt_none) {
		return nullptr;
	}
	if (frame->get_buffer(0) < 0) {
		return nullptr;
	}
	if (frame->make_writable() < 0) {
		return nullptr;
	}

	const int linesize = frame->linesize(0);
	for (int y = 0; y < frame->height(); ++y) {
		std::memset(frame->data(0) + y * linesize, fill_value, linesize);
	}

	TexturePtr texture = std::make_shared<Texture>(params);
	texture->handle_frame(frame);
	return texture;
}

// ============================================================================
// Smoke Test: Plugin Host
// ============================================================================

TEST(PluginSmoke, HostSingletonExists)
{
	// Verify that the plugin cache can be accessed
	auto *cache = OFX::Host::PluginCache::getPluginCache();
	EXPECT_NE(cache, nullptr);
}

TEST(PluginSmoke, LoadPluginsEmptyPathNoCrash)
{
	// Loading plugins from empty path should not crash
	EXPECT_NO_THROW({ load_plugins(QString()); });
}

TEST(PluginSmoke, LoadPluginsNonExistentPathNoCrash)
{
	// Loading plugins from non-existent path should not crash
	EXPECT_NO_THROW(
		{ load_plugins(QStringLiteral("/nonexistent/path/to/plugins")); });
}

// ============================================================================
// Smoke Test: Plugin Job
// ============================================================================

TEST(PluginSmokeJob, JobConstruction)
{
	NodeValueRow row;
	PluginJob job(nullptr, nullptr, row);

	EXPECT_EQ(job.plugin_instance(), nullptr);
	EXPECT_EQ(job.node(), nullptr);
	EXPECT_DOUBLE_EQ(job.time_seconds(), 0.0);
}

TEST(PluginSmokeJob, JobWithTime)
{
	NodeValueRow row;
	core::Rational time(5, 1); // 5 seconds
	PluginJob job(nullptr, nullptr, row, time);

	EXPECT_DOUBLE_EQ(job.time_seconds(), 5.0);
}

TEST(PluginSmokeJob, JobWithTextureValue)
{
	VideoParams params(64, 64, core::PixelFormat::u8, 4);
	TexturePtr tex = create_test_texture(params, 0x80);
	ASSERT_NE(tex, nullptr);

	NodeValueRow row;
	row.insert(QStringLiteral("source"), NodeValue(NodeValue::k_texture, tex));

	PluginJob job(nullptr, nullptr, row);

	// Job should have the values inserted
	EXPECT_FALSE(job.get_values().isEmpty());
}

// ============================================================================
// Smoke Test: Thread Safety
// ============================================================================

TEST(PluginSmokeThread, ConcurrentImageAllocation)
{
	const int num_threads = 4;
	const int num_allocs_per_thread = 10;

	std::vector<std::thread> threads;
	std::atomic<int> success_count{ 0 };

	for (int t = 0; t < num_threads; ++t) {
		threads.emplace_back([&success_count, t]() {
			for (int i = 0; i < num_allocs_per_thread; ++i) {
				OFX::Host::ImageEffect::ClipDescriptor desc(
					kOfxImageEffectOutputClipName);
				VideoParams params = make_video_params(
					32 + t, 32 + i, core::PixelFormat::u8, 4, false);
				OliveClipInstance clip(nullptr, desc, params);

				Image image(clip);
				OfxRectI bounds = { 0, 0, 32 + t, 32 + i };
				OfxRectI rod = bounds;
				image.allocate_from_params(params, bounds, rod, true);

				if (image.data() != nullptr && image.width() == 32 + t &&
					image.height() == 32 + i) {
					success_count++;
				}
			}
		});
	}

	for (auto &t : threads) {
		t.join();
	}

	EXPECT_EQ(success_count.load(), num_threads * num_allocs_per_thread);
}

} // namespace test
} // namespace plugin
} // namespace olive
