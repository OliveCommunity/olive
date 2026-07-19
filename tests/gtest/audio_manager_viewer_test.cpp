/*
 * Oak Video Editor - AudioManager + ViewerOutput headless tests
 *
 * Covers the CPU-safe surface of olive::AudioManager (PortAudio device
 * bookkeeping that never opens a stream) and olive::ViewerOutput (parameter
 * setters/getters, stream arrays, connected-output resolution, signals and
 * XML (de)serialization). Anything that requires an actual audio output or
 * input stream is intentionally not exercised here.
 */

#include <gtest/gtest.h>

#include <memory>

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "audio/audiomanager.h"
#include "config/config.h"
#include "node/block/clip/clip.h"
#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/globals.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/sampleformat.h"

// ============================================================================
// AudioManager (no audio hardware required: no stream is ever opened)
// ============================================================================

class AudioManagerTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::AudioManager::create_instance();
		ASSERT_NE(olive::AudioManager::instance(), nullptr);
	}

	void TearDown() override
	{
		olive::AudioManager::destroy_instance();
		EXPECT_EQ(olive::AudioManager::instance(), nullptr);
	}
};

TEST_F(AudioManagerTest, InstanceLifecycle)
{
	// The fixture already created the instance; creating again must be a no-op
	olive::AudioManager *first = olive::AudioManager::instance();
	olive::AudioManager::create_instance();
	EXPECT_EQ(olive::AudioManager::instance(), first);

	// The stored indices are either paNoDevice or a valid device index with
	// channels in the appropriate direction
	const PaDeviceIndex output =
		olive::AudioManager::instance()->get_output_device();
	const PaDeviceIndex input = olive::AudioManager::instance()->get_input_device();

	if (Pa_GetDeviceCount() == 0) {
		// No devices exist, so nothing could have been selected
		EXPECT_EQ(output, paNoDevice);
		EXPECT_EQ(input, paNoDevice);
	} else {
		if (output != paNoDevice) {
			ASSERT_GE(output, 0);
			ASSERT_LT(output, Pa_GetDeviceCount());
			EXPECT_GT(Pa_GetDeviceInfo(output)->maxOutputChannels, 0);
		}
		if (input != paNoDevice) {
			ASSERT_GE(input, 0);
			ASSERT_LT(input, Pa_GetDeviceCount());
			EXPECT_GT(Pa_GetDeviceInfo(input)->maxInputChannels, 0);
		}
	}
}

TEST_F(AudioManagerTest, SetAndGetNoDevice)
{
	olive::AudioManager::instance()->set_output_device(paNoDevice);
	EXPECT_EQ(olive::AudioManager::instance()->get_output_device(), paNoDevice);

	olive::AudioManager::instance()->set_input_device(paNoDevice);
	EXPECT_EQ(olive::AudioManager::instance()->get_input_device(), paNoDevice);
}

TEST_F(AudioManagerTest, PushToOutputWithoutDeviceFails)
{
	olive::AudioManager::instance()->set_output_device(paNoDevice);

	const olive::core::AudioParams params(
		48000, olive::core::k_channel_layout_stereo,
		olive::core::SampleFormat::f32_p);
	const QByteArray samples(1024, 0);

	QString error;
	EXPECT_FALSE(
		olive::AudioManager::instance()->push_to_output(params, samples, &error));
	EXPECT_EQ(error, QStringLiteral("No output device is set"));

	// A null error pointer must be tolerated too
	EXPECT_FALSE(
		olive::AudioManager::instance()->push_to_output(params, samples, nullptr));
}

TEST_F(AudioManagerTest, StartRecordingWithoutInputDeviceFails)
{
	olive::AudioManager::instance()->set_input_device(paNoDevice);

	// Fails before any encoder or PortAudio stream is created
	QString error;
	EXPECT_FALSE(
		olive::AudioManager::instance()->start_recording(olive::EncodingParams(),
														&error));

	// Tearing down a recording that never started must be harmless
	olive::AudioManager::instance()->stop_recording();
}

