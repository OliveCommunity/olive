#include <gtest/gtest.h>

#include "pluginSupport/olivehost.h"

TEST(PluginSupport, LoadPluginsEmptyPath)
{
	EXPECT_NO_THROW({ olive::plugin::load_plugins(QString()); });
}
