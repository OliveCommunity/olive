#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariant>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "common/filefunctions.h"
#include "core.h"
#include "codec/frame.h"
#include "node/color/colormanager/colormanager.h"
#include "node/globals.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "render/diskmanager.h"
#include "node/project/footage/footagedescription.h"
#include "render/job/footagejob.h"
#include "render/job/generatejob.h"
#include "render/loopmode.h"
#include "render/texture.h"
#include "ui/icons/icons.h"

namespace
{

// Footage subclass that exposes the protected stream mutators of ViewerOutput
// so tests can populate streams without probing real media
class TestableFootage : public olive::Footage {
public:
	using olive::ViewerOutput::add_stream;
	using olive::ViewerOutput::set_stream;
};

// Temporarily overrides an environment variable, restoring the previous state
// on destruction. Used to sandbox the footage metadata cache inside a
// QTemporaryDir.
class ScopedEnvVar {
public:
	ScopedEnvVar(const char *name, const QByteArray &value)
		: name_(name)
		, old_value_(qgetenv(name))
		, had_value_(qEnvironmentVariableIsSet(name))
	{
		qputenv(name_, value);
	}

	~ScopedEnvVar()
	{
		if (had_value_) {
			qputenv(name_, old_value_);
		} else {
			qunsetenv(name_);
		}
	}

private:
	const char *name_;
	QByteArray old_value_;
	bool had_value_;
};

olive::VideoParams make_video_stream(int stream_index)
{
	olive::VideoParams params(1920, 1080, olive::Rational(1, 24),
							  olive::core::PixelFormat::u8, 4);
	params.set_stream_index(stream_index);
	params.set_duration(48); // 2 seconds at 24 fps
	return params;
}

olive::core::AudioParams make_audio_stream(int stream_index)
{
	olive::core::AudioParams params(48000, olive::core::k_channel_layout_stereo,
									olive::core::SampleFormat::f32_p);
	params.set_stream_index(stream_index);
	params.set_duration(96000); // 2 seconds at 48 kHz
	return params;
}

olive::SubtitleParams make_subtitle_stream(int stream_index)
{
	olive::SubtitleParams params;
	params.set_stream_index(stream_index);
	params.push_back(olive::Subtitle(
		olive::TimeRange(olive::Rational(0), olive::Rational(3)),
		QStringLiteral("subtitle text")));
	return params;
}

// Two video streams (one with an explicit colorspace, one without) and one
// audio stream, reported as three source streams in total
olive::FootageDescription make_standard_description()
{
	olive::FootageDescription desc(QStringLiteral("fakedecoder"));

	olive::VideoParams video0 = make_video_stream(0);
	desc.add_video_stream(video0);

	olive::VideoParams video1(1280, 720, olive::Rational(1, 24),
							  olive::core::PixelFormat::u8, 4);
	video1.set_stream_index(1);
	video1.set_duration(48);
	video1.set_colorspace(QStringLiteral("ExplicitSpace"));
	desc.add_video_stream(video1);

	desc.add_audio_stream(make_audio_stream(2));

	desc.set_stream_count(3);
	return desc;
}

QString create_fake_media_file(QTemporaryDir &dir, const QString &name)
{
	const QString path = QDir(dir.path()).filePath(name);
	QFile file(path);
	if (!file.open(QFile::WriteOnly)) {
		return QString();
	}
	file.write("OAK_FAKE_MEDIA");
	file.close();
	return path;
}

// Seeds the footage metadata cache for media_path with desc, then points a
// project-parented Footage at the file so Reprobe() picks the cache up without
// running any real decoders. The caller is responsible for redirecting
// QStandardPaths::CacheLocation into a temporary directory first. Returns
// nullptr if the cache file could not be written.
TestableFootage *probe_footage_from_cache(olive::Project *project,
									   const QString &media_path,
									   const olive::FootageDescription &desc)
{
	const QString cache_location =
		QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	if (cache_location.isEmpty() || !QDir().mkpath(cache_location)) {
		return nullptr;
	}

	const QString cache_file =
		QDir(cache_location)
			.filePath(olive::FileFunctions::get_unique_file_identifier(media_path));
	if (!desc.save(cache_file)) {
		return nullptr;
	}

	auto *footage = new TestableFootage();
	footage->setParent(project);
	footage->set_filename(media_path);
	return footage;
}

olive::NodeGlobals make_globals(
	olive::LoopMode loop_mode = olive::LoopMode::k_loop_mode_off, int divider = 1)
{
	olive::VideoParams vparams(64, 64, olive::Rational(1, 24),
							   olive::core::PixelFormat::u8, 4);
	vparams.set_divider(divider);
	return olive::NodeGlobals(vparams, olive::core::AudioParams(),
							  olive::Rational(0), loop_mode);
}

} // namespace

