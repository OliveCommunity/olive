#include <gtest/gtest.h>

#include <QTemporaryFile>

#include "render/job/footagejob.h"

TEST(FootageJobProxy, NoProxyNeverUsed)
{
	olive::FootageJob job;

	EXPECT_FALSE(job.has_proxy());
	EXPECT_FALSE(job.should_use_proxy(olive::RenderMode::k_offline));
	EXPECT_FALSE(job.should_use_proxy(olive::RenderMode::k_online));
}

TEST(FootageJobProxy, OfflineUsesProxyOnlineDoesNot)
{
	QTemporaryFile proxy_file;
	ASSERT_TRUE(proxy_file.open());

	olive::FootageJob job;
	job.set_proxy(proxy_file.fileName(), QStringLiteral("ffmpeg"), 0);

	ASSERT_TRUE(job.has_proxy());

	// Preview renders may decode from the proxy
	EXPECT_TRUE(job.should_use_proxy(olive::RenderMode::k_offline));

	// Export/master renders must always decode the original media
	EXPECT_FALSE(job.should_use_proxy(olive::RenderMode::k_online));
}

TEST(FootageJobProxy, MissingProxyFileFallsBackToOriginal)
{
	olive::FootageJob job;
	job.set_proxy(QStringLiteral("/nonexistent/path/proxy.mp4"),
				  QStringLiteral("ffmpeg"), 0);

	ASSERT_TRUE(job.has_proxy());

	// A proxy that no longer exists on disk must not be used, even for preview
	EXPECT_FALSE(job.should_use_proxy(olive::RenderMode::k_offline));
	EXPECT_FALSE(job.should_use_proxy(olive::RenderMode::k_online));
}

TEST(FootageJobProxy, EmptyProxyFilenameDisablesProxy)
{
	olive::FootageJob job;
	job.set_proxy(QString(), QStringLiteral("ffmpeg"), 0);

	EXPECT_FALSE(job.has_proxy());
	EXPECT_FALSE(job.should_use_proxy(olive::RenderMode::k_offline));
	EXPECT_FALSE(job.should_use_proxy(olive::RenderMode::k_online));
}