TEST_F(AudioManagerTest, OutputControlsWithoutStreamAreNoOps)
{
	olive::AudioManager::instance()->set_output_device(paNoDevice);

	// No output stream is open; all of these must be harmless no-ops
	olive::AudioManager::instance()->stop_output();
	olive::AudioManager::instance()->clear_buffered_output();
	olive::AudioManager::instance()->set_output_notify_interval(64);
	olive::AudioManager::instance()->set_output_notify_interval(0);

	// ...and they must not disturb the device bookkeeping
	EXPECT_EQ(olive::AudioManager::instance()->get_output_device(), paNoDevice);
}

TEST_F(AudioManagerTest, HardResetKeepsManagerUsable)
{
	olive::AudioManager::instance()->hard_reset();

	// Device bookkeeping must survive a PortAudio terminate/init cycle
	olive::AudioManager::instance()->set_output_device(paNoDevice);
	EXPECT_EQ(olive::AudioManager::instance()->get_output_device(), paNoDevice);
}

TEST_F(AudioManagerTest, FindDeviceByNameFallsBackForUnknownName)
{
	// A name that matches nothing must never produce a garbage index. When no
	// devices exist there is nothing to fall back to and the result must be
	// exactly paNoDevice; otherwise the fallback is a preferred/default
	// device, i.e. a valid index or paNoDevice.
	const PaDeviceIndex bogus_output = olive::AudioManager::find_device_by_name(
		QStringLiteral("OakNoSuchAudioDevice12345"), true);
	const PaDeviceIndex bogus_input = olive::AudioManager::find_device_by_name(
		QStringLiteral("OakNoSuchAudioDevice12345"), false);

	if (Pa_GetDeviceCount() == 0) {
		EXPECT_EQ(bogus_output, paNoDevice);
		EXPECT_EQ(bogus_input, paNoDevice);
	} else {
		EXPECT_TRUE(bogus_output == paNoDevice ||
					(bogus_output >= 0 && bogus_output < Pa_GetDeviceCount()));
		EXPECT_TRUE(bogus_input == paNoDevice ||
					(bogus_input >= 0 && bogus_input < Pa_GetDeviceCount()));
	}
}

TEST_F(AudioManagerTest, FindDeviceByNameFindsExactMatch)
{
	if (Pa_GetDeviceCount() == 0) {
		GTEST_SKIP() << "No PortAudio devices available on this system";
	}

	// Searching the exact name of a device must return that device's index.
	// On Linux a match on a non-preferred backend (e.g. ALSA) may legitimately
	// be upgraded to a preferred device, so only a device that already sits on
	// a preferred host API (PipeWire/JACK/PulseAudio) gives an exact contract.
	for (PaDeviceIndex i = 0, end = Pa_GetDeviceCount(); i < end; i++) {
		const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
		if (!info || !info->maxOutputChannels) {
			continue;
		}

#ifdef Q_OS_LINUX
		const PaHostApiInfo *api = Pa_GetHostApiInfo(info->hostApi);
		if (!api) {
			continue;
		}
		const QString api_name = QString::fromLatin1(api->name);
		if (!api_name.contains(QStringLiteral("PipeWire"),
							   Qt::CaseInsensitive) &&
			!api_name.contains(QStringLiteral("JACK"), Qt::CaseInsensitive) &&
			!api_name.contains(QStringLiteral("PulseAudio"),
							   Qt::CaseInsensitive)) {
			continue;
		}
#endif

		EXPECT_EQ(olive::AudioManager::find_device_by_name(
					  QString::fromLatin1(info->name), true),
				  i);
		return;
	}

	GTEST_SKIP() << "No output device on a preferred host API on this system";
}

