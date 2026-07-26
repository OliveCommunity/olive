#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariant>
#include <QWidget>

#include "codec/decoder.h"
#include "common/filefunctions.h"
#include "core.h"
#include "node/color/colormanager/colormanager.h"
#include "node/globals.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/footage/footagedescription.h"
#include "render/diskmanager.h"
#include "render/job/footagejob.h"
#include "render/loopmode.h"
#include "render/texture.h"

namespace
{

QString demo_video_path()
{
	return QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
		.filePath(QStringLiteral("tests/demo.mp4"));
}

QString test_image_path()
{
	return QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
		.filePath(QStringLiteral("tests/img.png"));
}

// Mirrors the cache path expression used by Footage::Reprobe()
QString metadata_cache_file_for(const QString &media_path)
{
	return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
		.filePath(olive::FileFunctions::get_unique_file_identifier(media_path));
}

} // namespace

TEST(FootageProbe, FFmpegProbeOfDemoMp4ReportsExpectedStreams)
{
	const QString path = demo_video_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::DecoderPtr decoder =
		olive::Decoder::create_from_id(QStringLiteral("ffmpeg"));
	ASSERT_TRUE(decoder);

	const olive::FootageDescription desc = decoder->probe(path, nullptr);
	ASSERT_TRUE(desc.is_valid());
	EXPECT_EQ(desc.decoder(), QStringLiteral("ffmpeg"));

	// The file holds video + audio + a timecode data track. The data track is
	// counted in the total but not exposed as a usable stream.
	ASSERT_EQ(desc.get_video_streams().size(), 1);
	ASSERT_EQ(desc.get_audio_streams().size(), 1);
	EXPECT_EQ(desc.get_subtitle_streams().size(), 0);
	EXPECT_EQ(desc.get_stream_count(), 3);

	const olive::VideoParams &video = desc.get_video_streams().first();
	EXPECT_EQ(video.stream_index(), 0);
	EXPECT_EQ(video.width(), 1920);
	EXPECT_EQ(video.height(), 1080);
	EXPECT_EQ(video.video_type(), olive::VideoParams::k_video_type_video);
	EXPECT_EQ(video.interlacing(), olive::VideoParams::k_interlace_none);
	EXPECT_EQ(video.pixel_aspect_ratio(), olive::Rational(1, 1));
	EXPECT_EQ(video.frame_rate(), olive::Rational(25));
	EXPECT_EQ(video.time_base(), olive::Rational(1, 12800));
	EXPECT_EQ(video.duration(), 217600); // 17 seconds at 1/12800
	EXPECT_NE(video.format(), olive::core::PixelFormat::invalid);
	EXPECT_GT(video.channel_count(), 0);

	const olive::core::AudioParams &audio = desc.get_audio_streams().first();
	EXPECT_EQ(audio.stream_index(), 1);
	EXPECT_EQ(audio.sample_rate(), 48000);
	EXPECT_EQ(audio.channel_count(), 2);
	EXPECT_EQ(audio.time_base(), olive::Rational(1, 48000));
	EXPECT_EQ(audio.duration(), 816000); // 17 seconds at 1/48000

	// The file's timecode track starts at 01:00:00:00
	ASSERT_TRUE(desc.has_source_start_time());
	EXPECT_EQ(desc.source_start_time(), olive::Rational(3600));
	EXPECT_EQ(desc.source_start_time_source(), QStringLiteral("timecode"));
}

TEST(FootageProbe, ProbeOfUnprobeableFileYieldsInvalidDescription)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());

	const QString path =
		QDir(dir.path()).filePath(QStringLiteral("not_media.txt"));
	{
		QFile file(path);
		ASSERT_TRUE(file.open(QFile::WriteOnly));
		file.write("this is not a media file");
	}

	for (const olive::DecoderPtr &decoder :
		 olive::Decoder::receive_list_of_all_decoders()) {
		EXPECT_FALSE(decoder->probe(path, nullptr).is_valid())
			<< decoder->id().toStdString();
	}
}

class FootageProbeTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		if (!olive::Core::instance()) {
			// Leaked intentionally: Core is process-wide (matches footage_test)
			new olive::Core();
		}

		// Footage::Value() resolves Project::cache_path(), which goes through
		// the DiskManager singleton
		created_disk_manager_ = (olive::DiskManager::instance() == nullptr);
		if (created_disk_manager_) {
			olive::DiskManager::create_instance();
		}

		// Sandbox the footage metadata cache so real probes write into the
		// temp dir instead of the user's cache
		old_cache_home_ = qgetenv("XDG_CACHE_HOME");
		had_cache_home_ = qEnvironmentVariableIsSet("XDG_CACHE_HOME");
		qputenv("XDG_CACHE_HOME",
				QDir(temp_dir_.path()).filePath(QStringLiteral("xdg")).toUtf8());
		QDir().mkpath(
			QStandardPaths::writableLocation(QStandardPaths::CacheLocation));

		olive::ColorManager::set_up_default_config();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	void TearDown() override
	{
		project_.reset();
		if (created_disk_manager_) {
			olive::DiskManager::destroy_instance();
		}
		if (had_cache_home_) {
			qputenv("XDG_CACHE_HOME", old_cache_home_);
		} else {
			qunsetenv("XDG_CACHE_HOME");
		}
	}

	// Constructs a Footage pointing at path; the constructor's set_filename()
	// call probes the file synchronously before the node joins the graph
	olive::Footage *add_probed_footage(const QString &path)
	{
		auto *footage = new olive::Footage(path);
		footage->setParent(project_.get());
		return footage;
	}

	QTemporaryDir temp_dir_;
	QByteArray old_cache_home_;
	bool had_cache_home_ = false;
	bool created_disk_manager_ = false;
	std::unique_ptr<olive::Project> project_;
};

TEST_F(FootageProbeTest, ProbingDemoMp4PopulatesFootageState)
{
	const QString path = demo_video_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::Footage *footage = add_probed_footage(path);

	EXPECT_TRUE(footage->is_valid());
	EXPECT_EQ(footage->decoder(), QStringLiteral("ffmpeg"));
	EXPECT_EQ(footage->timestamp(),
			  QFileInfo(path).lastModified().toMSecsSinceEpoch());

	// Video + audio streams are usable; the timecode data track only shows up
	// in the total stream count
	EXPECT_EQ(footage->get_total_stream_count(), 3);
	EXPECT_EQ(footage->get_video_stream_count(), 1);
	EXPECT_EQ(footage->get_audio_stream_count(), 1);
	EXPECT_EQ(footage->get_subtitle_stream_count(), 0);

	EXPECT_EQ(footage->get_stream_index(olive::Track::k_video, 0), 0);
	EXPECT_EQ(footage->get_stream_index(olive::Track::k_audio, 0), 1);
	EXPECT_EQ(footage->get_reference_from_real_index(0),
			  olive::Track::Reference(olive::Track::k_video, 0));
	EXPECT_EQ(footage->get_reference_from_real_index(1),
			  olive::Track::Reference(olive::Track::k_audio, 0));
	EXPECT_EQ(footage->get_reference_from_real_index(2).type(),
			  olive::Track::k_none);

	EXPECT_EQ(footage->get_connected_texture_output(),
			  static_cast<olive::Node *>(footage));
	EXPECT_EQ(footage->get_connected_sample_output(),
			  static_cast<olive::Node *>(footage));

	const olive::VideoParams video = footage->get_video_params(0);
	ASSERT_TRUE(video.is_valid());
	EXPECT_EQ(video.stream_index(), 0);
	EXPECT_EQ(video.width(), 1920);
	EXPECT_EQ(video.height(), 1080);
	EXPECT_EQ(video.video_type(), olive::VideoParams::k_video_type_video);
	EXPECT_EQ(video.frame_rate(), olive::Rational(25));
	EXPECT_EQ(video.time_base(), olive::Rational(1, 12800));
	EXPECT_EQ(video.duration(), 217600);
	EXPECT_EQ(video.color_range(), olive::VideoParams::k_color_range_limited);
	EXPECT_TRUE(video.enabled());
	// The FFmpeg probe leaves colorspace unset so the project default applies
	EXPECT_TRUE(video.colorspace().isEmpty());

	const olive::core::AudioParams audio = footage->get_audio_params(0);
	ASSERT_TRUE(audio.is_valid());
	EXPECT_EQ(audio.stream_index(), 1);
	EXPECT_EQ(audio.sample_rate(), 48000);
	EXPECT_EQ(audio.channel_count(), 2);
	EXPECT_EQ(audio.duration(), 816000);
	EXPECT_TRUE(audio.enabled());
}