TEST(FootageStatic, DescribeVideoStreamFormatsVideoAndStillStreams)
{
	olive::VideoParams video = make_video_stream(0);
	EXPECT_EQ(olive::Footage::describe_video_stream(video),
			  QStringLiteral("0: Video - 1920x1080"));

	video.set_video_type(olive::VideoParams::k_video_type_still);
	video.set_stream_index(3);
	EXPECT_EQ(olive::Footage::describe_video_stream(video),
			  QStringLiteral("3: Image - 1920x1080"));
}

TEST(FootageStatic, DescribeAudioStreamContainsIndexAndRate)
{
	olive::core::AudioParams audio = make_audio_stream(1);

	// The %n plural marker is only substituted when a translation is loaded,
	// so assert on the stable parts of the description instead
	const QString description = olive::Footage::describe_audio_stream(audio);
	EXPECT_TRUE(description.startsWith(QStringLiteral("1: Audio")));
	EXPECT_TRUE(description.contains(QStringLiteral("48000Hz")));
}

TEST(FootageStatic, DescribeSubtitleStreamContainsIndex)
{
	olive::SubtitleParams subs = make_subtitle_stream(4);
	EXPECT_EQ(olive::Footage::describe_subtitle_stream(subs),
			  QStringLiteral("4: Subtitle"));
}

TEST(FootageStatic, GetStreamTypeNameCoversAllTrackTypes)
{
	EXPECT_EQ(olive::Footage::get_stream_type_name(olive::Track::k_video),
			  QStringLiteral("Video"));
	EXPECT_EQ(olive::Footage::get_stream_type_name(olive::Track::k_audio),
			  QStringLiteral("Audio"));
	EXPECT_EQ(olive::Footage::get_stream_type_name(olive::Track::k_subtitle),
			  QStringLiteral("Subtitle"));
	EXPECT_EQ(olive::Footage::get_stream_type_name(olive::Track::k_none),
			  QStringLiteral("Unknown"));
	EXPECT_EQ(olive::Footage::get_stream_type_name(olive::Track::k_count),
			  QStringLiteral("Unknown"));
}

TEST(FootageStatic, AdjustTimeByLoopModeReturnsZeroForStillImages)
{
	// Still images never loop, clamp, or drop: the adjusted time is always 0,
	// even for in-bounds times
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(3), olive::LoopMode::k_loop_mode_off,
				  olive::Rational(10), olive::VideoParams::k_video_type_still,
				  olive::Rational(1, 24)),
			  olive::Rational(0));
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(30), olive::LoopMode::k_loop_mode_loop,
				  olive::Rational(10), olive::VideoParams::k_video_type_still,
				  olive::Rational(1, 24)),
			  olive::Rational(0));
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(-1), olive::LoopMode::k_loop_mode_clamp,
				  olive::Rational(10), olive::VideoParams::k_video_type_still,
				  olive::Rational(1, 24)),
			  olive::Rational(0));
}

TEST(FootageStatic, AdjustTimeByLoopModeKeepsInBoundsTime)
{
	for (olive::LoopMode mode : { olive::LoopMode::k_loop_mode_off,
								  olive::LoopMode::k_loop_mode_clamp,
								  olive::LoopMode::k_loop_mode_loop }) {
		EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
					  olive::Rational(3), mode, olive::Rational(10),
					  olive::VideoParams::k_video_type_video,
					  olive::Rational(1, 24)),
				  olive::Rational(3));
	}
}

TEST(FootageStatic, AdjustTimeByLoopModeOffDropsOutOfBoundsTime)
{
	const olive::Rational negative = olive::Footage::adjust_time_by_loop_mode(
		olive::Rational(-1), olive::LoopMode::k_loop_mode_off, olive::Rational(10),
		olive::VideoParams::k_video_type_video, olive::Rational(1, 24));
	EXPECT_TRUE(negative.isNaN());

	// The length itself is already out of bounds
	const olive::Rational at_length = olive::Footage::adjust_time_by_loop_mode(
		olive::Rational(10), olive::LoopMode::k_loop_mode_off, olive::Rational(10),
		olive::VideoParams::k_video_type_video, olive::Rational(1, 24));
	EXPECT_TRUE(at_length.isNaN());
}