TEST_F(AudioManagerTest, FindConfigDeviceByNameMatchesConfiguredLookup)
{
	// The config-driven lookup must be exactly FindDeviceByName applied to the
	// configured name, and must never return a garbage index
	const PaDeviceIndex output =
		olive::AudioManager::find_config_device_by_name(true);
	const PaDeviceIndex input =
		olive::AudioManager::find_config_device_by_name(false);

	EXPECT_EQ(output,
			  olive::AudioManager::find_device_by_name(
				  olive::Config::current()[QStringLiteral("AudioOutput")]
					  .toString(),
				  true));
	EXPECT_EQ(input,
			  olive::AudioManager::find_device_by_name(
				  olive::Config::current()[QStringLiteral("AudioInput")]
					  .toString(),
				  false));

	if (Pa_GetDeviceCount() == 0) {
		EXPECT_EQ(output, paNoDevice);
		EXPECT_EQ(input, paNoDevice);
	}
}

TEST_F(AudioManagerTest, PortAudioParamsReflectAudioParams)
{
	if (Pa_GetDeviceCount() <= 0) {
		GTEST_SKIP() << "No PortAudio devices available on this system";
	}

	const olive::core::AudioParams params(
		48000, olive::core::k_channel_layout_stereo,
		olive::core::SampleFormat::f32);
	const PaStreamParameters p =
		olive::AudioManager::get_port_audio_params(params, 0);

	EXPECT_EQ(p.channelCount, 2);
	EXPECT_EQ(p.device, 0);
	EXPECT_EQ(p.sampleFormat, PaSampleFormat(paFloat32));
	EXPECT_EQ(p.hostApiSpecificStreamInfo, nullptr);
	EXPECT_GE(p.suggestedLatency, 0.0);
}

TEST_F(AudioManagerTest, PortAudioParamsMapsSampleFormats)
{
	if (Pa_GetDeviceCount() <= 0) {
		GTEST_SKIP() << "No PortAudio devices available on this system";
	}

	const auto format_for = [](olive::core::SampleFormat f) {
		const olive::core::AudioParams params(
			48000, olive::core::k_channel_layout_mono, f);
		return olive::AudioManager::get_port_audio_params(params, 0).sampleFormat;
	};

	// Packed and planar variants of the same depth map to the same flag
	EXPECT_EQ(format_for(olive::core::SampleFormat::u8),
			  PaSampleFormat(paUInt8));
	EXPECT_EQ(format_for(olive::core::SampleFormat::u8_p),
			  PaSampleFormat(paUInt8));
	EXPECT_EQ(format_for(olive::core::SampleFormat::s16),
			  PaSampleFormat(paInt16));
	EXPECT_EQ(format_for(olive::core::SampleFormat::s16_p),
			  PaSampleFormat(paInt16));
	EXPECT_EQ(format_for(olive::core::SampleFormat::s32),
			  PaSampleFormat(paInt32));
	EXPECT_EQ(format_for(olive::core::SampleFormat::s32_p),
			  PaSampleFormat(paInt32));
	EXPECT_EQ(format_for(olive::core::SampleFormat::f32),
			  PaSampleFormat(paFloat32));
	EXPECT_EQ(format_for(olive::core::SampleFormat::f32_p),
			  PaSampleFormat(paFloat32));

	// 64-bit depths have no PortAudio equivalent and map to paCustomFormat(0)
	EXPECT_EQ(format_for(olive::core::SampleFormat::s64), PaSampleFormat(0));
	EXPECT_EQ(format_for(olive::core::SampleFormat::f64), PaSampleFormat(0));
	EXPECT_EQ(format_for(olive::core::SampleFormat::invalid),
			  PaSampleFormat(0));
}

// ============================================================================
// ViewerOutput
// ============================================================================

// AddStream/SetStream are protected on ViewerOutput (Sequence is the intended
// caller); expose them so the stream-array bookkeeping can be tested directly
class TestViewerOutput : public olive::ViewerOutput {
public:
	using olive::ViewerOutput::ViewerOutput;
	using olive::ViewerOutput::add_stream;
	using olive::ViewerOutput::set_stream;
};

class ViewerOutputTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		olive::ColorManager::set_up_default_config();

		project_ = std::make_unique<olive::Project>();
		project_->initialize();

		viewer_ = new TestViewerOutput();
		viewer_->setParent(project_.get());
	}

	template <typename T> T *add_node()
	{
		T *node = new T();
		node->setParent(project_.get());
		return node;
	}

	std::unique_ptr<olive::Project> project_;
	TestViewerOutput *viewer_;
};

TEST_F(ViewerOutputTest, DefaultConstruction)
{
	EXPECT_EQ(viewer_->name(), QStringLiteral("Viewer"));
	EXPECT_EQ(viewer_->id(),
			  QStringLiteral("org.olivevideoeditor.Olive.vieweroutput"));
	EXPECT_TRUE(viewer_->category().contains(olive::Node::k_category_output));

	// One default video and audio stream, no subtitle streams
	EXPECT_EQ(viewer_->get_video_stream_count(), 1);
	EXPECT_EQ(viewer_->get_audio_stream_count(), 1);
	EXPECT_EQ(viewer_->get_subtitle_stream_count(), 0);
	EXPECT_EQ(viewer_->get_total_stream_count(), 2);

	EXPECT_NE(viewer_->get_work_area(), nullptr);
	EXPECT_NE(viewer_->get_markers(), nullptr);

	EXPECT_EQ(viewer_->get_playhead(), olive::Rational(0));
	EXPECT_EQ(viewer_->get_length(), olive::Rational(0));
	EXPECT_EQ(viewer_->get_video_length(), olive::Rational(0));
	EXPECT_EQ(viewer_->get_audio_length(), olive::Rational(0));

	EXPECT_EQ(viewer_->get_connected_texture_output(), nullptr);
	EXPECT_EQ(viewer_->get_connected_sample_output(), nullptr);
	EXPECT_EQ(viewer_->get_connected_waveform(), nullptr);

	// The autocache API is currently a stub that always reports disabled
	EXPECT_FALSE(viewer_->is_video_auto_cache_enabled());
}

TEST_F(ViewerOutputTest, SetAndGetVideoParams)
{
	const olive::VideoParams vp(1920, 1080, olive::Rational(1, 30),
								olive::PixelFormat::u8, 4);
	viewer_->set_video_params(vp);

	EXPECT_EQ(viewer_->get_video_params(), vp);
	EXPECT_EQ(viewer_->get_video_params().width(), 1920);
	EXPECT_EQ(viewer_->get_video_params().height(), 1080);

	// Out-of-range indices return invalid params instead of garbage
	EXPECT_FALSE(viewer_->get_video_params(5).is_valid());
}

TEST_F(ViewerOutputTest, SetAndGetAudioParams)
{
	const olive::core::AudioParams ap(48000, olive::core::k_channel_layout_stereo,
									  olive::core::SampleFormat::f32_p);
	viewer_->set_audio_params(ap);

	EXPECT_EQ(viewer_->get_audio_params(), ap);

	// The default-constructed stream uses the viewer's default sample format
	const olive::core::SampleFormat default_format =
		olive::ViewerOutput::k_default_sample_format;
	EXPECT_EQ(viewer_->get_audio_params().format(), default_format);

	// Out-of-range indices return invalid params instead of garbage
	EXPECT_FALSE(viewer_->get_audio_params(9).is_valid());
}

TEST_F(ViewerOutputTest, SetAndGetSubtitleParams)
{
	olive::SubtitleParams subs;
	subs.push_back(olive::Subtitle(
		olive::TimeRange(olive::Rational(0), olive::Rational(2)),
		QStringLiteral("hello")));

	EXPECT_EQ(viewer_->add_stream(olive::Track::k_subtitle,
								 QVariant::fromValue(subs)),
			  0);
	EXPECT_EQ(viewer_->get_subtitle_stream_count(), 1);

	ASSERT_TRUE(viewer_->get_subtitle_params(0).is_valid());
	EXPECT_EQ(viewer_->get_subtitle_params(0).duration(), olive::Rational(2));
	EXPECT_TRUE(viewer_->has_enabled_subtitle_streams());

	// Out-of-range indices return invalid (empty) params
	EXPECT_FALSE(viewer_->get_subtitle_params(3).is_valid());
}