TEST_F(FootageProbeTest, ProbingDemoMp4SetsLengthsAndSourceStartTime)
{
	const QString path = demo_video_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::Footage *footage = add_probed_footage(path);
	footage->verify_length();

	// Both streams describe 17 seconds of media
	EXPECT_EQ(footage->get_video_length(), olive::Rational(17));
	EXPECT_EQ(footage->get_audio_length(), olive::Rational(17));
	EXPECT_EQ(footage->get_length(), olive::Rational(17));

	// The embedded 01:00:00:00 timecode becomes the source start time
	ASSERT_TRUE(footage->has_source_start_time());
	EXPECT_EQ(footage->source_start_time(), olive::Rational(3600));
	EXPECT_EQ(footage->source_start_time_source(), QStringLiteral("timecode"));
}

TEST_F(FootageProbeTest, ProbingPngImageProducesSingleStillStream)
{
	const QString path = test_image_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::Footage *footage = add_probed_footage(path);

	EXPECT_TRUE(footage->is_valid());
	// Still images are handled by the OIIO decoder, which probes before FFmpeg
	EXPECT_EQ(footage->decoder(), QStringLiteral("oiio"));

	EXPECT_EQ(footage->get_total_stream_count(), 1);
	EXPECT_EQ(footage->get_video_stream_count(), 1);
	EXPECT_EQ(footage->get_audio_stream_count(), 0);
	EXPECT_EQ(footage->get_subtitle_stream_count(), 0);

	const olive::VideoParams still = footage->get_video_params(0);
	ASSERT_TRUE(still.is_valid());
	EXPECT_EQ(still.stream_index(), 0);
	EXPECT_EQ(still.width(), 1920);
	EXPECT_EQ(still.height(), 1080);
	EXPECT_EQ(still.video_type(), olive::VideoParams::k_video_type_still);
	EXPECT_EQ(still.channel_count(), 4);
	EXPECT_EQ(still.format(), olive::core::PixelFormat::u8);
	EXPECT_TRUE(still.premultiplied_alpha());
	EXPECT_TRUE(still.enabled());
	EXPECT_TRUE(still.colorspace().isEmpty());

	// Stills have no duration and no source start time
	footage->verify_length();
	EXPECT_EQ(footage->get_video_length(), olive::Rational(0));
	EXPECT_EQ(footage->get_length(), olive::Rational(0));
	EXPECT_FALSE(footage->has_source_start_time());

	EXPECT_EQ(footage->get_connected_texture_output(),
			  static_cast<olive::Node *>(footage));
	EXPECT_EQ(footage->get_connected_sample_output(), nullptr);
}

