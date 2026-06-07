#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "codec/proxymanager.h"
#include "node/project/footage/footage.h"
#include "render/job/footagejob.h"

TEST(ProxyManager, BuildsStableProxyFilename)
{
	olive::ProxyManager::ProxyParams params;
	params.width = 1280;
	params.height = 720;
	params.version = 1;

	const QString first = olive::ProxyManager::GetProxyFilename(
		QStringLiteral("/tmp/oak-cache"),
		QStringLiteral("/media/source.mov"), 0, params);
	const QString second = olive::ProxyManager::GetProxyFilename(
		QStringLiteral("/tmp/oak-cache"),
		QStringLiteral("/media/source.mov"), 0, params);
	const QString other_stream = olive::ProxyManager::GetProxyFilename(
		QStringLiteral("/tmp/oak-cache"),
		QStringLiteral("/media/source.mov"), 1, params);

	EXPECT_EQ(first, second);
	EXPECT_NE(first, other_stream);
	EXPECT_TRUE(first.contains(QStringLiteral("/proxy/")));
	EXPECT_TRUE(first.endsWith(QStringLiteral(".mp4")));
}

TEST(ProxyManager, ProxyFilenameIncludesPresetParameters)
{
	olive::ProxyManager::ProxyParams mp4_720p;
	mp4_720p.width = 1280;
	mp4_720p.height = 720;
	mp4_720p.version = 1;
	mp4_720p.extension = QStringLiteral("mp4");

	olive::ProxyManager::ProxyParams mov_540p = mp4_720p;
	mov_540p.width = 960;
	mov_540p.height = 540;
	mov_540p.version = 2;
	mov_540p.extension = QStringLiteral("mov");

	const QString first = olive::ProxyManager::GetProxyFilename(
		QStringLiteral("/tmp/oak-cache"),
		QStringLiteral("/media/source.mov"), 0, mp4_720p);
	const QString second = olive::ProxyManager::GetProxyFilename(
		QStringLiteral("/tmp/oak-cache"),
		QStringLiteral("/media/source.mov"), 0, mov_540p);

	EXPECT_NE(first, second);
	EXPECT_TRUE(first.contains(QStringLiteral(".1280x720.v1.")));
	EXPECT_TRUE(second.contains(QStringLiteral(".960x540.v2.")));
	EXPECT_TRUE(second.endsWith(QStringLiteral(".mov")));
}

TEST(ProxyManager, DetectsProxyState)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	const QString proxy =
		QDir(dir.path()).filePath(QStringLiteral("proxy-file.mp4"));
	EXPECT_EQ(olive::ProxyManager::GetProxyState(proxy),
			  olive::ProxyManager::kProxyMissing);

	QFile working(olive::ProxyManager::GetWorkingProxyFilename(proxy));
	ASSERT_TRUE(working.open(QFile::WriteOnly));
	working.close();
	EXPECT_EQ(olive::ProxyManager::GetProxyState(proxy),
			  olive::ProxyManager::kProxyGenerating);

	QFile ready(proxy);
	ASSERT_TRUE(ready.open(QFile::WriteOnly));
	ready.close();
	EXPECT_EQ(olive::ProxyManager::GetProxyState(proxy),
			  olive::ProxyManager::kProxyReady);
}

TEST(ProxyManager, ReadyStateTakesPrecedenceOverWorkingFile)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	const QString proxy =
		QDir(dir.path()).filePath(QStringLiteral("proxy-file.mp4"));
	QFile ready(proxy);
	ASSERT_TRUE(ready.open(QFile::WriteOnly));
	ready.close();

	QFile working(olive::ProxyManager::GetWorkingProxyFilename(proxy));
	ASSERT_TRUE(working.open(QFile::WriteOnly));
	working.close();

	EXPECT_EQ(olive::ProxyManager::GetProxyState(proxy),
			  olive::ProxyManager::kProxyReady);
}

TEST(ProxyManager, ConvertsProxyStateToAndFromStrings)
{
	EXPECT_EQ(olive::ProxyManager::ProxyStateToString(
				  olive::ProxyManager::kProxyMissing),
			  QStringLiteral("missing"));
	EXPECT_EQ(olive::ProxyManager::ProxyStateToString(
				  olive::ProxyManager::kProxyGenerating),
			  QStringLiteral("generating"));
	EXPECT_EQ(olive::ProxyManager::ProxyStateToString(
				  olive::ProxyManager::kProxyReady),
			  QStringLiteral("ready"));
	EXPECT_EQ(olive::ProxyManager::ProxyStateToString(
				  olive::ProxyManager::kProxyFailed),
			  QStringLiteral("failed"));

	EXPECT_EQ(olive::ProxyManager::ProxyStateFromString(
				  QStringLiteral("missing")),
			  olive::ProxyManager::kProxyMissing);
	EXPECT_EQ(olive::ProxyManager::ProxyStateFromString(
				  QStringLiteral("generating")),
			  olive::ProxyManager::kProxyGenerating);
	EXPECT_EQ(olive::ProxyManager::ProxyStateFromString(
				  QStringLiteral("ready")),
			  olive::ProxyManager::kProxyReady);
	EXPECT_EQ(olive::ProxyManager::ProxyStateFromString(
				  QStringLiteral("failed")),
			  olive::ProxyManager::kProxyFailed);
	EXPECT_EQ(olive::ProxyManager::ProxyStateFromString(
				  QStringLiteral("unknown")),
			  olive::ProxyManager::kProxyMissing);
}

