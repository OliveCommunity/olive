#include <gtest/gtest.h>

#include "render/loopmode.h"

TEST(LoopMode, ValuesAreDistinct)
{
	EXPECT_NE(olive::LoopMode::k_loop_mode_off, olive::LoopMode::k_loop_mode_loop);
	EXPECT_NE(olive::LoopMode::k_loop_mode_off, olive::LoopMode::k_loop_mode_clamp);
	EXPECT_NE(olive::LoopMode::k_loop_mode_loop, olive::LoopMode::k_loop_mode_clamp);
}
