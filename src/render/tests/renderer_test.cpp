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

#include "render/renderer.h"

#include <vector>

#include <gtest/gtest.h>

#include "render/cache.h" /* oakrender_debug_alive_count */

namespace
{

oakrender_video_params make_params()
{
	oakrender_video_params p = {};
	p.width = 1920;
	p.height = 1080;
	p.time_base_num = 1001;
	p.time_base_den = 30000;
	p.format = 0; // olive::PixelFormat::u8
	p.pixel_aspect_num = 1;
	p.pixel_aspect_den = 1;
	p.interlacing = 0;
	p.color_range = 0;
	p.divider = 1;
	return p;
}

} // namespace

/* ---- Frame handle (CPU side, no GL required) ----------------------------- */

TEST(OakRenderFrameTest, CreateRetainFree)
{
	const int alive_before = oakrender_debug_alive_count();

	OakCodecFrame frame = oakrender_codec_frame_create();
	ASSERT_NE(frame.ctx, nullptr);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before + 1);

	EXPECT_EQ(oakrender_codec_frame_is_allocated(frame), 0);
	EXPECT_EQ(oakrender_codec_frame_data(frame), nullptr);
	EXPECT_EQ(oakrender_codec_frame_const_data(frame), nullptr);
	EXPECT_EQ(oakrender_codec_frame_linesize_bytes(frame), 0);

	// retain returns the same handle and pairs with exactly one free
	OakCodecFrame retained = oakrender_codec_frame_retain(frame);
	EXPECT_EQ(retained.ctx, frame.ctx);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before + 1);
	oakrender_codec_frame_free(&retained);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before + 1);

	oakrender_codec_frame_free(&frame);
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);

	// Empty-handle / NULL no-ops
	EXPECT_EQ(oakrender_codec_frame_retain(OakCodecFrame{}).ctx, nullptr);
	oakrender_codec_frame_free(nullptr);
	oakrender_codec_frame_free(&frame); // already cleared
	EXPECT_EQ(oakrender_debug_alive_count(), alive_before);
}

TEST(OakRenderFrameTest, SetGetParams)
{
	OakCodecFrame frame = oakrender_codec_frame_create();
	ASSERT_NE(frame.ctx, nullptr);

	const oakrender_video_params in = make_params();
	EXPECT_EQ(oakrender_codec_frame_set_video_params(frame, &in),
			  OAKRENDER_OK);

	// The transitional codec Frame (M5 pending) does not store params,
	// so only the call contract is asserted here; the round-trip is
	// covered by the oakcodec wave.
	oakrender_video_params out = {};
	EXPECT_EQ(oakrender_codec_frame_get_params(frame, &out), OAKRENDER_OK);

	EXPECT_EQ(oakrender_codec_frame_set_video_params(OakCodecFrame{}, &in),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_codec_frame_set_video_params(frame, nullptr),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_codec_frame_get_params(OakCodecFrame{}, &out),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_codec_frame_get_params(frame, nullptr),
			  OAKRENDER_E_INVALID);

	oakrender_codec_frame_free(&frame);
}

TEST(OakRenderFrameTest, AllocateOnTransitionalFrameFails)
{
	// codec/frame.h is still the transition stub (oakcodec, M5): its
	// allocate() reports failure, which the ABI surfaces as E_FAILED.
	OakCodecFrame frame = oakrender_codec_frame_create();
	ASSERT_NE(frame.ctx, nullptr);
	EXPECT_EQ(oakrender_codec_frame_allocate(frame), OAKRENDER_E_FAILED);
	EXPECT_EQ(oakrender_codec_frame_allocate(OakCodecFrame{}),
			  OAKRENDER_E_INVALID);
	oakrender_codec_frame_free(&frame);
}

/* ---- Renderer / texture: empty-handle error paths (no GL) ----------------- */