TEST(FootageStatic, AdjustTimeByLoopModeClampsToLength)
{
	// Beyond the end, clamp to the last frame (length - timebase)
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(10), olive::LoopMode::k_loop_mode_clamp,
				  olive::Rational(10), olive::VideoParams::k_video_type_video,
				  olive::Rational(1, 24)),
			  olive::Rational(239, 24));

	// Before the start, clamp to 0
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(-5), olive::LoopMode::k_loop_mode_clamp,
				  olive::Rational(10), olive::VideoParams::k_video_type_video,
				  olive::Rational(1, 24)),
			  olive::Rational(0));
}

TEST(FootageStatic, AdjustTimeByLoopModeLoopsAroundLength)
{
	// Single wrap past the end
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(12), olive::LoopMode::k_loop_mode_loop,
				  olive::Rational(10), olive::VideoParams::k_video_type_video,
				  olive::Rational(1, 24)),
			  olive::Rational(2));

	// Multiple wraps past the end
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(25), olive::LoopMode::k_loop_mode_loop,
				  olive::Rational(10), olive::VideoParams::k_video_type_video,
				  olive::Rational(1, 24)),
			  olive::Rational(5));

	// Wraps from before the start
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(-3), olive::LoopMode::k_loop_mode_loop,
				  olive::Rational(10), olive::VideoParams::k_video_type_video,
				  olive::Rational(1, 24)),
			  olive::Rational(7));
	EXPECT_EQ(olive::Footage::adjust_time_by_loop_mode(
				  olive::Rational(-25), olive::LoopMode::k_loop_mode_loop,
				  olive::Rational(10), olive::VideoParams::k_video_type_video,
				  olive::Rational(1, 24)),
			  olive::Rational(5));
}

TEST(FootageStatic, AdjustTimeByLoopModeWithEmptyRangeReturnsNaN)
{
	// Looping an empty range would never terminate; return NaN instead
	EXPECT_TRUE(olive::Footage::adjust_time_by_loop_mode(
					olive::Rational(1), olive::LoopMode::k_loop_mode_loop,
					olive::Rational(0), olive::VideoParams::k_video_type_video,
					olive::Rational(1, 24))
					.isNaN());

	// Clamping a range shorter than one frame has no frame to clamp to
	EXPECT_TRUE(olive::Footage::adjust_time_by_loop_mode(
					olive::Rational(1), olive::LoopMode::k_loop_mode_clamp,
					olive::Rational(0), olive::VideoParams::k_video_type_video,
					olive::Rational(1, 24))
					.isNaN());
}

TEST(FootageStatic, RetranslateSetsInputNames)
{
	TestableFootage footage;
	footage.retranslate();

	EXPECT_EQ(footage.get_input_name(olive::Footage::k_filename_input),
			  QStringLiteral("Filename"));
	EXPECT_EQ(footage.get_input_name(olive::ViewerOutput::k_video_params_input),
			  QStringLiteral("Video Parameters"));
	EXPECT_EQ(footage.get_input_name(olive::ViewerOutput::k_audio_params_input),
			  QStringLiteral("Audio Parameters"));
	EXPECT_EQ(footage.get_input_name(olive::ViewerOutput::k_subtitle_params_input),
			  QStringLiteral("Subtitle Parameters"));
}

class FootageTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		if (!olive::Core::instance()) {
			// Leaked intentionally: Core is process-wide and DiskManager
			// touches it (matches render_diskcache_test).
			new olive::Core();
		}

		// Footage::Value() resolves Project::cache_path(), which goes through
		// the DiskManager singleton
		olive::DiskManager::create_instance();

		olive::ColorManager::set_up_default_config();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();
	}

	void TearDown() override
	{
		project_.reset();
		olive::DiskManager::destroy_instance();
	}

	olive::Footage *add_footage()
	{
		auto *footage = new olive::Footage();
		footage->setParent(project_.get());
		return footage;
	}

	std::unique_ptr<olive::Project> project_;
};

TEST_F(FootageTest, ManuallyAddedStreamsMapBetweenReferencesAndIndices)
{
	TestableFootage footage;

	EXPECT_EQ(footage.add_stream(olive::Track::k_video,
								QVariant::fromValue(make_video_stream(5))),
			  0);
	EXPECT_EQ(footage.add_stream(olive::Track::k_audio,
								QVariant::fromValue(make_audio_stream(2))),
			  0);
	EXPECT_EQ(footage.add_stream(olive::Track::k_subtitle,
								QVariant::fromValue(make_subtitle_stream(7))),
			  0);

	EXPECT_EQ(footage.get_stream_index(olive::Track::k_video, 0), 5);
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_audio, 0), 2);
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_subtitle, 0), 7);
	EXPECT_EQ(footage.get_stream_index(
				  olive::Track::Reference(olive::Track::k_video, 0)),
			  5);
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_none, 0), -1);
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_count, 0), -1);

	// Out-of-range indices report -1 rather than a default-constructed stream
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_video, 1), -1);
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_video, -1), -1);
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_audio, 1), -1);
	EXPECT_EQ(footage.get_stream_index(olive::Track::k_subtitle, 1), -1);

	EXPECT_EQ(footage.get_reference_from_real_index(5),
			  olive::Track::Reference(olive::Track::k_video, 0));
	EXPECT_EQ(footage.get_reference_from_real_index(2),
			  olive::Track::Reference(olive::Track::k_audio, 0));
	EXPECT_EQ(footage.get_reference_from_real_index(7),
			  olive::Track::Reference(olive::Track::k_subtitle, 0));

	const olive::Track::Reference unknown =
		footage.get_reference_from_real_index(99);
	EXPECT_EQ(unknown.type(), olive::Track::k_none);
	EXPECT_EQ(unknown.index(), -1);
}