TEST_F(ViewerOutputTest, VideoParamSignals)
{
	int size_emissions = 0;
	int frame_rate_emissions = 0;
	int pixel_aspect_emissions = 0;
	int interlacing_emissions = 0;
	int params_emissions = 0;
	olive::Rational emitted_frame_rate;
	QObject::connect(viewer_, &olive::ViewerOutput::size_changed,
					 [&size_emissions](int, int) { ++size_emissions; });
	QObject::connect(viewer_, &olive::ViewerOutput::frame_rate_changed,
					 [&frame_rate_emissions, &emitted_frame_rate](
						 const olive::Rational &r) {
						 ++frame_rate_emissions;
						 emitted_frame_rate = r;
					 });
	QObject::connect(viewer_, &olive::ViewerOutput::pixel_aspect_changed,
					 [&pixel_aspect_emissions](const olive::Rational &) {
						 ++pixel_aspect_emissions;
					 });
	QObject::connect(viewer_, &olive::ViewerOutput::interlacing_changed,
					 [&interlacing_emissions](olive::VideoParams::Interlacing) {
						 ++interlacing_emissions;
					 });
	QObject::connect(viewer_, &olive::ViewerOutput::video_params_changed,
					 [&params_emissions]() { ++params_emissions; });

	// Every aspect differs from the cached defaults, so all signals fire once
	const olive::VideoParams vp(1280, 720, olive::Rational(1, 60),
								olive::PixelFormat::u8, 4, olive::Rational(2),
								olive::VideoParams::k_interlaced_top_first);
	viewer_->set_video_params(vp);

	EXPECT_EQ(size_emissions, 1);
	EXPECT_EQ(frame_rate_emissions, 1);
	EXPECT_EQ(emitted_frame_rate, olive::Rational(60, 1));
	EXPECT_EQ(pixel_aspect_emissions, 1);
	EXPECT_EQ(interlacing_emissions, 1);
	EXPECT_EQ(params_emissions, 1);

	// Setting identical params only re-emits the unconditional change signal
	viewer_->set_video_params(vp);

	EXPECT_EQ(size_emissions, 1);
	EXPECT_EQ(frame_rate_emissions, 1);
	EXPECT_EQ(pixel_aspect_emissions, 1);
	EXPECT_EQ(interlacing_emissions, 1);
	EXPECT_EQ(params_emissions, 2);
}

TEST_F(ViewerOutputTest, AudioParamSignals)
{
	int sample_rate_emissions = 0;
	int params_emissions = 0;
	int emitted_sample_rate = 0;
	QObject::connect(viewer_, &olive::ViewerOutput::sample_rate_changed,
					 [&sample_rate_emissions, &emitted_sample_rate](int sr) {
						 ++sample_rate_emissions;
						 emitted_sample_rate = sr;
					 });
	QObject::connect(viewer_, &olive::ViewerOutput::audio_params_changed,
					 [&params_emissions]() { ++params_emissions; });

	const olive::core::AudioParams ap(44100, olive::core::k_channel_layout_stereo,
									  olive::core::SampleFormat::f32_p);
	viewer_->set_audio_params(ap);

	EXPECT_EQ(sample_rate_emissions, 1);
	EXPECT_EQ(emitted_sample_rate, 44100);
	EXPECT_EQ(params_emissions, 1);

	// Same sample rate again: no SampleRateChanged, but AudioParamsChanged
	viewer_->set_audio_params(ap);

	EXPECT_EQ(sample_rate_emissions, 1);
	EXPECT_EQ(params_emissions, 2);
}

TEST_F(ViewerOutputTest, SetPlayheadEmitsPlayheadChanged)
{
	int emissions = 0;
	olive::Rational emitted;
	QObject::connect(viewer_, &olive::ViewerOutput::playhead_changed,
					 [&emissions, &emitted](const olive::Rational &t) {
						 ++emissions;
						 emitted = t;
					 });

	viewer_->set_playhead(olive::Rational(3, 2));

	EXPECT_EQ(emissions, 1);
	EXPECT_EQ(emitted, olive::Rational(3, 2));
	EXPECT_EQ(viewer_->get_playhead(), olive::Rational(3, 2));
}

