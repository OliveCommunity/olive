#include <gtest/gtest.h>

#include "config/config.h"
#include "render/rendermanager.h"

TEST(Config, DefaultsPresent)
{
	olive::Config &cfg = olive::Config::Current();
	cfg.SetDefaults();

	EXPECT_TRUE(cfg[QStringLiteral("Style")].isValid());
	EXPECT_TRUE(cfg[QStringLiteral("TimecodeDisplay")].isValid());
	EXPECT_TRUE(cfg[QStringLiteral("DefaultStillLength")].isValid());
	EXPECT_EQ(cfg[QStringLiteral("GraphicsBackend")].toString(),
			  QStringLiteral("opengl"));
}

TEST(Config, SetAndGetValues)
{
	olive::Config &cfg = olive::Config::Current();
	cfg[QStringLiteral("UnitTestValue")] = 42;
	EXPECT_EQ(cfg[QStringLiteral("UnitTestValue")].toInt(), 42);
}

TEST(Config, GraphicsBackendStringConversion)
{
	EXPECT_EQ(olive::RenderManager::BackendFromString(QStringLiteral("opengl")),
			  olive::RenderManager::kOpenGL);
	EXPECT_EQ(olive::RenderManager::BackendFromString(QStringLiteral("vulkan")),
			  olive::RenderManager::kVulkan);
	EXPECT_EQ(olive::RenderManager::BackendToString(
				  olive::RenderManager::kOpenGL),
			  QStringLiteral("opengl"));
	EXPECT_EQ(olive::RenderManager::BackendToString(
				  olive::RenderManager::kVulkan),
			  QStringLiteral("vulkan"));
	EXPECT_EQ(olive::RenderManager::BackendFromString(QStringLiteral("bad")),
			  olive::RenderManager::kOpenGL);
}