TEST_F(FootageTest, ConnectedOutputsReflectStreamTypes)
{
	TestableFootage footage;

	EXPECT_EQ(footage.get_connected_texture_output(), nullptr);
	EXPECT_EQ(footage.get_connected_sample_output(), nullptr);

	footage.add_stream(olive::Track::k_video,
					  QVariant::fromValue(make_video_stream(0)));
	EXPECT_EQ(footage.get_connected_texture_output(),
			  static_cast<olive::Node *>(&footage));
	EXPECT_EQ(footage.get_connected_sample_output(), nullptr);

	footage.add_stream(olive::Track::k_audio,
					  QVariant::fromValue(make_audio_stream(1)));
	EXPECT_EQ(footage.get_connected_sample_output(),
			  static_cast<olive::Node *>(&footage));
}

TEST_F(FootageTest, DataRolesForInvalidFootage)
{
	TestableFootage footage;

	EXPECT_EQ(footage.data(olive::Node::tooltip).toString(),
			  QStringLiteral("Invalid"));
	// B1: engine icon sites return icon name strings (mapped to QIcon in the
	// app layer via icon::from_name); invalid footage gets "error".
	EXPECT_EQ(footage.data(olive::Node::icon).toString(),
			  QStringLiteral("error"));

	// With no existing file behind the footage, the time roles fall through
	// to the base class and stay invalid
	EXPECT_FALSE(footage.data(olive::Node::created_time).isValid());
	EXPECT_FALSE(footage.data(olive::Node::modified_time).isValid());
}

TEST_F(FootageTest, TooltipDescribesEnabledStreams)
{
	TestableFootage footage;
	// The file does not exist, so the filename change clears the footage
	// without probing anything
	footage.set_filename(QStringLiteral("/nonexistent/media.mkv"));
	footage.add_stream(olive::Track::k_video,
					  QVariant::fromValue(make_video_stream(0)));
	footage.add_stream(olive::Track::k_audio,
					  QVariant::fromValue(make_audio_stream(1)));
	footage.set_valid();

	QString tip = footage.data(olive::Node::tooltip).toString();
	EXPECT_TRUE(
		tip.contains(QStringLiteral("Filename: /nonexistent/media.mkv")));
	EXPECT_TRUE(tip.contains(QStringLiteral("0: Video - 1920x1080")));
	EXPECT_TRUE(tip.contains(QStringLiteral("Audio")));

	// Disabled streams are omitted from the tooltip
	olive::VideoParams disabled = footage.get_video_params(0);
	disabled.set_enabled(false);
	footage.set_stream(olive::Track::k_video, QVariant::fromValue(disabled), 0);

	tip = footage.data(olive::Node::tooltip).toString();
	EXPECT_FALSE(tip.contains(QStringLiteral("0: Video")));
	EXPECT_TRUE(tip.contains(QStringLiteral("Audio")));
}