TEST_F(ViewerOutputTest, VerifyLengthWithoutConnectionsStaysZero)
{
	int length_emissions = 0;
	QObject::connect(viewer_, &olive::ViewerOutput::length_changed,
					 [&length_emissions](const olive::Rational &) {
						 ++length_emissions;
					 });

	viewer_->verify_length();
	viewer_->verify_length();

	// Nothing is connected, so all lengths stay zero and nothing is emitted
	EXPECT_EQ(viewer_->get_length(), olive::Rational(0));
	EXPECT_EQ(viewer_->get_video_length(), olive::Rational(0));
	EXPECT_EQ(viewer_->get_audio_length(), olive::Rational(0));
	EXPECT_EQ(length_emissions, 0);

	EXPECT_EQ(viewer_->get_video_cache_range(),
			  olive::TimeRange(olive::Rational(0), olive::Rational(0)));
	EXPECT_EQ(viewer_->get_audio_cache_range(),
			  olive::TimeRange(olive::Rational(0), olive::Rational(0)));
}

TEST_F(ViewerOutputTest, StreamEnableDisable)
{
	ASSERT_TRUE(viewer_->has_enabled_video_streams());
	ASSERT_TRUE(viewer_->has_enabled_audio_streams());

	olive::VideoParams vp = viewer_->get_video_params();
	vp.set_enabled(false);
	viewer_->set_video_params(vp);

	EXPECT_FALSE(viewer_->has_enabled_video_streams());
	EXPECT_FALSE(viewer_->get_first_enabled_video_stream().is_valid());
	EXPECT_TRUE(viewer_->get_enabled_video_streams().isEmpty());

	olive::core::AudioParams ap = viewer_->get_audio_params();
	ap.set_enabled(false);
	viewer_->set_audio_params(ap);

	EXPECT_FALSE(viewer_->has_enabled_audio_streams());
	EXPECT_FALSE(viewer_->get_first_enabled_audio_stream().is_valid());
	EXPECT_TRUE(viewer_->get_enabled_audio_streams().isEmpty());
	EXPECT_TRUE(viewer_->get_enabled_streams_as_references().isEmpty());
}

TEST_F(ViewerOutputTest, AddAndSetStreams)
{
	const olive::VideoParams vp2(640, 360, olive::Rational(1, 25),
								 olive::PixelFormat::u8, 4);

	EXPECT_EQ(viewer_->add_stream(olive::Track::k_video,
								 QVariant::fromValue(vp2)),
			  1);
	EXPECT_EQ(viewer_->get_video_stream_count(), 2);
	EXPECT_EQ(viewer_->get_video_params(1), vp2);
	EXPECT_EQ(viewer_->get_total_stream_count(), 3);

	// References enumerate the enabled streams in video/audio/subtitle order
	const QVector<olive::Track::Reference> refs =
		viewer_->get_enabled_streams_as_references();
	ASSERT_EQ(refs.size(), 3);
	EXPECT_EQ(refs.at(0), olive::Track::Reference(olive::Track::k_video, 0));
	EXPECT_EQ(refs.at(1), olive::Track::Reference(olive::Track::k_video, 1));
	EXPECT_EQ(refs.at(2), olive::Track::Reference(olive::Track::k_audio, 0));

	// SetStream replaces an existing element in place
	const olive::core::AudioParams ap2(
		32000, olive::core::k_channel_layout_mono, olive::core::SampleFormat::s16);
	EXPECT_EQ(viewer_->set_stream(olive::Track::k_audio,
								 QVariant::fromValue(ap2), 0),
			  0);
	EXPECT_EQ(viewer_->get_audio_stream_count(), 1);
	EXPECT_EQ(viewer_->get_audio_params(0), ap2);

	// kNone is not a valid stream type
	EXPECT_EQ(viewer_->add_stream(olive::Track::k_none, QVariant()), -1);
}

