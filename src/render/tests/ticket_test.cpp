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

#include <gtest/gtest.h>

#include "render/ticket.h"

TEST(OakRenderTicket, NullAndInvalidArgs)
{
	EXPECT_EQ(oakrender_ticket_render_frame(nullptr, nullptr, nullptr),
			  nullptr);

	oakrender_video_ticket_params params = {};
	EXPECT_EQ(oakrender_ticket_render_frame(&params, nullptr, nullptr),
			  nullptr);

	EXPECT_EQ(oakrender_ticket_is_finished(nullptr), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_ticket_wait(nullptr), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_ticket_cancel(nullptr), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_ticket_get_type(nullptr), OAKRENDER_E_INVALID);
	EXPECT_EQ(oakrender_ticket_get_time(nullptr, nullptr, nullptr),
			  OAKRENDER_E_INVALID);

	OakCodecFrame *frame = nullptr;
	EXPECT_EQ(oakrender_ticket_get_frame(nullptr, &frame),
			  OAKRENDER_E_INVALID);

	OakSampleBuffer *samples = nullptr;
	EXPECT_EQ(oakrender_ticket_get_samples(nullptr, &samples),
			  OAKRENDER_E_INVALID);

	oakrender_ticket_free(nullptr); // no-op
}

TEST(OakRenderTicket, AggressiveGcRequiresManager)
{
	// The standalone test binary never initializes the render manager
	int result = oakrender_manager_set_aggressive_gc(1);
	EXPECT_TRUE(result == OAKRENDER_OK || result == OAKRENDER_E_STATE);
}