TEST(ProxyManager, FootagePersistsProxyMetadata)
{
	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	writer.writeTextElement(QStringLiteral("timestamp"), QStringLiteral("0"));
	writer.writeStartElement(QStringLiteral("proxy"));
	writer.writeAttribute(QStringLiteral("enabled"), QStringLiteral("1"));
	writer.writeAttribute(QStringLiteral("state"), QStringLiteral("ready"));
	writer.writeAttribute(QStringLiteral("stream"), QStringLiteral("0"));
	writer.writeAttribute(QStringLiteral("preset"), QStringLiteral("1"));
	writer.writeCharacters(QStringLiteral("/cache/proxy/example.mp4"));
	writer.writeEndElement();
	writer.writeEndElement();
	writer.writeEndDocument();

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));

	olive::Footage footage;
	ASSERT_TRUE(footage.LoadCustom(&reader, nullptr));
	EXPECT_TRUE(footage.proxy_enabled());
	EXPECT_EQ(footage.proxy_path(), QStringLiteral("/cache/proxy/example.mp4"));
	EXPECT_EQ(footage.proxy_state(), olive::ProxyManager::kProxyReady);
	EXPECT_EQ(footage.proxy_video_stream_index(), 0);
	EXPECT_EQ(footage.proxy_preset_version(), 1);
}

TEST(ProxyManager, FootageSavesProxyMetadata)
{
	olive::Footage footage;
	footage.set_timestamp(42);
	footage.SetProxy(QStringLiteral("/cache/proxy/example.mp4"),
					 olive::ProxyManager::kProxyReady, 2, 3, true);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	footage.SaveCustom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	EXPECT_TRUE(xml.contains(QStringLiteral("<proxy")));
	EXPECT_TRUE(xml.contains(QStringLiteral("enabled=\"1\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("state=\"ready\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("stream=\"2\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("preset=\"3\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("/cache/proxy/example.mp4")));
}

TEST(ProxyManager, FootageClearRemovesProxyMetadata)
{
	olive::Footage footage;
	footage.SetProxy(QStringLiteral("/cache/proxy/example.mp4"),
					 olive::ProxyManager::kProxyReady, 0, 1, true);

	footage.Clear();

	EXPECT_FALSE(footage.proxy_enabled());
	EXPECT_TRUE(footage.proxy_path().isEmpty());
	EXPECT_EQ(footage.proxy_state(), olive::ProxyManager::kProxyMissing);
	EXPECT_EQ(footage.proxy_video_stream_index(), -1);
	EXPECT_EQ(footage.proxy_preset_version(), 0);
}

TEST(ProxyManager, EmitsProxyFinishedState)
{
	olive::ProxyManager::CreateInstance();

	bool received = false;
	QString received_source;
	int received_stream = -1;
	QString received_proxy;
	olive::ProxyManager::ProxyState received_state =
		olive::ProxyManager::kProxyMissing;
	QObject::connect(
		olive::ProxyManager::instance(), &olive::ProxyManager::ProxyFinished,
		[&received, &received_source, &received_stream, &received_proxy,
		 &received_state](const QString &source_filename, int stream_index,
						  const QString &proxy_filename,
						  olive::ProxyManager::ProxyState state) {
			received = true;
			received_source = source_filename;
			received_stream = stream_index;
			received_proxy = proxy_filename;
			received_state = state;
		});

	emit olive::ProxyManager::instance()->ProxyFinished(
		QStringLiteral("/media/source.mov"), 0,
		QStringLiteral("/cache/proxy/example.mp4"),
		olive::ProxyManager::kProxyFailed);

	EXPECT_TRUE(received);
	EXPECT_EQ(received_source, QStringLiteral("/media/source.mov"));
	EXPECT_EQ(received_stream, 0);
	EXPECT_EQ(received_proxy, QStringLiteral("/cache/proxy/example.mp4"));
	EXPECT_EQ(received_state, olive::ProxyManager::kProxyFailed);

	olive::ProxyManager::DestroyInstance();
}

TEST(ProxyManager, FootageJobCarriesProxyMetadata)
{
	olive::FootageJob job(olive::TimeRange(), QStringLiteral("source-decoder"),
						  QStringLiteral("/media/source.mov"),
						  olive::Track::kVideo, olive::rational(10),
						  olive::LoopMode::kLoopModeOff);
	EXPECT_FALSE(job.has_proxy());

	job.set_proxy(QStringLiteral("/cache/proxy/source.mp4"),
				  QStringLiteral("ffmpeg"), 0);

	EXPECT_TRUE(job.has_proxy());
	EXPECT_EQ(job.filename(), QStringLiteral("/media/source.mov"));
	EXPECT_EQ(job.proxy_filename(), QStringLiteral("/cache/proxy/source.mp4"));
	EXPECT_EQ(job.proxy_decoder(), QStringLiteral("ffmpeg"));
	EXPECT_EQ(job.proxy_stream_index(), 0);
}