TEST_F(ViewerOutputTest, ConnectTextureEmitsAndResolves)
{
	auto *solid = add_node<olive::SolidGenerator>();

	int emissions = 0;
	QObject::connect(viewer_, &olive::ViewerOutput::texture_input_changed,
					 [&emissions]() { ++emissions; });

	olive::Node::connect_edge(
		solid, olive::NodeInput(viewer_, olive::ViewerOutput::k_texture_input));

	EXPECT_EQ(emissions, 1);
	EXPECT_EQ(viewer_->get_connected_texture_output(), solid);

	// Explicit value hints set on the input are reported through the getter
	const olive::Node::ValueHint hint({ olive::NodeValue::k_texture }, 2,
									  QStringLiteral("tex"));
	viewer_->set_value_hint_for_input(olive::ViewerOutput::k_texture_input, hint);
	const olive::Node::ValueHint got =
		viewer_->get_connected_texture_value_hint();
	ASSERT_EQ(got.types().size(), 1);
	EXPECT_EQ(got.types().first(), olive::NodeValue::k_texture);
	EXPECT_EQ(got.index(), 2);
	EXPECT_EQ(got.tag(), QStringLiteral("tex"));

	olive::Node::disconnect_edge(
		solid, olive::NodeInput(viewer_, olive::ViewerOutput::k_texture_input));

	EXPECT_EQ(emissions, 2);
	EXPECT_EQ(viewer_->get_connected_texture_output(), nullptr);
}

TEST_F(ViewerOutputTest, ConnectSamplesResolves)
{
	auto *clip = add_node<olive::ClipBlock>();

	olive::Node::connect_edge(
		clip, olive::NodeInput(viewer_, olive::ViewerOutput::k_samples_input));

	EXPECT_EQ(viewer_->get_connected_sample_output(), clip);
	EXPECT_EQ(viewer_->get_connected_waveform(), clip->waveform_cache());

	// Without an explicit hint the sample value hint is empty
	EXPECT_TRUE(viewer_->get_connected_sample_value_hint().types().isEmpty());

	olive::Node::disconnect_edge(
		clip, olive::NodeInput(viewer_, olive::ViewerOutput::k_samples_input));

	EXPECT_EQ(viewer_->get_connected_sample_output(), nullptr);
	EXPECT_EQ(viewer_->get_connected_waveform(), nullptr);
}

TEST_F(ViewerOutputTest, InvalidateCacheWithoutConnectionsIsSafe)
{
	// With nothing connected the request path is skipped entirely; this must
	// neither crash nor produce a length change
	viewer_->invalidate_cache(olive::TimeRange(olive::Rational(0),
											  olive::Rational(1)),
							 olive::ViewerOutput::k_texture_input, -1,
							 olive::Node::InvalidateCacheOptions());
	viewer_->invalidate_cache(olive::TimeRange(olive::Rational(0),
											  olive::Rational(1)),
							 olive::ViewerOutput::k_samples_input, -1,
							 olive::Node::InvalidateCacheOptions());

	EXPECT_EQ(viewer_->get_length(), olive::Rational(0));
}

TEST_F(ViewerOutputTest, ValueRepushTagsStreams)
{
	olive::NodeValueRow row;
	row.insert(olive::ViewerOutput::k_texture_input,
			   olive::NodeValue(olive::NodeValue::k_texture, 0));
	row.insert(olive::ViewerOutput::k_samples_input,
			   olive::NodeValue(olive::NodeValue::k_samples, 0));

	olive::NodeValueTable table;
	viewer_->value(row, olive::NodeGlobals(), &table);

	// The texture value is re-pushed tagged as video stream 0
	const QString video_tag =
		olive::Track::Reference(olive::Track::k_video, 0).to_string();
	EXPECT_EQ(table.get(olive::NodeValue::k_texture, video_tag).type(),
			  olive::NodeValue::k_texture);

	// The samples value is re-pushed tagged as audio stream 0
	const QString audio_tag =
		olive::Track::Reference(olive::Track::k_audio, 0).to_string();
	EXPECT_EQ(table.get(olive::NodeValue::k_samples, audio_tag).type(),
			  olive::NodeValue::k_samples);
	EXPECT_EQ(table.get(olive::NodeValue::k_samples).tag(), audio_tag);
}

