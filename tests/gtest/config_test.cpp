#include <gtest/gtest.h>

#include "config/config.h"
#include "render/rendermanager.h"

TEST(Config, DefaultsPresent)
{
	olive::Config &cfg = olive::Config::current();
	cfg.set_defaults();

	EXPECT_TRUE(cfg[QStringLiteral("Style")].isValid());
	EXPECT_TRUE(cfg[QStringLiteral("TimecodeDisplay")].isValid());
	EXPECT_TRUE(cfg[QStringLiteral("DefaultStillLength")].isValid());
	EXPECT_EQ(cfg[QStringLiteral("GraphicsBackend")].toString(),
			  QStringLiteral("opengl"));
}

TEST(Config, SetAndGetValues)
{
	olive::Config &cfg = olive::Config::current();
	cfg[QStringLiteral("UnitTestValue")] = 42;
	EXPECT_EQ(cfg[QStringLiteral("UnitTestValue")].toInt(), 42);

	cfg[QStringLiteral("UnitTestString")] = QStringLiteral("hello");
	EXPECT_EQ(cfg[QStringLiteral("UnitTestString")].toString(),
			  QStringLiteral("hello"));

	cfg[QStringLiteral("UnitTestBool")] = true;
	EXPECT_TRUE(cfg[QStringLiteral("UnitTestBool")].toBool());

	cfg[QStringLiteral("UnitTestDouble")] = 3.14;
	EXPECT_NEAR(cfg[QStringLiteral("UnitTestDouble")].toDouble(), 3.14, 0.001);

	// Config offers no key-removal API, so reset the singleton to its
	// default state to avoid leaking the UnitTest* keys into later tests
	cfg.set_defaults();
	EXPECT_FALSE(cfg[QStringLiteral("UnitTestValue")].isValid());
	EXPECT_FALSE(cfg[QStringLiteral("UnitTestString")].isValid());
	EXPECT_FALSE(cfg[QStringLiteral("UnitTestBool")].isValid());
	EXPECT_FALSE(cfg[QStringLiteral("UnitTestDouble")].isValid());
}

TEST(Config, MissingKeyReturnsInvalidVariant)
{
	olive::Config &cfg = olive::Config::current();
	EXPECT_FALSE(cfg[QStringLiteral("DefinitelyMissingKey")].isValid());
}

TEST(Config, GraphicsBackendStringConversion)
{
	EXPECT_EQ(olive::RenderManager::backend_from_string(QStringLiteral("opengl")),
			  olive::RenderManager::k_open_gl);
	EXPECT_EQ(olive::RenderManager::backend_from_string(QStringLiteral("vulkan")),
			  olive::RenderManager::k_vulkan);
	EXPECT_EQ(olive::RenderManager::backend_from_string(QStringLiteral("dummy")),
			  olive::RenderManager::k_dummy);
	EXPECT_EQ(
		olive::RenderManager::backend_from_string(QStringLiteral("multiprocess")),
		olive::RenderManager::k_multi_process);
	EXPECT_EQ(
		olive::RenderManager::backend_to_string(olive::RenderManager::k_open_gl),
		QStringLiteral("opengl"));
	EXPECT_EQ(
		olive::RenderManager::backend_to_string(olive::RenderManager::k_vulkan),
		QStringLiteral("vulkan"));
	EXPECT_EQ(
		olive::RenderManager::backend_to_string(olive::RenderManager::k_dummy),
		QStringLiteral("dummy"));
	EXPECT_EQ(olive::RenderManager::backend_to_string(
				  olive::RenderManager::k_multi_process),
			  QStringLiteral("multiprocess"));
	EXPECT_EQ(olive::RenderManager::backend_from_string(QStringLiteral("bad")),
			  olive::RenderManager::k_open_gl);
}

TEST(Config, SetDefaultsPopulatesRequiredKeys)
{
	olive::Config &cfg = olive::Config::current();
	cfg.set_defaults();

	const QStringList required = {
		QStringLiteral("Style"),
		QStringLiteral("TimecodeDisplay"),
		QStringLiteral("DefaultStillLength"),
		QStringLiteral("GraphicsBackend"),
		QStringLiteral("AutoCacheDelay"),
		QStringLiteral("AudioOutput"),
		QStringLiteral("AudioInput"),
	};

	foreach (const QString &key, required) {
		EXPECT_TRUE(cfg[key].isValid()) << key.toStdString();
	}
}