TEST_F(FootageProbeTest, ProbedFootageValuePushesRealStreamJobs)
{
	const QString path = demo_video_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::Footage *footage = add_probed_footage(path);
	footage->verify_length();

	// The colorspace fallback reads the project default, and the audio cache
	// path comes from the project's cache settings
	project_->set_default_input_color_space(QStringLiteral("ProbeInputSpace"));
	project_->set_cache_location_setting(olive::Project::k_cache_custom_path);
	const QString cache_path =
		QDir(temp_dir_.path()).filePath(QStringLiteral("cache"));
	project_->set_custom_cache_path(cache_path);

	olive::NodeValueRow row;
	row.insert(olive::Footage::k_filename_input,
			   olive::NodeValue(olive::NodeValue::k_file, path));

	olive::VideoParams vparams(64, 64, olive::Rational(1, 24),
							   olive::core::PixelFormat::u8, 4);
	const olive::NodeGlobals globals(vparams, olive::core::AudioParams(),
									 olive::Rational(0),
									 olive::LoopMode::k_loop_mode_off);

	olive::NodeValueTable table;
	footage->value(row, globals, &table);

	// Length, one texture job for the video stream, one sample job for the
	// audio stream; the timecode data track produces no job
	ASSERT_EQ(table.count(), 3);

	const olive::NodeValue length =
		table.get(olive::NodeValue::k_rational, QStringLiteral("length"));
	ASSERT_EQ(length.type(), olive::NodeValue::k_rational);
	EXPECT_EQ(length.to_rational(), olive::Rational(17));

	const olive::TexturePtr texture =
		table.get(olive::NodeValue::k_texture, QStringLiteral("v:0"))
			.to_texture();
	ASSERT_NE(texture, nullptr);
	EXPECT_EQ(texture->params().width(), 1920);
	EXPECT_EQ(texture->params().height(), 1080);
	// The probed stream has no explicit colorspace. The media is tagged
	// BT.709, which auto-detects to the config's Rec.709 input space and
	// takes precedence over the project default
	EXPECT_EQ(texture->params().colorspace(),
			  QStringLiteral("Rec.709 OETF"));

	const auto *video_job =
		static_cast<const olive::FootageJob *>(texture->job());
	ASSERT_NE(video_job, nullptr);
	EXPECT_EQ(video_job->decoder(), QStringLiteral("ffmpeg"));
	EXPECT_EQ(video_job->filename(), path);
	EXPECT_EQ(video_job->type(), olive::Track::k_video);
	EXPECT_EQ(video_job->length(), olive::Rational(17));

	const olive::FootageJob audio_job =
		table.get(olive::NodeValue::k_samples, QStringLiteral("a:0"))
			.data()
			.value<olive::FootageJob>();
	EXPECT_EQ(audio_job.decoder(), QStringLiteral("ffmpeg"));
	EXPECT_EQ(audio_job.filename(), path);
	EXPECT_EQ(audio_job.type(), olive::Track::k_audio);
	EXPECT_EQ(audio_job.audio_params().sample_rate(), 48000);
	EXPECT_EQ(audio_job.length(), olive::Rational(17));
	EXPECT_EQ(audio_job.cache_path(), cache_path);
}

TEST_F(FootageProbeTest, SecondProbeReadsBackMetadataCache)
{
	const QString path = demo_video_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::Footage *first = add_probed_footage(path);
	ASSERT_TRUE(first->is_valid());

	// The first probe writes a stream metadata cache into the cache location
	const QString cache_file = metadata_cache_file_for(path);
	ASSERT_TRUE(QFileInfo::exists(cache_file));

	// A second footage for the same file loads its metadata from that cache
	// and ends up with identical state
	olive::Footage *second = add_probed_footage(path);
	ASSERT_TRUE(second->is_valid());
	EXPECT_EQ(second->decoder(), first->decoder());
	EXPECT_EQ(second->get_total_stream_count(), first->get_total_stream_count());
	EXPECT_EQ(second->get_video_stream_count(), first->get_video_stream_count());
	EXPECT_EQ(second->get_audio_stream_count(), first->get_audio_stream_count());

	const olive::VideoParams from_cache = second->get_video_params(0);
	const olive::VideoParams probed = first->get_video_params(0);
	EXPECT_EQ(from_cache.stream_index(), probed.stream_index());
	EXPECT_EQ(from_cache.width(), probed.width());
	EXPECT_EQ(from_cache.height(), probed.height());
	EXPECT_EQ(from_cache.frame_rate(), probed.frame_rate());
	EXPECT_EQ(from_cache.time_base(), probed.time_base());
	EXPECT_EQ(from_cache.duration(), probed.duration());
	EXPECT_EQ(from_cache.video_type(), probed.video_type());

	ASSERT_TRUE(second->has_source_start_time());
	EXPECT_EQ(second->source_start_time(), olive::Rational(3600));
	EXPECT_EQ(second->source_start_time_source(), QStringLiteral("timecode"));
}