TEST_F(ViewerOutputTest, LastUsedEncodingParamsRoundTrip)
{
	olive::EncodingParams params;
	params.set_filename(QStringLiteral("/tmp/oak-export.mp4"));

	viewer_->set_last_used_encoding_params(params);

	EXPECT_EQ(viewer_->get_last_used_encoding_params().filename(),
			  QStringLiteral("/tmp/oak-export.mp4"));
}

TEST_F(ViewerOutputTest, SaveLoadCustomRoundTrip)
{
	viewer_->get_work_area()->set_enabled(true);
	viewer_->get_work_area()->set_range(
		olive::TimeRange(olive::Rational(1), olive::Rational(5)));

	QString xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartDocument();
	writer.writeStartElement(QStringLiteral("custom"));
	viewer_->save_custom(&writer);
	writer.writeEndElement();
	writer.writeEndDocument();

	EXPECT_TRUE(xml.contains(QStringLiteral("workarea")));
	EXPECT_TRUE(xml.contains(QStringLiteral("markers")));

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement());
	ASSERT_EQ(reader.name(), QStringLiteral("custom"));

	olive::ViewerOutput loaded;
	ASSERT_TRUE(loaded.load_custom(&reader, nullptr));
	EXPECT_TRUE(loaded.get_work_area()->enabled());
	EXPECT_EQ(loaded.get_work_area()->range(), viewer_->get_work_area()->range());
}

TEST_F(ViewerOutputTest, RetranslateSetsInputNames)
{
	viewer_->retranslate();

	EXPECT_EQ(viewer_->get_input_name(olive::ViewerOutput::k_video_params_input),
			  QStringLiteral("Video Parameters"));
	EXPECT_EQ(viewer_->get_input_name(olive::ViewerOutput::k_audio_params_input),
			  QStringLiteral("Audio Parameters"));
	EXPECT_EQ(viewer_->get_input_name(olive::ViewerOutput::k_subtitle_params_input),
			  QStringLiteral("Subtitle Parameters"));
	EXPECT_EQ(viewer_->get_input_name(olive::ViewerOutput::k_texture_input),
			  QStringLiteral("Texture"));
	EXPECT_EQ(viewer_->get_input_name(olive::ViewerOutput::k_samples_input),
			  QStringLiteral("Samples"));
}

TEST_F(ViewerOutputTest, AutoCacheStubsAlwaysReportDisabled)
{
	EXPECT_FALSE(viewer_->is_video_auto_cache_enabled());

	// The setter is a stub and must not change the reported state
	viewer_->set_video_auto_cache_enabled(true);
	EXPECT_FALSE(viewer_->is_video_auto_cache_enabled());
}

TEST_F(ViewerOutputTest, SetWaveformEnabledWithoutConnectionIsSafe)
{
	// No samples input connected: enabling waveform requests must not crash
	viewer_->set_waveform_enabled(true);
	EXPECT_EQ(viewer_->get_connected_waveform(), nullptr);

	viewer_->set_waveform_enabled(false);
}

TEST_F(ViewerOutputTest, FrequencyRateDataReflectsEnabledStreams)
{
	// A video stream takes priority and is reported as a frame rate
	QString rate = viewer_->data(olive::Node::frequency_rate).toString();
	EXPECT_TRUE(rate.endsWith(QStringLiteral(" FPS")));

	// With the video stream disabled, the audio sample rate is reported
	olive::VideoParams vp = viewer_->get_video_params();
	vp.set_enabled(false);
	viewer_->set_video_params(vp);

	rate = viewer_->data(olive::Node::frequency_rate).toString();
	EXPECT_TRUE(rate.endsWith(QStringLiteral(" Hz")));
}
