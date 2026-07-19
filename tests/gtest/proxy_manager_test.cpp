#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "codec/proxymanager.h"
#include "config/config.h"
#include "node/project/footage/footage.h"
#include "render/job/footagejob.h"
#include "task/proxy/proxy.h"
#include "task/taskmanager.h"

namespace
{

QVariant proxy_config_value(const char *key)
{
	return olive::Config::current()[QString::fromUtf8(key)];
}

} // namespace

TEST(ProxyManager, BuildsStableProxyFilename)
{
	olive::ProxyManager::ProxyParams params;
	params.width = 1280;
	params.height = 720;
	params.version = 1;

	const QString first = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		0, params);
	const QString second = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		0, params);
	const QString other_stream = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		1, params);

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

	const QString first = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		0, mp4_720p);
	const QString second = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		0, mov_540p);

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
	EXPECT_EQ(olive::ProxyManager::get_proxy_state(proxy),
			  olive::ProxyManager::k_proxy_missing);

	QFile working(olive::ProxyManager::get_working_proxy_filename(proxy));
	ASSERT_TRUE(working.open(QFile::WriteOnly));
	working.close();
	EXPECT_EQ(olive::ProxyManager::get_proxy_state(proxy),
			  olive::ProxyManager::k_proxy_generating);

	QFile ready(proxy);
	ASSERT_TRUE(ready.open(QFile::WriteOnly));
	ready.close();
	EXPECT_EQ(olive::ProxyManager::get_proxy_state(proxy),
			  olive::ProxyManager::k_proxy_ready);
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

	QFile working(olive::ProxyManager::get_working_proxy_filename(proxy));
	ASSERT_TRUE(working.open(QFile::WriteOnly));
	working.close();

	EXPECT_EQ(olive::ProxyManager::get_proxy_state(proxy),
			  olive::ProxyManager::k_proxy_ready);
}

TEST(ProxyManager, ConvertsProxyStateToAndFromStrings)
{
	EXPECT_EQ(olive::ProxyManager::proxy_state_to_string(
				  olive::ProxyManager::k_proxy_missing),
			  QStringLiteral("missing"));
	EXPECT_EQ(olive::ProxyManager::proxy_state_to_string(
				  olive::ProxyManager::k_proxy_generating),
			  QStringLiteral("generating"));
	EXPECT_EQ(olive::ProxyManager::proxy_state_to_string(
				  olive::ProxyManager::k_proxy_ready),
			  QStringLiteral("ready"));
	EXPECT_EQ(olive::ProxyManager::proxy_state_to_string(
				  olive::ProxyManager::k_proxy_failed),
			  QStringLiteral("failed"));

	EXPECT_EQ(
		olive::ProxyManager::proxy_state_from_string(QStringLiteral("missing")),
		olive::ProxyManager::k_proxy_missing);
	EXPECT_EQ(
		olive::ProxyManager::proxy_state_from_string(QStringLiteral("generating")),
		olive::ProxyManager::k_proxy_generating);
	EXPECT_EQ(
		olive::ProxyManager::proxy_state_from_string(QStringLiteral("ready")),
		olive::ProxyManager::k_proxy_ready);
	EXPECT_EQ(
		olive::ProxyManager::proxy_state_from_string(QStringLiteral("failed")),
		olive::ProxyManager::k_proxy_failed);
	EXPECT_EQ(
		olive::ProxyManager::proxy_state_from_string(QStringLiteral("unknown")),
		olive::ProxyManager::k_proxy_missing);
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
	ASSERT_TRUE(footage.load_custom(&reader, nullptr));
	EXPECT_TRUE(footage.proxy_enabled());
	EXPECT_EQ(footage.proxy_path(), QStringLiteral("/cache/proxy/example.mp4"));
	EXPECT_EQ(footage.proxy_state(), olive::ProxyManager::k_proxy_ready);
	EXPECT_EQ(footage.proxy_video_stream_index(), 0);
	EXPECT_EQ(footage.proxy_preset_version(), 1);
}