TEST_F(FootageProbeTest, FilenameChangeToMissingFileClearsProbeState)
{
	const QString path = demo_video_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	olive::Footage *footage = add_probed_footage(path);
	ASSERT_TRUE(footage->is_valid());
	ASSERT_GT(footage->get_total_stream_count(), 0);

	// Pointing the footage at a nonexistent file clears the probed state and
	// the re-probe fails
	footage->set_filename(
		QDir(temp_dir_.path()).filePath(QStringLiteral("gone.mp4")));

	EXPECT_FALSE(footage->is_valid());
	EXPECT_EQ(footage->get_total_stream_count(), 0);
	EXPECT_EQ(footage->get_video_stream_count(), 0);
	EXPECT_EQ(footage->get_audio_stream_count(), 0);
	EXPECT_TRUE(footage->decoder().isEmpty());
	EXPECT_EQ(footage->timestamp(), 0);
	EXPECT_FALSE(footage->has_source_start_time());
}

TEST_F(FootageProbeTest, CheckFootageOnlyRespondsWithActiveWindow)
{
	const QString path = test_image_path();
	ASSERT_TRUE(QFileInfo::exists(path));

	// Work on a copy so the original test asset is untouched
	const QString copy =
		QDir(temp_dir_.path()).filePath(QStringLiteral("image.png"));
	ASSERT_TRUE(QFile::copy(path, copy));

	olive::Footage *footage = add_probed_footage(copy);
	ASSERT_TRUE(footage->is_valid());
	const qint64 probed_timestamp = footage->timestamp();
	ASSERT_GT(probed_timestamp, 0);

	// The file vanishes behind the footage's back
	ASSERT_TRUE(QFile::remove(copy));

	// Without an active window, CheckFootage is a no-op
	ASSERT_TRUE(
		QMetaObject::invokeMethod(footage, "check_footage", Qt::DirectConnection));
	EXPECT_EQ(footage->timestamp(), probed_timestamp);
	EXPECT_TRUE(footage->is_valid());

	// With an active window, CheckFootage notices the missing file and
	// re-probes. The re-probe clears the existing state first, and since the
	// file no longer exists, the footage is left invalid with no streams.
	{
		QWidget window;
		window.show();
		window.activateWindow();
		QCoreApplication::processEvents();
		ASSERT_EQ(qApp->activeWindow(), &window);

		ASSERT_TRUE(QMetaObject::invokeMethod(footage, "check_footage",
											  Qt::DirectConnection));
	}
	ASSERT_EQ(qApp->activeWindow(), nullptr);

	EXPECT_EQ(footage->timestamp(), 0);
	EXPECT_FALSE(footage->is_valid());
	EXPECT_EQ(footage->get_video_stream_count(), 0);
}

TEST_F(FootageProbeTest, ProbingExistingButInvalidMediaStaysInvalid)
{
	const QString path =
		QDir(temp_dir_.path()).filePath(QStringLiteral("fake.mkv"));
	{
		QFile file(path);
		ASSERT_TRUE(file.open(QFile::WriteOnly));
		file.write("OAK_FAKE_MEDIA");
	}

	olive::Footage *footage = add_probed_footage(path);

	// The file exists but no decoder can probe it, so the footage stays
	// invalid
	EXPECT_FALSE(footage->is_valid());
	EXPECT_TRUE(footage->decoder().isEmpty());
	EXPECT_EQ(footage->get_total_stream_count(), 0);
	EXPECT_EQ(footage->get_video_stream_count(), 0);
	EXPECT_EQ(footage->get_audio_stream_count(), 0);
	EXPECT_EQ(footage->get_subtitle_stream_count(), 0);

	// A failed probe is not written to the metadata cache, so future reprobes
	// of the same path probe again instead of reloading an invalid description
	EXPECT_FALSE(QFileInfo::exists(metadata_cache_file_for(path)));
}