TEST_F(FootageTest, IconReflectsPrioritizedStreamType)
{
	// B1: engine icon sites return icon name strings ("video", "audio",
	// "image", "subtitles", "error"); the QIcon mapping lives in the app layer.

	// Footage::data(ICON) only inspects streams once the footage has been
	// probed (total_stream_count_ is set by Reprobe), so each variant is
	// seeded through the metadata cache like a real probe would leave it
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const ScopedEnvVar xdg(
		"XDG_CACHE_HOME",
		QDir(dir.path()).filePath(QStringLiteral("xdg")).toUtf8());

	auto probe = [&](const QString &name,
					 const olive::FootageDescription &desc) {
		const QString media = create_fake_media_file(dir, name);
		EXPECT_FALSE(media.isEmpty());
		TestableFootage *footage =
			probe_footage_from_cache(project_.get(), media, desc);
		EXPECT_NE(footage, nullptr);
		return footage;
	};

	// Invalid footage gets the error icon
	TestableFootage invalid;
	EXPECT_EQ(invalid.data(olive::Node::icon).toString(),
			  QStringLiteral("error"));

	// Real video streams take priority over audio
	olive::FootageDescription video_audio(QStringLiteral("fakedecoder"));
	video_audio.add_video_stream(make_video_stream(0));
	video_audio.add_audio_stream(make_audio_stream(1));
	video_audio.set_stream_count(2);
	TestableFootage *footage = probe(QStringLiteral("video-audio.mkv"), video_audio);
	ASSERT_NE(footage, nullptr);
	EXPECT_EQ(footage->data(olive::Node::icon).toString(),
			  QStringLiteral("video"));

	// Audio still takes priority over a still image stream
	olive::VideoParams still_stream = make_video_stream(0);
	still_stream.set_video_type(olive::VideoParams::k_video_type_still);
	olive::FootageDescription still_audio(QStringLiteral("fakedecoder"));
	still_audio.add_video_stream(still_stream);
	still_audio.add_audio_stream(make_audio_stream(1));
	still_audio.set_stream_count(2);
	TestableFootage *still_and_audio =
		probe(QStringLiteral("still-audio.mkv"), still_audio);
	ASSERT_NE(still_and_audio, nullptr);
	EXPECT_EQ(still_and_audio->data(olive::Node::icon).toString(),
			  QStringLiteral("audio"));

	// A still image without audio hits the image branch
	olive::FootageDescription stills(QStringLiteral("fakedecoder"));
	stills.add_video_stream(still_stream);
	stills.set_stream_count(1);
	TestableFootage *still_only = probe(QStringLiteral("still.mkv"), stills);
	ASSERT_NE(still_only, nullptr);
	EXPECT_EQ(still_only->data(olive::Node::icon).toString(),
			  QStringLiteral("image"));

	// Audio-only footage
	olive::FootageDescription audio(QStringLiteral("fakedecoder"));
	audio.add_audio_stream(make_audio_stream(0));
	audio.set_stream_count(1);
	TestableFootage *audio_only = probe(QStringLiteral("audio.mkv"), audio);
	ASSERT_NE(audio_only, nullptr);
	EXPECT_EQ(audio_only->data(olive::Node::icon).toString(),
			  QStringLiteral("audio"));

	// Subtitle-only footage
	olive::FootageDescription subs(QStringLiteral("fakedecoder"));
	subs.add_subtitle_stream(make_subtitle_stream(0));
	subs.set_stream_count(1);
	TestableFootage *subs_only = probe(QStringLiteral("subs.mkv"), subs);
	ASSERT_NE(subs_only, nullptr);
	EXPECT_EQ(subs_only->data(olive::Node::icon).toString(),
			  QStringLiteral("subtitles"));
}

TEST_F(FootageTest, ProxyChangesMarkProjectModifiedAndEmitSignal)
{
	olive::Footage *footage = add_footage();
	ASSERT_FALSE(project_->is_modified());

	int emissions = 0;
	QObject::connect(footage, &olive::Footage::proxy_settings_changed,
					 [&emissions]() { ++emissions; });

	footage->set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					  olive::ProxyManager::k_proxy_ready, 0, 1, true);
	EXPECT_EQ(emissions, 1);
	EXPECT_TRUE(project_->is_modified());

	// set_proxy_enabled only reacts to actual changes
	project_->set_modified(false);
	footage->set_proxy_enabled(true);
	EXPECT_EQ(emissions, 1);
	EXPECT_FALSE(project_->is_modified());

	footage->set_proxy_enabled(false);
	EXPECT_EQ(emissions, 2);
	EXPECT_TRUE(project_->is_modified());
	EXPECT_FALSE(footage->proxy_enabled());
}

TEST_F(FootageTest, OfflineMediaGeneratesWarningFrame)
{
	olive::Footage *footage = add_footage();
	footage->set_filename(QStringLiteral("/nonexistent/media.mp4"));

	olive::FramePtr frame = olive::Frame::create();
	frame->set_video_params(olive::VideoParams(320, 180, olive::PixelFormat::u8,
											 olive::VideoParams::k_rgba_channel_count));
	frame->allocate();

	footage->generate_frame(frame, olive::GenerateJob());

	// The offline slat must be visible: dark red background/stripes plus
	// white warning text
	bool found_red_pixel = false;
	bool found_bright_pixel = false;
	const auto *data = reinterpret_cast<const uchar *>(frame->data());
	for (int y = 0; y < frame->height(); y++) {
		const uchar *row = data + y * frame->linesize_bytes();
		for (int x = 0; x < frame->width(); x++) {
			const uchar *px = row + x * 4;
			if (px[0] > 40 && px[1] < px[0] / 2 && px[2] < px[0] / 2) {
				found_red_pixel = true;
			}
			if (px[0] > 200 && px[1] > 200 && px[2] > 200) {
				found_bright_pixel = true;
			}
		}
	}
	EXPECT_TRUE(found_red_pixel);
	EXPECT_TRUE(found_bright_pixel);
}