TEST(ProxyManager, FootageSavesProxyMetadata)
{
	olive::Footage footage;
	footage.set_timestamp(42);
	footage.set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					 olive::ProxyManager::k_proxy_ready, 2, 3, true);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	footage.save_custom(&writer);
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
	footage.set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					 olive::ProxyManager::k_proxy_ready, 0, 1, true);

	footage.clear();

	EXPECT_FALSE(footage.proxy_enabled());
	EXPECT_TRUE(footage.proxy_path().isEmpty());
	EXPECT_EQ(footage.proxy_state(), olive::ProxyManager::k_proxy_missing);
	EXPECT_EQ(footage.proxy_video_stream_index(), -1);
	EXPECT_EQ(footage.proxy_preset_version(), 0);
}

TEST(ProxyManager, EmitsProxyFinishedState)
{
	// Drives a real proxy job through ProxyManager so that ProxyFinished is
	// emitted by the manager's own task completion path
	const QString ffmpeg = olive::ProxyManager::find_f_fmpeg_executable(
		proxy_config_value("FFmpegPath").toString());
	if (ffmpeg.isEmpty()) {
		GTEST_SKIP() << "ffmpeg executable not available";
	}

	const QString source =
		QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
			.filePath(QStringLiteral("tests/demo.mp4"));
	ASSERT_TRUE(QFileInfo::exists(source));

	const bool created_task_manager =
		(olive::TaskManager::instance() == nullptr);
	if (created_task_manager) {
		olive::TaskManager::create_instance();
	}
	olive::ProxyManager::create_instance();

	QTemporaryDir cache;
	ASSERT_TRUE(cache.isValid());

	// A small, fast preset keeps the encode of the 17 second demo clip quick
	olive::ProxyManager::ProxyParams params;
	params.width = 320;
	params.height = 180;
	params.preset = QStringLiteral("ultrafast");
	params.include_audio = true;

	const QString expected_proxy = olive::ProxyManager::get_proxy_filename(
		cache.path(), source, 0, params);

	bool received = false;
	QString received_source;
	int received_stream = -1;
	QString received_proxy;
	olive::ProxyManager::ProxyState received_state =
		olive::ProxyManager::k_proxy_missing;
	bool ready_received = false;
	QEventLoop loop;
	QObject::connect(
		olive::ProxyManager::instance(), &olive::ProxyManager::proxy_finished,
		&loop,
		[&received, &received_source, &received_stream, &received_proxy,
		 &received_state, &loop](const QString &source_filename,
								 int stream_index, const QString &proxy_filename,
								 olive::ProxyManager::ProxyState state) {
			received = true;
			received_source = source_filename;
			received_stream = stream_index;
			received_proxy = proxy_filename;
			received_state = state;
			loop.quit();
		});
	QObject::connect(olive::ProxyManager::instance(),
					 &olive::ProxyManager::proxy_ready, &loop,
					 [&ready_received](const QString &, int, const QString &) {
						 ready_received = true;
					 });
	// Generous timeout; failure to finish in time fails the test below
	QTimer::singleShot(120000, &loop, &QEventLoop::quit);

	const olive::ProxyManager::Proxy proxy =
		olive::ProxyManager::instance()->get_or_start_proxy(cache.path(), source, 0,
														 params);
	ASSERT_EQ(proxy.state, olive::ProxyManager::k_proxy_generating);
	ASSERT_NE(proxy.task, nullptr);

	loop.exec();

	ASSERT_TRUE(received) << "Timed out waiting for proxy generation";
	EXPECT_TRUE(ready_received);
	EXPECT_EQ(received_source, source);
	EXPECT_EQ(received_stream, 0);
	EXPECT_EQ(received_proxy, expected_proxy);
	EXPECT_EQ(received_state, olive::ProxyManager::k_proxy_ready);

	// The manager moved the completed proxy into its final location
	EXPECT_TRUE(QFileInfo::exists(expected_proxy));
	EXPECT_EQ(olive::ProxyManager::get_proxy_state(expected_proxy),
			  olive::ProxyManager::k_proxy_ready);

	olive::ProxyManager::destroy_instance();
	if (created_task_manager) {
		olive::TaskManager::destroy_instance();
	}
}

