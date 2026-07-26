/*
 * Oak Video Editor - Render Processor & Manager CPU Tests
 * Copyright (C) 2026 Oak Team
 *
 * CPU-only, headless coverage for render pipeline pieces that need no GL
 * context, worker process, or audio device:
 * - RenderProcessor  (ticket plumbing: audio render + clamp + waveform,
 *                     video dry-run with a null renderer, cancelled tickets)
 * - RenderManager    (RenderVideoParams/RenderAudioParams default plumbing,
 *                     kDryRunInterval)
 * - RenderJobTracker (job-time tagged range bookkeeping)
 * - SubtitleParams   (ASS header generation, XML save/load round trip)
 * - ManagedColor     (color input/output transform plumbing)
 * - Texture          (dummy textures constructible without a renderer)
 *
 * SpscRingBuffer is intentionally not covered here; render_ipc_test.cpp and
 * render_workerpool_ipc_test.cpp already exercise it thoroughly. The backend
 * string conversions are covered by config_test.cpp.
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include <QMatrix4x4>
#include <QSize>
#include <QString>
#include <QVariant>
#include <QVector2D>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <olive/core/core.h>

#include "audio/audiovisualwaveform.h"
#include "common/jobtime.h"
#include "common/qtutils.h"
#include "node/color/colormanager/colormanager.h"
#include "node/generator/solid/solid.h"
#include "node/globals.h"
#include "node/project.h"
#include "render/job/acceleratedjob.h"
#include "widget/manageddisplay/colorprocessorhandle.h"
#include "render/renderjobtracker.h"
#include "render/rendermanager.h"
#include "render/renderprocessor.h"
#include "render/subtitleparams.h"
#include "render/texture.h"

namespace
{

// A node that emits a constant, deliberately over-range (+2.0) sample buffer
// so RenderProcessor's clamp path has observable work to do.
class ConstantSampleNode : public olive::Node {
public:
	ConstantSampleNode() = default;

	NODE_DEFAULT_FUNCTIONS(ConstantSampleNode)

	virtual QString name() const override
	{
		return QStringLiteral("Constant Sample Node");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.constant_sample_node");
	}

	virtual QVector<CategoryID> category() const override
	{
		return {};
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)

		const olive::core::AudioParams &params = globals.aparams();
		const size_t sample_count =
			size_t(params.time_to_samples(globals.time().length()));

		olive::core::SampleBuffer buffer(params, sample_count);
		for (int ch = 0; ch < buffer.channel_count(); ch++) {
			float *data = buffer.data(ch);
			for (size_t i = 0; i < sample_count; i++) {
				data[i] = 2.0f;
			}
		}

		table->push(olive::NodeValue::k_samples, QVariant::fromValue(buffer),
					this);
	}
};

olive::RenderTicketPtr make_video_ticket(olive::Node *node)
{
	olive::RenderTicketPtr ticket = std::make_shared<olive::RenderTicket>();
	ticket->setProperty("node", olive::QtUtils::ptr_to_value(node));
	ticket->setProperty("time", QVariant::fromValue(olive::Rational(0)));
	ticket->setProperty(
		"type", QVariant::fromValue(olive::RenderManager::k_type_video));
	ticket->setProperty(
		"vparam",
		QVariant::fromValue(olive::VideoParams(64, 64, olive::Rational(1, 30),
											   olive::core::PixelFormat::u8,
											   4)));
	ticket->setProperty("aparam",
						QVariant::fromValue(olive::core::AudioParams()));
	ticket->setProperty("mode", int(olive::RenderMode::k_online));
	return ticket;
}

olive::RenderTicketPtr make_audio_ticket(olive::Node *node, bool waveforms,
									   bool clamp)
{
	olive::RenderTicketPtr ticket = std::make_shared<olive::RenderTicket>();
	ticket->setProperty("node", olive::QtUtils::ptr_to_value(node));
	ticket->setProperty(
		"time",
		QVariant::fromValue(olive::TimeRange(olive::Rational(0),
											 olive::Rational(1))));
	ticket->setProperty(
		"type", QVariant::fromValue(olive::RenderManager::k_type_audio));
	ticket->setProperty("enablewaveforms", waveforms);
	ticket->setProperty("clamp", clamp);
	ticket->setProperty(
		"aparam",
		QVariant::fromValue(olive::core::AudioParams(
			48000, olive::core::k_channel_layout_stereo,
			olive::core::SampleFormat::f32_p)));
	ticket->setProperty(
		"vparam",
		QVariant::fromValue(olive::VideoParams(64, 64, olive::Rational(1, 30),
											   olive::core::PixelFormat::u8,
											   4)));
	ticket->setProperty("mode", int(olive::RenderMode::k_online));
	return ticket;
}

} // namespace

// ============================================================================
// RenderProcessor (null renderer = dry run path)
// ============================================================================

TEST(RenderProcessor, AudioTicketRendersAndClampsSamples)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = new ConstantSampleNode();
	node->setParent(&project);

	olive::RenderTicketPtr ticket = make_audio_ticket(node, false, true);
	ticket->start();

	olive::RenderProcessor::process(ticket, nullptr, nullptr, nullptr);

	ASSERT_TRUE(ticket->has_result());
	olive::core::SampleBuffer samples =
		ticket->get().value<olive::core::SampleBuffer>();
	ASSERT_TRUE(samples.is_allocated());
	EXPECT_EQ(samples.channel_count(), 2);
	EXPECT_EQ(samples.sample_count(), size_t(48000));

	// The node emitted +2.0 everywhere; the ticket requested clamping.
	for (int ch = 0; ch < samples.channel_count(); ch++) {
		const float *data = samples.data(ch);
		for (size_t i = 0; i < samples.sample_count(); i++) {
			EXPECT_FLOAT_EQ(data[i], 1.0f);
		}
	}
}

TEST(RenderProcessor, AudioTicketWithoutClampKeepsSamples)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = new ConstantSampleNode();
	node->setParent(&project);

	olive::RenderTicketPtr ticket = make_audio_ticket(node, false, false);
	ticket->start();

	olive::RenderProcessor::process(ticket, nullptr, nullptr, nullptr);

	ASSERT_TRUE(ticket->has_result());
	olive::core::SampleBuffer samples =
		ticket->get().value<olive::core::SampleBuffer>();
	ASSERT_TRUE(samples.is_allocated());
	ASSERT_GT(samples.sample_count(), size_t(0));

	const float *data = samples.data(0);
	for (size_t i = 0; i < samples.sample_count(); i++) {
		EXPECT_FLOAT_EQ(data[i], 2.0f);
	}
}

TEST(RenderProcessor, AudioTicketGeneratesWaveformWhenRequested)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = new ConstantSampleNode();
	node->setParent(&project);

	olive::RenderTicketPtr ticket = make_audio_ticket(node, true, true);
	ticket->start();

	olive::RenderProcessor::process(ticket, nullptr, nullptr, nullptr);

	ASSERT_TRUE(ticket->has_result());

	const QVariant waveform_var = ticket->property("waveform");
	ASSERT_TRUE(waveform_var.isValid());
	const olive::AudioVisualWaveform waveform =
		waveform_var.value<olive::AudioVisualWaveform>();
	EXPECT_EQ(waveform.channel_count(), 2);
}

TEST(RenderProcessor, AudioTicketWithoutNodeReturnsEmptyBuffer)
{
	olive::RenderTicketPtr ticket = make_audio_ticket(nullptr, true, true);
	ticket->start();

	olive::RenderProcessor::process(ticket, nullptr, nullptr, nullptr);

	// With no node to traverse the processor still finishes with a (null)
	// SampleBuffer, and skips both clamping and waveform generation.
	ASSERT_TRUE(ticket->has_result());
	const olive::core::SampleBuffer samples =
		ticket->get().value<olive::core::SampleBuffer>();
	EXPECT_FALSE(samples.is_allocated());
	EXPECT_FALSE(ticket->property("waveform").isValid());
}

TEST(RenderProcessor, VideoTicketWithoutRendererFinishesWithoutResult)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);

	olive::RenderTicketPtr ticket = make_video_ticket(solid);
	ticket->start();

	// A null render context is the "dry run": the graph is traversed (Solid
	// emits a shader job which is skipped) and the ticket finishes empty.
	olive::RenderProcessor::process(ticket, nullptr, nullptr, nullptr);

	EXPECT_FALSE(ticket->is_running());
	EXPECT_EQ(ticket->get_finish_count(), 1);
	EXPECT_FALSE(ticket->has_result());
}

TEST(RenderProcessor, VideoTicketWithoutNodeFinishesWithoutResult)
{
	olive::RenderTicketPtr ticket = make_video_ticket(nullptr);
	ticket->start();

	olive::RenderProcessor::process(ticket, nullptr, nullptr, nullptr);

	EXPECT_FALSE(ticket->is_running());
	EXPECT_EQ(ticket->get_finish_count(), 1);
	EXPECT_FALSE(ticket->has_result());
}

TEST(RenderProcessor, CancelledTicketFinishesImmediately)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *solid = new olive::SolidGenerator();
	solid->setParent(&project);

	olive::RenderTicketPtr ticket = make_video_ticket(solid);
	ticket->start();
	ticket->cancel();

	olive::RenderProcessor::process(ticket, nullptr, nullptr, nullptr);

	EXPECT_EQ(ticket->get_finish_count(), 1);
	EXPECT_FALSE(ticket->has_result());
}

// ============================================================================
// RenderManager parameter plumbing (no instance required)
// ============================================================================

TEST(RenderManagerParams, RenderVideoParamsDefaults)
{
	const olive::VideoParams vparams(1920, 1080, olive::core::PixelFormat::u8,
									 4);
	const olive::core::AudioParams aparams;

	olive::RenderManager::RenderVideoParams params(nullptr, vparams, aparams,
												   olive::Rational(5), nullptr,
												   olive::RenderMode::k_online);

	EXPECT_EQ(params.node, nullptr);
	EXPECT_EQ(params.video_params, vparams);
	EXPECT_EQ(params.audio_params, aparams);
	EXPECT_EQ(params.time, olive::Rational(5));
	EXPECT_EQ(params.color_manager, nullptr);
	EXPECT_EQ(params.mode, olive::RenderMode::k_online);

	EXPECT_FALSE(params.use_cache);
	EXPECT_EQ(params.return_type, olive::RenderManager::k_frame);
	EXPECT_EQ(params.multicam, nullptr);

	EXPECT_TRUE(params.cache_dir.isEmpty());
	EXPECT_TRUE(params.cache_id.isEmpty());

	EXPECT_EQ(params.force_size, QSize(0, 0));
	EXPECT_EQ(params.force_channel_count, 0);
	EXPECT_TRUE(params.force_matrix.isIdentity());
	EXPECT_EQ(int(params.force_format),
			  int(olive::core::PixelFormat::invalid));
	EXPECT_TRUE(params.force_color_output == nullptr);
	EXPECT_FALSE(params.force_color_transform.is_display());
	EXPECT_TRUE(params.force_color_transform.output().isEmpty());
}

TEST(RenderManagerParams, RenderAudioParamsDefaults)
{
	const olive::core::AudioParams aparams(
		48000, olive::core::k_channel_layout_stereo,
		olive::core::SampleFormat::f32_p);
	const olive::TimeRange range(olive::Rational(2), olive::Rational(7));

	olive::RenderManager::RenderAudioParams params(nullptr, range, aparams,
												   olive::RenderMode::k_offline);

	EXPECT_EQ(params.node, nullptr);
	EXPECT_EQ(params.range, range);
	EXPECT_EQ(params.audio_params, aparams);
	EXPECT_FALSE(params.generate_waveforms);
	EXPECT_TRUE(params.clamp);
	EXPECT_EQ(params.mode, olive::RenderMode::k_offline);
}

TEST(RenderManagerParams, DryRunIntervalIsTenSeconds)
{
	EXPECT_EQ(olive::Rational(olive::RenderManager::k_dry_run_interval),
			  olive::Rational(10));
}

// ============================================================================
// RenderJobTracker
// ============================================================================

TEST(RenderJobTracker, EmptyTrackerIsNeverCurrent)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime job;

	EXPECT_FALSE(tracker.isCurrent(olive::Rational(0), job));
	EXPECT_TRUE(
		tracker.getCurrentSubRanges(olive::TimeRange(0, 10), job).isEmpty());
}

TEST(RenderJobTracker, InsertedRangeIsCurrentForSameAndNewerJobs)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime older;
	const olive::JobTime newer;

	tracker.insert(olive::TimeRange(0, 10), older);

	// A range rendered at job time T satisfies queries at T and later.
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(5), older));
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(5), newer));
}

TEST(RenderJobTracker, IsCurrentRespectsRangeBoundaries)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime job;

	tracker.insert(olive::TimeRange(0, 10), job);

	EXPECT_TRUE(tracker.isCurrent(olive::Rational(0), job));
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(-1), job));
	// The out point is exclusive.
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(10), job));
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(11), job));
}

TEST(RenderJobTracker, ReinsertingSameRangeBumpsJobTime)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime older;
	const olive::JobTime newer;

	tracker.insert(olive::TimeRange(0, 10), older);
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(5), older));

	tracker.insert(olive::TimeRange(0, 10), newer);

	// The older job no longer describes the cached content.
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(5), older));
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(5), newer));
}

TEST(RenderJobTracker, InsertSplitsExistingRange)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime older;
	const olive::JobTime newer;

	tracker.insert(olive::TimeRange(0, 10), older);
	tracker.insert(olive::TimeRange(4, 6), newer);

	// The original range is split around the new one, keeping its job time.
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(2), older));
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(8), older));
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(5), older));
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(5), newer));
}

TEST(RenderJobTracker, InsertTrimsOverlappingRangeEnds)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime older;
	const olive::JobTime newer;

	tracker.insert(olive::TimeRange(0, 10), older);
	tracker.insert(olive::TimeRange(5, 15), newer);

	EXPECT_TRUE(tracker.isCurrent(olive::Rational(2), older));
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(7), older));
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(7), newer));
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(12), newer));
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(16), newer));
}

TEST(RenderJobTracker, InsertRangeListTagsAllRanges)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime job;

	olive::TimeRangeList ranges;
	ranges.insert(olive::TimeRange(0, 5));
	ranges.insert(olive::TimeRange(10, 15));
	tracker.insert(ranges, job);

	EXPECT_TRUE(tracker.isCurrent(olive::Rational(2), job));
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(12), job));
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(7), job));
}

TEST(RenderJobTracker, GetCurrentSubRangesClipsToQueryRange)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime job;

	tracker.insert(olive::TimeRange(0, 10), job);

	const olive::TimeRangeList sub =
		tracker.getCurrentSubRanges(olive::TimeRange(4, 20), job);
	ASSERT_EQ(sub.size(), 1);
	EXPECT_EQ(*sub.begin(), olive::TimeRange(4, 10));
}

TEST(RenderJobTracker, GetCurrentSubRangesIgnoresNewerJobs)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime older;
	const olive::JobTime newer;

	tracker.insert(olive::TimeRange(0, 10), newer);

	// Querying with an older job time sees nothing current.
	EXPECT_TRUE(
		tracker.getCurrentSubRanges(olive::TimeRange(0, 10), older).isEmpty());
}

TEST(RenderJobTracker, GetCurrentSubRangesMergesAdjacentJobs)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime older;
	const olive::JobTime newer;

	tracker.insert(olive::TimeRange(0, 5), older);
	tracker.insert(olive::TimeRange(5, 10), newer);

	// Both jobs are current for `newer`; touching ranges merge into one.
	const olive::TimeRangeList sub =
		tracker.getCurrentSubRanges(olive::TimeRange(0, 10), newer);
	ASSERT_EQ(sub.size(), 1);
	EXPECT_EQ(*sub.begin(), olive::TimeRange(0, 10));
}

TEST(RenderJobTracker, ClearDropsAllJobs)
{
	olive::RenderJobTracker tracker;
	const olive::JobTime job;

	tracker.insert(olive::TimeRange(0, 10), job);
	EXPECT_TRUE(tracker.isCurrent(olive::Rational(5), job));

	tracker.clear();
	EXPECT_FALSE(tracker.isCurrent(olive::Rational(5), job));
}

// ============================================================================
// SubtitleParams
// ============================================================================

TEST(SubtitleParams, DefaultsAreEmptyEnabledStreamZero)
{
	const olive::SubtitleParams params;

	EXPECT_FALSE(params.is_valid());
	EXPECT_EQ(params.duration(), olive::Rational(0));
	EXPECT_EQ(params.stream_index(), 0);
	EXPECT_TRUE(params.enabled());
}

TEST(SubtitleParams, DurationFollowsLastSubtitle)
{
	olive::SubtitleParams params;
	params.push_back(
		olive::Subtitle(olive::TimeRange(0, 2), QStringLiteral("one")));
	params.push_back(
		olive::Subtitle(olive::TimeRange(3, 5), QStringLiteral("two")));

	EXPECT_TRUE(params.is_valid());
	EXPECT_EQ(params.duration(), olive::Rational(5));

	params.set_stream_index(3);
	params.set_enabled(false);
	EXPECT_EQ(params.stream_index(), 3);
	EXPECT_FALSE(params.enabled());
}

TEST(SubtitleParams, SubtitleAccessorsRoundTrip)
{
	olive::Subtitle sub(olive::TimeRange(1, 4), QStringLiteral("hello"));
	EXPECT_EQ(sub.time(), olive::TimeRange(1, 4));
	EXPECT_EQ(sub.text(), QStringLiteral("hello"));

	sub.set_time(olive::TimeRange(2, 6));
	sub.set_text(QStringLiteral("world"));
	EXPECT_EQ(sub.time(), olive::TimeRange(2, 6));
	EXPECT_EQ(sub.text(), QStringLiteral("world"));

	const olive::Subtitle def;
	EXPECT_TRUE(def.text().isEmpty());
}

TEST(SubtitleParams, GenerateAssHeaderContainsRequiredSections)
{
	const QString header = olive::SubtitleParams::generate_ass_header();

	EXPECT_TRUE(header.contains(QStringLiteral("[Script Info]\r\n")));
	EXPECT_TRUE(header.contains(QStringLiteral("ScriptType: v4.00+\r\n")));
	EXPECT_TRUE(header.contains(QStringLiteral("PlayResX: 384\r\n")));
	EXPECT_TRUE(header.contains(QStringLiteral("PlayResY: 288\r\n")));
	EXPECT_TRUE(
		header.contains(QStringLiteral("ScaledBorderAndShadow: yes\r\n")));
	EXPECT_TRUE(header.contains(QStringLiteral("[V4+ Styles]\r\n")));
	EXPECT_TRUE(header.contains(QStringLiteral("Style: Default,Arial,16,")));
	EXPECT_TRUE(
		header.contains(QStringLiteral("&Hffffff,&Hffffff,&H0,&H0,")));
	EXPECT_TRUE(header.contains(QStringLiteral("[Events]\r\n")));
	EXPECT_TRUE(header.contains(
		QStringLiteral("Format: Layer, Start, End, Style, Name, MarginL, "
					   "MarginR, MarginV, Effect, Text")));
	EXPECT_TRUE(header.endsWith(QStringLiteral("\r\n")));
}

TEST(SubtitleParams, SaveLoadRoundTrip)
{
	olive::SubtitleParams params;
	params.set_stream_index(2);
	params.set_enabled(false);
	params.push_back(olive::Subtitle(
		olive::TimeRange(olive::Rational(0), olive::Rational(1, 2)),
		QStringLiteral("Hello, world!")));
	params.push_back(olive::Subtitle(
		olive::TimeRange(olive::Rational(3, 4), olive::Rational(2)),
		QStringLiteral("Second <line> & more")));

	QString xml;
	{
		QXmlStreamWriter writer(&xml);
		writer.writeStartElement(QStringLiteral("root"));
		params.save(&writer);
		writer.writeEndElement();
	}

	olive::SubtitleParams loaded;
	// Pre-existing content must be cleared by Load.
	loaded.push_back(
		olive::Subtitle(olive::TimeRange(9, 10), QStringLiteral("junk")));

	QXmlStreamReader reader(xml);
	ASSERT_TRUE(reader.readNextStartElement()); // position on <root>
	loaded.load(&reader);

	EXPECT_EQ(loaded.stream_index(), 2);
	EXPECT_FALSE(loaded.enabled());
	ASSERT_EQ(loaded.size(), size_t(2));
	EXPECT_EQ(loaded.at(0).time().in(), olive::Rational(0));
	EXPECT_EQ(loaded.at(0).time().out(), olive::Rational(1, 2));
	EXPECT_EQ(loaded.at(0).text(), QStringLiteral("Hello, world!"));
	EXPECT_EQ(loaded.at(1).time().in(), olive::Rational(3, 4));
	EXPECT_EQ(loaded.at(1).time().out(), olive::Rational(2));
	EXPECT_EQ(loaded.at(1).text(), QStringLiteral("Second <line> & more"));
}

// ============================================================================
// ManagedColor
// ============================================================================

TEST(ManagedColor, DefaultConstructionHasNoTransforms)
{
	const olive::ManagedColor color;

	EXPECT_FLOAT_EQ(color.red(), 0.0f);
	EXPECT_FLOAT_EQ(color.green(), 0.0f);
	EXPECT_FLOAT_EQ(color.blue(), 0.0f);
	EXPECT_FLOAT_EQ(color.alpha(), 0.0f);
	EXPECT_TRUE(color.color_input().isEmpty());
	EXPECT_FALSE(color.color_output().is_display());
	EXPECT_TRUE(color.color_output().output().isEmpty());
}

TEST(ManagedColor, RgbaConstructorPreservesChannels)
{
	const olive::ManagedColor color(0.25, 0.5, 0.75, 0.5);

	EXPECT_FLOAT_EQ(color.red(), 0.25f);
	EXPECT_FLOAT_EQ(color.green(), 0.5f);
	EXPECT_FLOAT_EQ(color.blue(), 0.75f);
	EXPECT_FLOAT_EQ(color.alpha(), 0.5f);
}

TEST(ManagedColor, ColorCopyConstructorPreservesChannels)
{
	const olive::core::Color base(0.1f, 0.2f, 0.3f, 1.0f);
	const olive::ManagedColor color(base);

	EXPECT_FLOAT_EQ(color.red(), base.red());
	EXPECT_FLOAT_EQ(color.green(), base.green());
	EXPECT_FLOAT_EQ(color.blue(), base.blue());
	EXPECT_FLOAT_EQ(color.alpha(), base.alpha());
}

TEST(ManagedColor, RawDataConstructorDecodesU8)
{
	const char data[4] = { char(255), char(128), char(0), char(64) };
	const olive::ManagedColor color(data, olive::core::PixelFormat::u8, 4);

	EXPECT_FLOAT_EQ(color.red(), 1.0f);
	EXPECT_NEAR(color.green(), 128.0 / 255.0, 1e-6);
	EXPECT_FLOAT_EQ(color.blue(), 0.0f);
	EXPECT_NEAR(color.alpha(), 64.0 / 255.0, 1e-6);
}

TEST(ManagedColor, ColorInputAndOutputRoundTrip)
{
	olive::ManagedColor color;

	color.set_color_input(QStringLiteral("linear"));
	EXPECT_EQ(color.color_input(), QStringLiteral("linear"));

	color.set_color_output(olive::ColorTransform(QStringLiteral("sRGB")));
	EXPECT_FALSE(color.color_output().is_display());
	EXPECT_EQ(color.color_output().output(), QStringLiteral("sRGB"));

	color.set_color_output(olive::ColorTransform(QStringLiteral("sRGB"),
												 QStringLiteral("Filmic"),
												 QStringLiteral("None")));
	EXPECT_TRUE(color.color_output().is_display());
	EXPECT_EQ(color.color_output().display(), QStringLiteral("sRGB"));
	EXPECT_EQ(color.color_output().view(), QStringLiteral("Filmic"));
	EXPECT_EQ(color.color_output().look(), QStringLiteral("None"));
}

// ============================================================================
// Texture (dummy, no renderer)
// ============================================================================

TEST(RenderTexture, DummyTextureExposesParams)
{
	const olive::VideoParams params(320, 240, olive::core::PixelFormat::u8, 4);
	olive::Texture texture(params);

	EXPECT_TRUE(texture.is_dummy());
	EXPECT_EQ(texture.renderer(), nullptr);
	EXPECT_EQ(texture.params(), params);
	EXPECT_EQ(texture.width(), 320);
	EXPECT_EQ(texture.height(), 240);
	EXPECT_EQ(texture.channel_count(), 4);
	EXPECT_EQ(texture.divider(), 1);
	EXPECT_EQ(texture.pixel_aspect_ratio(), olive::Rational(1));
	EXPECT_EQ(texture.virtual_resolution(), QVector2D(320, 240));
	EXPECT_EQ(int(texture.format()), int(olive::core::PixelFormat::u8));
	EXPECT_FALSE(texture.id().isValid());
	EXPECT_FALSE(texture.is_job());
	EXPECT_EQ(texture.job(), nullptr);
	EXPECT_TRUE(texture.frame() == nullptr);
}

TEST(RenderTexture, DummyTextureHonorsDivider)
{
	const olive::VideoParams params(320, 240, olive::core::PixelFormat::u8, 4,
									olive::Rational(1),
									olive::VideoParams::k_interlace_none, 2);
	const olive::Texture texture(params);

	EXPECT_EQ(texture.divider(), 2);
	EXPECT_EQ(texture.width(), 160);
	EXPECT_EQ(texture.height(), 120);
}

TEST(RenderTexture, JobTextureCarriesJobAndParams)
{
	const olive::VideoParams params(64, 64, olive::core::PixelFormat::f32, 4);

	olive::AcceleratedJob job;
	job.insert(QStringLiteral("value_in"),
			   olive::NodeValue(olive::NodeValue::k_float, 2.5));

	const olive::TexturePtr texture = olive::Texture::job(params, job);
	ASSERT_TRUE(texture != nullptr);
	EXPECT_TRUE(texture->is_dummy());
	EXPECT_TRUE(texture->is_job());
	ASSERT_TRUE(texture->job() != nullptr);
	EXPECT_TRUE(
		texture->job()->get_values().contains(QStringLiteral("value_in")));
	EXPECT_EQ(texture->job()->get(QStringLiteral("value_in")).to_double(), 2.5);
	EXPECT_EQ(texture->params(), params);
}

TEST(RenderTexture, ToJobCreatesJobTextureWithSameParams)
{
	const olive::VideoParams params(128, 72, olive::core::PixelFormat::u8, 4);
	olive::Texture dummy(params);

	const olive::AcceleratedJob job;
	const olive::TexturePtr job_tex = dummy.to_job(job);

	ASSERT_TRUE(job_tex != nullptr);
	EXPECT_FALSE(dummy.is_job());
	EXPECT_TRUE(job_tex->is_job());
	EXPECT_EQ(job_tex->params(), dummy.params());
}

TEST(RenderTexture, UploadDownloadOnDummyAreNoOps)
{
	const olive::VideoParams params(16, 16, olive::core::PixelFormat::u8, 4);
	olive::Texture texture(params);

	// With no renderer backend both calls must return without touching data.
	uint8_t buffer[16 * 16 * 4];
	memset(buffer, 0xAB, sizeof(buffer));
	texture.upload(buffer, 16 * 4);
	texture.download(buffer, 16 * 4);
	EXPECT_EQ(buffer[0], uint8_t(0xAB));
}

TEST(RenderTexture, DefaultInterpolationIsMipmappedLinear)
{
	EXPECT_EQ(int(olive::Texture::k_default_interpolation),
			  int(olive::Texture::k_mipmapped_linear));
}
