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

#include "render/manager.h"

#include <vector>

#include <gtest/gtest.h>

#include "node/factory.h"
#include "node/node.h"

namespace
{

void noop_frame_ready(OakCodecFrame frame, int64_t ts, void *userdata)
{
	(void) frame;
	(void) ts;
	(void) userdata;
}

} // namespace

/* ---- Manager lifecycle ----------------------------------------------------
 *
 * RenderManager's constructor reads the not-yet-split config module
 * (OAK_CONFIG) and spawns backend/worker machinery whose symbols dangle
 * in the standalone build (-undefined dynamic_lookup), so init/shutdown
 * and everything requiring a live manager cannot run here (M7 §4). The
 * no-manager error paths below are the runnable half.
 */

TEST(OakRenderManagerTest, InitShutdown)
{
	GTEST_SKIP() << "RenderManager pulls dangling config/codec/worker "
					"symbols in the standalone build (M7 §4)";
}

TEST(OakRenderManagerTest, RequestFrameRequiresManager)
{
	ASSERT_EQ(oaknode_factory_initialize(), OAKNODE_OK);
	OakNodeNode viewer = oaknode_factory_create_from_id(
		"org.olivevideoeditor.Olive.vieweroutput");
	ASSERT_NE(viewer.ctx, nullptr);

	// No oakrender_manager_init() in this process: E_STATE
	EXPECT_EQ(oakrender_request_frame(viewer, 0, noop_frame_ready, nullptr),
			  int64_t(OAKRENDER_E_STATE));

	oaknode_node_free(&viewer);
	oaknode_factory_destroy();
}

TEST(OakRenderManagerTest, RequestFrameInvalidArgs)
{
	EXPECT_EQ(oakrender_request_frame(OakNodeNode{}, 0, noop_frame_ready,
									  nullptr),
			  int64_t(OAKRENDER_E_INVALID));

	ASSERT_EQ(oaknode_factory_initialize(), OAKNODE_OK);
	OakNodeNode viewer = oaknode_factory_create_from_id(
		"org.olivevideoeditor.Olive.vieweroutput");
	ASSERT_NE(viewer.ctx, nullptr);
	EXPECT_EQ(oakrender_request_frame(viewer, 0, nullptr, nullptr),
			  int64_t(OAKRENDER_E_INVALID));
	oaknode_node_free(&viewer);
	oaknode_factory_destroy();
}

TEST(OakRenderManagerTest, RequestFrameWithManager)
{
	GTEST_SKIP() << "A live frame request needs the render manager plus "
					"codec/worker symbols that dangle standalone (M7 §4)";
}

TEST(OakRenderManagerTest, CancelUnknownRequest)
{
	EXPECT_EQ(oakrender_cancel_request(-1), OAKRENDER_E_NOT_FOUND);
	EXPECT_EQ(oakrender_cancel_request(424242), OAKRENDER_E_NOT_FOUND);
}

TEST(OakRenderManagerTest, CacherSettersRequireManager)
{
	EXPECT_EQ(oakrender_set_cacher_multicam(OakNodeNode{}), OAKRENDER_E_STATE);
	EXPECT_EQ(oakrender_set_display_color_processor(OakColorProcessor{}),
			  OAKRENDER_E_STATE);
}

/* ---- Disk cache ----------------------------------------------------------- */

TEST(OakRenderDiskCacheTest, PathTwoStage)
{
	const int required = oakrender_disk_cache_path(nullptr, 0);
	ASSERT_GT(required, 1);

	std::vector<char> buf(static_cast<size_t>(required));
	EXPECT_EQ(oakrender_disk_cache_path(buf.data(), required), required);
	EXPECT_STRNE(buf.data(), "");

	// Too-small buffer: size query only, no write
	std::vector<char> small(2, 0);
	EXPECT_EQ(oakrender_disk_cache_path(small.data(), 2), required);
}

TEST(OakRenderDiskCacheTest, SizeAndClear)
{
	// Lazily creates the DiskManager singleton; consumption is
	// non-negative by definition.
	EXPECT_GE(oakrender_disk_cache_size(), int64_t(0));

	// Clearing an existing (possibly empty) default folder succeeds.
	EXPECT_EQ(oakrender_disk_cache_clear(), OAKRENDER_OK);
}

TEST(OakRenderManagerTest, AvailableReturnsBool)
{
	int available = oakrender_manager_available();
	EXPECT_TRUE(available == 0 || available == 1);
}

TEST(OakRenderManagerTest, CancelVideoTasksWithoutManagerIsNoOp)
{
	if (oakrender_manager_available()) {
		GTEST_SKIP() << "manager exists in this process";
	}
	oakrender_cancel_video_tasks(0);
	oakrender_cancel_video_tasks(1);
}