TEST(ProxyManager, WorkingProxyFilenamePrependsExtension)
{
	const QString proxy = QStringLiteral("/cache/proxy/example.mp4");
	const QString working = olive::ProxyManager::get_working_proxy_filename(proxy);

	EXPECT_EQ(working, QStringLiteral("/cache/proxy/example.mp4.working.mp4"));
}

TEST(ProxyManager, ProxyStateFromStringDefaultsForEmpty)
{
	EXPECT_EQ(olive::ProxyManager::proxy_state_from_string(QString()),
			  olive::ProxyManager::k_proxy_missing);
}

TEST(ProxyManager, FootageProxyCanBeDisabled)
{
	olive::Footage footage;
	footage.set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					 olive::ProxyManager::k_proxy_ready, 0, 1, false);

	EXPECT_FALSE(footage.proxy_enabled());
	EXPECT_FALSE(footage.proxy_path().isEmpty());
}

TEST(ProxyManager, FootageJobWithoutProxyHasEmptyProxyFields)
{
	olive::FootageJob job(olive::TimeRange(), QStringLiteral("source-decoder"),
						  QStringLiteral("/media/source.mov"),
						  olive::Track::k_video, olive::Rational(10),
						  olive::LoopMode::k_loop_mode_off);

	EXPECT_FALSE(job.has_proxy());
	EXPECT_TRUE(job.proxy_filename().isEmpty());
	EXPECT_TRUE(job.proxy_decoder().isEmpty());
	EXPECT_EQ(job.proxy_stream_index(), -1);
}

TEST(ProxyManager, ProxyFilenameIncludesAudioFlag)
{
	olive::ProxyManager::ProxyParams params;
	params.include_audio = true;

	const QString with_audio = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		0, params);
	EXPECT_TRUE(with_audio.contains(QStringLiteral(".a1.")));
	EXPECT_TRUE(olive::ProxyManager::proxy_filename_has_audio(with_audio));

	params.include_audio = false;
	const QString without_audio = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		0, params);
	EXPECT_TRUE(without_audio.contains(QStringLiteral(".a0.")));
	EXPECT_FALSE(olive::ProxyManager::proxy_filename_has_audio(without_audio));

	// Legacy proxy filenames (no audio marker) must be treated as video-only
	EXPECT_FALSE(olive::ProxyManager::proxy_filename_has_audio(
		QStringLiteral("/tmp/oak-cache/proxy/abc-0.1280x720.v1.mp4")));

	// The audio flag distinguishes otherwise identical proxy filenames
	EXPECT_NE(with_audio, without_audio);
}

TEST(ProxyManager, ProxyParamsFromConfigReadsDefaults)
{
	const olive::ProxyManager::ProxyParams params =
		olive::ProxyManager::proxy_params_from_config();

	EXPECT_EQ(params.width, proxy_config_value("ProxyWidth").value<int>());
	EXPECT_EQ(params.height, proxy_config_value("ProxyHeight").value<int>());
	EXPECT_EQ(params.crf, proxy_config_value("ProxyCRF").value<int>());
	EXPECT_EQ(params.preset, proxy_config_value("ProxyPreset").toString());
	EXPECT_EQ(params.include_audio,
			  proxy_config_value("ProxyIncludeAudio").toBool());
}

