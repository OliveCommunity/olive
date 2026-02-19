/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#include <gtest/gtest.h>

#include "codec/frame.h"

TEST(CodecFrame, DefaultState)
{
	olive::Frame frame;
	EXPECT_EQ(frame.width(), 0);
	EXPECT_EQ(frame.height(), 0);
	EXPECT_EQ(frame.format(), olive::core::PixelFormat::INVALID);
}