TEST(OakRenderDisplayTest, NullArgumentErrorPaths)
{
	const oakrender_video_params params = make_params();
	oakrender_video_params out = {};
	char pixel[4] = {};

	EXPECT_EQ(oakrender_display_texture_create(OakRenderRenderer{}, &params,
											   nullptr, 0)
				  .ctx,
			  nullptr);
	EXPECT_EQ(oakrender_display_texture_retain(OakRenderTexture{}).ctx,
			  nullptr);
	oakrender_display_texture_free(nullptr);
	EXPECT_EQ(oakrender_display_texture_upload(OakRenderTexture{}, pixel, 4),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_display_texture_download(OakRenderTexture{}, pixel, 4),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_display_texture_get_params(OakRenderTexture{}, &out),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_display_texture_id(OakRenderTexture{}), 0);

	EXPECT_EQ(oakrender_display_renderer_init(OakRenderRenderer{}, nullptr),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_display_renderer_is_open_gl(OakRenderRenderer{}), 0);
	EXPECT_EQ(oakrender_display_renderer_is_vulkan(OakRenderRenderer{}), 0);
	oakrender_display_renderer_destroy(nullptr);

	oakrender_color_transform_job job = {};
	EXPECT_EQ(oakrender_display_renderer_blit_color_managed(
				  OakRenderRenderer{}, &job, OakRenderTexture{}, &params),
			  OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_display_renderer_download_from_texture(
				  OakRenderRenderer{}, 0, &params, pixel, 4),
			  OAKRENDER_E_INVALID);
}

TEST(OakRenderDisplayTest, CreateDynamicRejectsBadInput)
{
	EXPECT_EQ(oakrender_display_renderer_create_dynamic(nullptr).ctx, nullptr);
	EXPECT_EQ(oakrender_display_renderer_create_dynamic("").ctx, nullptr);
}

TEST(OakRenderDisplayTest, CreateOpenGLRenderer)
{
	// Construction must not require a GL context; init() would.
	OakRenderRenderer r = oakrender_display_renderer_create_opengl();
	ASSERT_NE(r.ctx, nullptr);
	EXPECT_EQ(oakrender_display_renderer_is_open_gl(r), 1);
	EXPECT_EQ(oakrender_display_renderer_is_vulkan(r), 0);
	oakrender_display_renderer_destroy(&r);
}

TEST(OakRenderDisplayTest, InitAndTextureLifecycle)
{
	GTEST_SKIP() << "GL-dependent: renderer init and GPU textures need a "
					"GL context (M7 §4 skip pattern)";
}

TEST(OakRenderDisplayTest, BlitColorManaged)
{
	GTEST_SKIP() << "GL-dependent: color-managed blit needs an initialized "
					"renderer (M7 §4 skip pattern)";
}

/* ---- Backend management --------------------------------------------------- */

TEST(OakRenderBackendTest, CountAndIds)
{
	EXPECT_EQ(oakrender_backend_count(), 4);

	char buf[64];
	ASSERT_GT(oakrender_backend_id_at(0, buf, sizeof(buf)), 1);
	EXPECT_STREQ(buf, "opengl");
	ASSERT_GT(oakrender_backend_id_at(1, buf, sizeof(buf)), 1);
	EXPECT_STREQ(buf, "vulkan");
	ASSERT_GT(oakrender_backend_id_at(3, buf, sizeof(buf)), 1);
	EXPECT_STREQ(buf, "dummy");

	// Two-stage query
	const int required = oakrender_backend_id_at(0, nullptr, 0);
	EXPECT_EQ(required, 7); // "opengl" + NUL
	EXPECT_EQ(oakrender_backend_id_at(0, buf, 3), required); // too small

	EXPECT_EQ(oakrender_backend_id_at(-1, buf, sizeof(buf)),
			  OAKRENDER_E_NOT_FOUND);
	EXPECT_EQ(oakrender_backend_id_at(4, buf, sizeof(buf)),
			  OAKRENDER_E_NOT_FOUND);
}

TEST(OakRenderBackendTest, SetAndCurrentBackend)
{
	EXPECT_EQ(oakrender_set_backend("vulkan"), OAKRENDER_OK);

	char buf[64];
	ASSERT_GT(oakrender_current_backend(buf, sizeof(buf)), 1);
	EXPECT_STREQ(buf, "vulkan");

	EXPECT_EQ(oakrender_set_backend("OpenGL"), OAKRENDER_OK);
	ASSERT_GT(oakrender_current_backend(buf, sizeof(buf)), 1);
	EXPECT_STREQ(buf, "opengl");

	EXPECT_EQ(oakrender_set_backend(nullptr), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_set_backend("metal"), OAKRENDER_E_INVALID);
}