TEST(ProxyManager, FindFFmpegExecutablePrefersConfiguredPath)
{
	// The test executable itself is guaranteed to be an existing executable
	// file, making it a safe stand-in for an ffmpeg binary
	const QString self = QCoreApplication::applicationFilePath();
	ASSERT_FALSE(self.isEmpty());

	EXPECT_EQ(olive::ProxyManager::find_f_fmpeg_executable(self), self);
}

TEST(ProxyManager, FindFFmpegExecutableRejectsInvalidConfiguredPath)
{
	const QString bogus = QStringLiteral("/nonexistent/ffmpeg-binary");
	const QString result = olive::ProxyManager::find_f_fmpeg_executable(bogus);

	// Must not return the invalid configured path; any fallback is acceptable
	EXPECT_NE(result, bogus);
}

TEST(ProxyTask, BuildArgumentsIncludesAudioWhenEnabled)
{
	olive::ProxyManager::ProxyParams params;
	params.include_audio = true;

	const QStringList args = olive::ProxyTask::build_arguments(
		QStringLiteral("/media/source.mov"), 1, params,
		QStringLiteral("/cache/proxy/out.mp4"));

	EXPECT_FALSE(args.contains(QStringLiteral("-an")));
	const int audio_map = args.indexOf(QStringLiteral("0:a?"));
	EXPECT_GE(audio_map, 0);
	EXPECT_GT(audio_map, args.indexOf(QStringLiteral("-map")));
	EXPECT_TRUE(args.contains(QStringLiteral("-c:a")));
	EXPECT_TRUE(args.contains(QStringLiteral("aac")));
	// The requested video stream must be mapped before the audio streams
	EXPECT_LT(args.indexOf(QStringLiteral("0:1")), audio_map);
}

TEST(ProxyTask, BuildArgumentsDisablesAudioWhenDisabled)
{
	olive::ProxyManager::ProxyParams params;
	params.include_audio = false;

	const QStringList args = olive::ProxyTask::build_arguments(
		QStringLiteral("/media/source.mov"), 1, params,
		QStringLiteral("/cache/proxy/out.mp4"));

	EXPECT_TRUE(args.contains(QStringLiteral("-an")));
	EXPECT_FALSE(args.contains(QStringLiteral("0:a?")));
}

TEST(ProxyTask, ParseProgressReadsOutTime)
{
	EXPECT_DOUBLE_EQ(olive::ProxyTask::parse_progress(
						 QStringLiteral("out_time_us=5000000"), 10.0),
					 0.5);
	// Despite the name, out_time_ms is also reported in microseconds
	EXPECT_DOUBLE_EQ(olive::ProxyTask::parse_progress(
						 QStringLiteral("out_time_ms=2500000"), 10.0),
					 0.25);
}

TEST(ProxyTask, ParseProgressClampsToOne)
{
	EXPECT_DOUBLE_EQ(olive::ProxyTask::parse_progress(
						 QStringLiteral("out_time_us=20000000"), 10.0),
					 1.0);
}

TEST(ProxyTask, ParseProgressIgnoresUnknownDuration)
{
	EXPECT_LT(olive::ProxyTask::parse_progress(
				  QStringLiteral("out_time_us=5000000"), 0.0),
			  0.0);
}

TEST(ProxyTask, ParseProgressIgnoresUnrelatedLines)
{
	EXPECT_LT(olive::ProxyTask::parse_progress(QStringLiteral("frame=  250"),
											   10.0),
			  0.0);
	EXPECT_LT(olive::ProxyTask::parse_progress(QStringLiteral("progress=end"),
											   10.0),
			  0.0);
}

TEST(ProxyTask, BuildArgumentsDividerScalesFromSource)
{
	olive::ProxyManager::ProxyParams params;
	params.divider = 4;

	const QStringList args = olive::ProxyTask::build_arguments(
		QStringLiteral("/media/source.mov"), 0, params,
		QStringLiteral("/cache/proxy/out.mp4"));

	const int vf = args.indexOf(QStringLiteral("-vf"));
	ASSERT_GE(vf, 0);
	EXPECT_EQ(args.at(vf + 1),
			  QStringLiteral("scale=w=trunc(iw/4/2)*2:h=trunc(ih/4/2)*2"));
}