TEST_F(FootageTest, RelinkClearsStaleProxy)
{
	olive::Footage *footage = add_footage();
	footage->set_filename(QStringLiteral("/media/original.mov"));
	footage->set_proxy(QStringLiteral("/cache/proxy/example.mp4"),
					  olive::ProxyManager::k_proxy_ready, 0, 1, true);
	ASSERT_FALSE(footage->proxy_path().isEmpty());

	// Relinking to a different file must invalidate the proxy that was
	// generated from the old source (Footage::clear() drops it when the
	// filename input changes)
	footage->set_filename(QStringLiteral("/media/relinked.mov"));
	EXPECT_TRUE(footage->proxy_path().isEmpty());
	EXPECT_FALSE(footage->proxy_enabled());
	EXPECT_EQ(footage->proxy_state(), olive::ProxyManager::k_proxy_missing);
}

TEST_F(FootageTest, ReprobeRestoresStreamsFromMetadataCache)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const ScopedEnvVar xdg(
		"XDG_CACHE_HOME",
		QDir(dir.path()).filePath(QStringLiteral("xdg")).toUtf8());

	const QString media = create_fake_media_file(dir, QStringLiteral("fake.mkv"));
	ASSERT_FALSE(media.isEmpty());

	TestableFootage *footage = probe_footage_from_cache(
		project_.get(), media, make_standard_description());
	ASSERT_NE(footage, nullptr);

	EXPECT_TRUE(footage->is_valid());
	EXPECT_EQ(footage->decoder(), QStringLiteral("fakedecoder"));
	EXPECT_EQ(footage->get_total_stream_count(), 3);
	EXPECT_EQ(footage->get_video_stream_count(), 2);
	EXPECT_EQ(footage->get_audio_stream_count(), 1);
	EXPECT_EQ(footage->get_subtitle_stream_count(), 0);

	EXPECT_EQ(footage->get_stream_index(olive::Track::k_video, 0), 0);
	EXPECT_EQ(footage->get_stream_index(olive::Track::k_video, 1), 1);
	EXPECT_EQ(footage->get_stream_index(olive::Track::k_audio, 0), 2);
	EXPECT_EQ(footage->get_reference_from_real_index(2),
			  olive::Track::Reference(olive::Track::k_audio, 0));

	EXPECT_EQ(footage->get_connected_texture_output(),
			  static_cast<olive::Node *>(footage));
	EXPECT_EQ(footage->get_connected_sample_output(),
			  static_cast<olive::Node *>(footage));

	// The file behind the footage exists, so both time roles are reported
	const QVariant modified = footage->data(olive::Node::modified_time);
	ASSERT_TRUE(modified.isValid());
	EXPECT_EQ(modified.toLongLong(),
			  QFileInfo(media).lastModified().toSecsSinceEpoch());
	EXPECT_TRUE(footage->data(olive::Node::created_time).isValid());
}

TEST_F(FootageTest, VerifyLengthUsesStreamDurations)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const ScopedEnvVar xdg(
		"XDG_CACHE_HOME",
		QDir(dir.path()).filePath(QStringLiteral("xdg")).toUtf8());

	const QString media = create_fake_media_file(dir, QStringLiteral("fake.mkv"));
	ASSERT_FALSE(media.isEmpty());

	TestableFootage *footage = probe_footage_from_cache(
		project_.get(), media, make_standard_description());
	ASSERT_NE(footage, nullptr);

	// Both streams describe two seconds of media
	footage->verify_length();
	EXPECT_EQ(footage->get_video_length(), olive::Rational(2));
	EXPECT_EQ(footage->get_audio_length(), olive::Rational(2));
	EXPECT_EQ(footage->get_length(), olive::Rational(2));
}

TEST_F(FootageTest, ValueSkipsMissingFiles)
{
	TestableFootage footage;

	olive::NodeValueRow row;
	row.insert(olive::Footage::k_filename_input,
			   olive::NodeValue(olive::NodeValue::k_file,
								QStringLiteral("/nonexistent/media.mkv")));

	olive::NodeValueTable table;
	footage.value(row, make_globals(), &table);

	EXPECT_TRUE(table.isEmpty());
}

