/*
  This file is part of Oak Video Editor - A fork of original project Olive 

  SPDX-License-Identifier: GPL-3.0-only
  Copyright (C) 2025 mikesolar

*/

#include <gtest/gtest.h>

#include "pluginSupport/OliveHost.h"

TEST(PluginSupport, LoadPluginsEmptyPath)
{
	EXPECT_NO_THROW({
		olive::plugin::loadPlugins(QString());
	});
}