TEST(ProxyManager, DividerProxyFilenameUsesDividerTag)
{
	olive::ProxyManager::ProxyParams params;
	params.divider = 2;

	const QString filename = olive::ProxyManager::get_proxy_filename(
		QStringLiteral("/tmp/oak-cache"), QStringLiteral("/media/source.mov"),
		0, params);

	EXPECT_TRUE(QFileInfo(filename).fileName().contains(
		QStringLiteral(".div2.")));
}

TEST(ProxyManager, FootagePersistsCustomProxyParams)
{
	olive::Footage footage;
	olive::ProxyManager::ProxyParams params;
	params.width = 640;
	params.height = 360;
	params.divider = 4;
	params.crf = 30;
	params.preset = QStringLiteral("faster");
	params.extension = QStringLiteral("mov");
	params.include_audio = false;
	footage.set_custom_proxy_params(params);
	footage.set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					 olive::ProxyManager::k_proxy_ready, 0, 1, true);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	footage.save_custom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	EXPECT_TRUE(xml.contains(QStringLiteral("custom=\"1\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("pwidth=\"640\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("pheight=\"360\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("pdivider=\"4\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("pcrf=\"30\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("ppreset=\"faster\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("pext=\"mov\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("paudio=\"0\"")));

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));

	olive::Footage loaded;
	ASSERT_TRUE(loaded.load_custom(&reader, nullptr));
	ASSERT_TRUE(loaded.has_custom_proxy_params());
	EXPECT_EQ(loaded.custom_proxy_params().width, 640);
	EXPECT_EQ(loaded.custom_proxy_params().height, 360);
	EXPECT_EQ(loaded.custom_proxy_params().divider, 4);
	EXPECT_EQ(loaded.custom_proxy_params().crf, 30);
	EXPECT_EQ(loaded.custom_proxy_params().preset, QStringLiteral("faster"));
	EXPECT_EQ(loaded.custom_proxy_params().extension, QStringLiteral("mov"));
	EXPECT_FALSE(loaded.custom_proxy_params().include_audio);
}

TEST(ProxyManager, FootageWithoutCustomParamsOmitsThemFromXml)
{
	olive::Footage footage;
	footage.set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					 olive::ProxyManager::k_proxy_ready, 0, 1, true);

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	footage.save_custom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	EXPECT_FALSE(xml.contains(QStringLiteral("custom=")));

	// Loading old project files without custom params must not enable them
	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	olive::Footage loaded;
	ASSERT_TRUE(loaded.load_custom(&reader, nullptr));
	EXPECT_FALSE(loaded.has_custom_proxy_params());
}

TEST(ProxyManager, FootageEffectiveProxyParams)
{
	olive::Footage footage;

	// Without custom params, the global config values apply
	const olive::ProxyManager::ProxyParams global_params =
		footage.get_effective_proxy_params();
	EXPECT_EQ(global_params.width, proxy_config_value("ProxyWidth").value<int>());
	EXPECT_EQ(global_params.include_audio,
			  proxy_config_value("ProxyIncludeAudio").toBool());

	// Custom params take precedence
	olive::ProxyManager::ProxyParams custom;
	custom.width = 320;
	custom.height = 180;
	footage.set_custom_proxy_params(custom);
	EXPECT_TRUE(footage.has_custom_proxy_params());
	EXPECT_EQ(footage.get_effective_proxy_params().width, 320);
	EXPECT_EQ(footage.get_effective_proxy_params().height, 180);

	// Clearing reverts to the global config values
	footage.clear_custom_proxy_params();
	EXPECT_FALSE(footage.has_custom_proxy_params());
	EXPECT_EQ(footage.get_effective_proxy_params().width,
			  proxy_config_value("ProxyWidth").value<int>());
}