TEST_F(FootageTest, ValuePushesOnlyLengthWhenNoStreams)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const QString media = create_fake_media_file(dir, QStringLiteral("empty.mkv"));
	ASSERT_FALSE(media.isEmpty());

	TestableFootage footage;

	olive::NodeValueRow row;
	row.insert(olive::Footage::k_filename_input,
			   olive::NodeValue(olive::NodeValue::k_file, media));

	olive::NodeValueTable table;
	footage.value(row, make_globals(), &table);

	// The file exists but no streams were ever probed, so only the (zero)
	// length is pushed
	ASSERT_EQ(table.count(), 1);
	const olive::NodeValue length =
		table.get(olive::NodeValue::k_rational, QStringLiteral("length"));
	EXPECT_EQ(length.type(), olive::NodeValue::k_rational);
	EXPECT_EQ(length.to_rational(), olive::Rational(0));
	EXPECT_EQ(length.source(), static_cast<const olive::Node *>(&footage));
}

TEST_F(FootageTest, ValuePushesStreamJobs)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const ScopedEnvVar xdg(
		"XDG_CACHE_HOME",
		QDir(dir.path()).filePath(QStringLiteral("xdg")).toUtf8());

	const QString media = create_fake_media_file(dir, QStringLiteral("fake.mkv"));
	ASSERT_FALSE(media.isEmpty());

	TestableFootage *footage = probe_footage_from_cache(
		project_.get(), media, make_standard_description());
	ASSERT_NE(footage, nullptr);
	footage->verify_length();

	// The colorspace fallback reads the project default, and the audio cache
	// path comes from the project's cache settings
	project_->set_default_input_color_space(QStringLiteral("TestInputSpace"));
	project_->set_cache_location_setting(olive::Project::k_cache_custom_path);
	const QString cache_path =
		QDir(dir.path()).filePath(QStringLiteral("cache"));
	project_->set_custom_cache_path(cache_path);

	olive::NodeValueRow row;
	row.insert(olive::Footage::k_filename_input,
			   olive::NodeValue(olive::NodeValue::k_file, media));

	// A divider > 1 routes through the target-resolution divider calculation
	const olive::NodeGlobals globals =
		make_globals(olive::LoopMode::k_loop_mode_loop, 2);

	olive::NodeValueTable table;
	footage->value(row, globals, &table);

	// Length, two texture jobs, and one sample job
	EXPECT_EQ(table.count(), 4);

	const olive::NodeValue length =
		table.get(olive::NodeValue::k_rational, QStringLiteral("length"));
	EXPECT_EQ(length.to_rational(), olive::Rational(2));

	// A stream without a colorspace falls back to the project default
	const olive::TexturePtr tex0 =
		table.get(olive::NodeValue::k_texture, QStringLiteral("v:0"))
			.to_texture();
	ASSERT_NE(tex0, nullptr);
	EXPECT_EQ(tex0->params().colorspace(), QStringLiteral("TestInputSpace"));
	// min(calculated divider for 32x32 from 1920x1080, requested 2)
	EXPECT_EQ(tex0->params().divider(), 2);

	// An explicit colorspace survives
	const olive::TexturePtr tex1 =
		table.get(olive::NodeValue::k_texture, QStringLiteral("v:1"))
			.to_texture();
	ASSERT_NE(tex1, nullptr);
	EXPECT_EQ(tex1->params().colorspace(), QStringLiteral("ExplicitSpace"));

	const olive::NodeValue samples =
		table.get(olive::NodeValue::k_samples, QStringLiteral("a:0"));
	ASSERT_EQ(samples.type(), olive::NodeValue::k_samples);
	const olive::FootageJob audio_job =
		samples.data().value<olive::FootageJob>();
	EXPECT_EQ(audio_job.filename(), media);
	EXPECT_EQ(audio_job.decoder(), QStringLiteral("fakedecoder"));
	EXPECT_EQ(audio_job.type(), olive::Track::k_audio);
	EXPECT_EQ(audio_job.audio_params().sample_rate(), 48000);
	EXPECT_EQ(audio_job.length(), olive::Rational(2));
	EXPECT_EQ(audio_job.loop_mode(), olive::LoopMode::k_loop_mode_loop);
	EXPECT_EQ(audio_job.time(), globals.time());
	EXPECT_EQ(audio_job.cache_path(), cache_path);

	// With a divider of 1, everything renders at full resolution
	olive::NodeValueTable full_res;
	footage->value(row, make_globals(), &full_res);
	const olive::TexturePtr full_res_tex =
		full_res.get(olive::NodeValue::k_texture, QStringLiteral("v:0"))
			.to_texture();
	ASSERT_NE(full_res_tex, nullptr);
	EXPECT_EQ(full_res_tex->params().divider(), 1);
}

