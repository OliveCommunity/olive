/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#include <gtest/gtest.h>

#include "ofxParam.h"
#include "ofxhParam.h"
#include "pluginSupport/paraminstance.h"

TEST(PluginSupportParam, IntegerInstanceNullNodeRoundTrip)
{
	OFX::Host::Param::Descriptor descriptor(kOfxParamTypeInteger,
											"TestInteger");
	olive::plugin::IntegerInstance instance(nullptr, descriptor);

	int value = -1;
	EXPECT_EQ(instance.get(value), kOfxStatOK);
	EXPECT_EQ(value, 0);

	EXPECT_EQ(instance.set(7), kOfxStatOK);
	EXPECT_EQ(instance.get(value), kOfxStatOK);
	EXPECT_EQ(value, 7);

	int time_value = -1;
	EXPECT_EQ(instance.get(1.0, time_value), kOfxStatOK);
	EXPECT_EQ(time_value, 7);
}