TEST_F(FootageTest, ValueAttachesReadyProxyToJobs)
{
	QTemporaryDir dir;
	ASSERT_TRUE(dir.isValid());
	const ScopedEnvVar xdg(
		"XDG_CACHE_HOME",
		QDir(dir.path()).filePath(QStringLiteral("xdg")).toUtf8());

	const QString media = create_fake_media_file(dir, QStringLiteral("fake.mkv"));
	ASSERT_FALSE(media.isEmpty());

	TestableFootage *footage = probe_footage_from_cache(
		project_.get(), media, make_standard_description());
	ASSERT_NE(footage, nullptr);
	footage->verify_length();

	// A ready proxy is simply an existing proxy file with no .working
	// sibling; the .a1. marker declares that it contains audio
	const QString proxy = QDir(dir.path())
							  .filePath(QStringLiteral(
								  "proxy-0.1920x1080.v1.a1.mp4"));
	{
		QFile proxy_file(proxy);
		ASSERT_TRUE(proxy_file.open(QFile::WriteOnly));
	}
	footage->set_proxy(proxy, olive::ProxyManager::k_proxy_ready, 0, 1, true);

	olive::NodeValueRow row;
	row.insert(olive::Footage::k_filename_input,
			   olive::NodeValue(olive::NodeValue::k_file, media));

	olive::NodeValueTable table;
	footage->value(row, make_globals(), &table);

	// The proxied video stream (real index 0) gets the proxy at stream 0
	const olive::TexturePtr tex0 =
		table.get(olive::NodeValue::k_texture, QStringLiteral("v:0"))
			.to_texture();
	ASSERT_NE(tex0, nullptr);
	const auto *video0_job =
		static_cast<const olive::FootageJob *>(tex0->job());
	ASSERT_NE(video0_job, nullptr);
	EXPECT_TRUE(video0_job->has_proxy());
	EXPECT_EQ(video0_job->proxy_filename(), proxy);
	EXPECT_EQ(video0_job->proxy_decoder(), QStringLiteral("ffmpeg"));
	EXPECT_EQ(video0_job->proxy_stream_index(), 0);

	// Other video streams are unaffected
	const olive::TexturePtr tex1 =
		table.get(olive::NodeValue::k_texture, QStringLiteral("v:1"))
			.to_texture();
	ASSERT_NE(tex1, nullptr);
	const auto *video1_job =
		static_cast<const olive::FootageJob *>(tex1->job());
	ASSERT_NE(video1_job, nullptr);
	EXPECT_FALSE(video1_job->has_proxy());

	// The first audio stream follows the video stream inside the proxy file
	const olive::FootageJob audio_job =
		table.get(olive::NodeValue::k_samples, QStringLiteral("a:0"))
			.data()
			.value<olive::FootageJob>();
	EXPECT_TRUE(audio_job.has_proxy());
	EXPECT_EQ(audio_job.proxy_filename(), proxy);
	EXPECT_EQ(audio_job.proxy_stream_index(), 1);

	// Disabling the proxy detaches it from subsequent jobs
	footage->set_proxy_enabled(false);
	olive::NodeValueTable no_proxy;
	footage->value(row, make_globals(), &no_proxy);
	const olive::TexturePtr no_proxy_tex =
		no_proxy.get(olive::NodeValue::k_texture, QStringLiteral("v:0"))
			.to_texture();
	ASSERT_NE(no_proxy_tex, nullptr);
	const auto *no_proxy_job =
		static_cast<const olive::FootageJob *>(no_proxy_tex->job());
	ASSERT_NE(no_proxy_job, nullptr);
	EXPECT_FALSE(no_proxy_job->has_proxy());
}

TEST_F(FootageTest, SaveCustomPersistsSourceStartTime)
{
	olive::Footage footage;
	footage.set_timestamp(7);
	footage.set_source_start_time(olive::Rational(3600), QStringLiteral("manual"));

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	footage.save_custom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	EXPECT_TRUE(xml.contains(QStringLiteral("sourcestarttime")));
	EXPECT_TRUE(xml.contains(QStringLiteral("source=\"manual\"")));
	EXPECT_TRUE(xml.contains(QStringLiteral("3600/1")));

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));

	olive::Footage loaded;
	ASSERT_TRUE(loaded.load_custom(&reader, nullptr));
	ASSERT_TRUE(loaded.has_source_start_time());
	EXPECT_EQ(loaded.source_start_time(), olive::Rational(3600));
	EXPECT_EQ(loaded.source_start_time_source(), QStringLiteral("manual"));
	EXPECT_EQ(loaded.timestamp(), 7);
}
