#include <gtest/gtest.h>

#include <climits>
#include <limits>

#include <QTransform>
#include <QVector>

#include "node/block/clip/clip.h"
#include "node/generator/solid/solid.h"
#include "node/globals.h"
#include "node/input/time/timeinput.h"
#include "node/output/track/track.h"
#include "node/output/viewer/viewer.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/traverser.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"
#include "render/framehashcache.h"
#include "render/loopmode.h"
#include "render/playbackcache.h"

namespace
{

// Records the loop mode seen in NodeGlobals so the traverser's clip loop
// mode pick-up can be observed.
class LoopModeProbeNode : public olive::Node {
public:
	LoopModeProbeNode() = default;

	NODE_DEFAULT_FUNCTIONS(LoopModeProbeNode)

	virtual QString name() const override
	{
		return QStringLiteral("Loop Mode Probe");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.loopmodeprobe");
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

		last_loop_mode_ = globals.loop_mode();
		table->push(olive::NodeValue::k_float, 0.0, this);
	}

	olive::LoopMode last_loop_mode() const
	{
		return last_loop_mode_;
	}

private:
	mutable olive::LoopMode last_loop_mode_ = olive::LoopMode::k_loop_mode_off;
};

// Node with two connectable inputs and a configurable gizmo transformation,
// used to verify which nodes the traverser accumulates transforms from.
class GizmoProbeNode : public olive::Node {
public:
	GizmoProbeNode()
	{
		add_input(QStringLiteral("a_in"), olive::NodeValue::k_float, 0.0);
		add_input(QStringLiteral("b_in"), olive::NodeValue::k_float, 0.0);
	}

	NODE_DEFAULT_FUNCTIONS(GizmoProbeNode)

	virtual QString name() const override
	{
		return QStringLiteral("Gizmo Probe");
	}

	virtual QString id() const override
	{
		return QStringLiteral("org.oak.test.gizmoprobe");
	}

	virtual QVector<CategoryID> category() const override
	{
		return {};
	}

	void set_gizmo_transform(const QTransform &t)
	{
		t_ = t;
	}

	virtual QTransform
	gizmo_transformation(const olive::NodeValueRow &row,
						const olive::NodeGlobals &globals) const override
	{
		Q_UNUSED(row)
		Q_UNUSED(globals)

		return t_;
	}

	virtual void value(const olive::NodeValueRow &value,
					   const olive::NodeGlobals &globals,
					   olive::NodeValueTable *table) const override
	{
		Q_UNUSED(value)
		Q_UNUSED(globals)

		table->push(olive::NodeValue::k_float, 0.0, this);
	}

	static QString input_a()
	{
		return QStringLiteral("a_in");
	}

	static QString input_b()
	{
		return QStringLiteral("b_in");
	}

private:
	QTransform t_;
};

olive::ClipBlock *create_clip(olive::Project *project,
							 const olive::core::Rational &length)
{
	auto *clip = new olive::ClipBlock();
	clip->setParent(project);
	clip->set_length_and_media_out(length);
	return clip;
}

olive::SolidGenerator *create_solid(olive::Project *project)
{
	auto *solid = new olive::SolidGenerator();
	solid->setParent(project);
	return solid;
}

// Generates the clip's output table at a single time with a fresh traverser
// (the traverser caches tables per node+range, so reusing one would return
// stale values after the clip's parameters change).
olive::NodeValueTable generate_clip_table(const olive::ClipBlock *clip,
										const olive::core::Rational &time)
{
	olive::NodeTraverser traverser;
	return traverser.generate_table(
		clip, olive::TimeRange(time, time + olive::core::Rational(1, 30)));
}

double generate_clip_time_value(const olive::ClipBlock *clip,
							 const olive::core::Rational &time)
{
	olive::NodeValueTable table = generate_clip_table(clip, time);
	olive::NodeValue v = table.get(olive::NodeValue::k_float);
	if (v.type() != olive::NodeValue::k_float) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return v.to_double();
}

} // namespace

TEST(ClipBlock, DefaultState)
{
	olive::ClipBlock clip;

	EXPECT_DOUBLE_EQ(clip.speed(), 1.0);
	EXPECT_FALSE(clip.reverse());
	EXPECT_EQ(clip.loop_mode(), olive::LoopMode::k_loop_mode_off);
	EXPECT_EQ(clip.media_in(), olive::core::Rational(0));
	EXPECT_FALSE(clip.maintain_audio_pitch());
	EXPECT_FALSE(clip.is_autocaching());
	EXPECT_EQ(clip.in_transition(), nullptr);
	EXPECT_EQ(clip.out_transition(), nullptr);
	EXPECT_EQ(clip.connected_viewer(), nullptr);
	EXPECT_EQ(clip.get_track_type(), olive::Track::k_none);
	EXPECT_TRUE(clip.block_links().isEmpty());

	EXPECT_EQ(clip.length(), olive::core::Rational(0));
	EXPECT_EQ(clip.get_video_cache_range(),
			  olive::TimeRange(olive::core::Rational(0), olive::core::Rational(0)));
	EXPECT_EQ(clip.get_audio_cache_range(),
			  olive::TimeRange(olive::core::Rational(0), olive::core::Rational(0)));

	EXPECT_EQ(clip.id(), QStringLiteral("org.olivevideoeditor.Olive.clip"));
	EXPECT_EQ(clip.name(), QStringLiteral("Clip"));
	EXPECT_FALSE(clip.description().isEmpty());
	EXPECT_TRUE(clip.category().contains(olive::Node::k_category_timeline));
}

TEST(ClipBlock, SpeedReverseLoopPitchAutocacheAccessors)
{
	olive::ClipBlock clip;

	clip.set_standard_value(olive::ClipBlock::k_speed_input, 2.5);
	EXPECT_DOUBLE_EQ(clip.speed(), 2.5);

	clip.set_reverse(true);
	EXPECT_TRUE(clip.reverse());
	clip.set_reverse(false);
	EXPECT_FALSE(clip.reverse());

	clip.set_loop_mode(olive::LoopMode::k_loop_mode_loop);
	EXPECT_EQ(clip.loop_mode(), olive::LoopMode::k_loop_mode_loop);
	clip.set_loop_mode(olive::LoopMode::k_loop_mode_clamp);
	EXPECT_EQ(clip.loop_mode(), olive::LoopMode::k_loop_mode_clamp);
	clip.set_loop_mode(olive::LoopMode::k_loop_mode_off);
	EXPECT_EQ(clip.loop_mode(), olive::LoopMode::k_loop_mode_off);

	clip.set_maintain_audio_pitch(true);
	EXPECT_TRUE(clip.maintain_audio_pitch());

	clip.set_autocache(true);
	EXPECT_TRUE(clip.is_autocaching());
	clip.set_autocache(false);
	EXPECT_FALSE(clip.is_autocaching());
}

TEST(ClipBlock, LoopModeChangeEmitsPreviewChanged)
{
	olive::ClipBlock clip;

	int emissions = 0;
	QObject::connect(&clip, &olive::Block::preview_changed,
					 [&emissions]() { ++emissions; });

	clip.set_loop_mode(olive::LoopMode::k_loop_mode_loop);
	EXPECT_EQ(emissions, 1);

	clip.set_loop_mode(olive::LoopMode::k_loop_mode_clamp);
	EXPECT_EQ(emissions, 2);
}

TEST(ClipBlock, MediaInAccessor)
{
	olive::ClipBlock clip;

	clip.set_media_in(olive::core::Rational(5));
	EXPECT_EQ(clip.media_in(), olive::core::Rational(5));
	EXPECT_EQ(clip.get_standard_value(olive::ClipBlock::k_media_in_input)
				  .value<olive::core::Rational>(),
			  olive::core::Rational(5));
}

TEST(ClipBlock, InputTimeAdjustmentPassesThroughByDefault)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	const olive::TimeRange range(olive::core::Rational(2),
								 olive::core::Rational(4));

	// A default clip (speed 1, no reverse, no media in) maps times unchanged
	EXPECT_EQ(clip->input_time_adjustment(olive::ClipBlock::k_buffer_in, -1, range,
										true),
			  range);

	// Non-buffer inputs never adjust time
	EXPECT_EQ(clip->input_time_adjustment(olive::ClipBlock::k_speed_input, -1,
										range, true),
			  range);
	EXPECT_EQ(clip->output_time_adjustment(olive::ClipBlock::k_speed_input, -1,
										 range),
			  range);
}

TEST(ClipBlock, InputTimeAdjustmentAppliesSpeed)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	EXPECT_EQ(clip->input_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(4),
							   olive::core::Rational(8)));

	clip->set_standard_value(olive::ClipBlock::k_speed_input, 0.5);
	EXPECT_EQ(clip->input_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(1),
							   olive::core::Rational(2)));

	// Media in is added after the speed multiplication
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	clip->set_media_in(olive::core::Rational(3));
	EXPECT_EQ(clip->input_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(4)),
				  true),
			  olive::TimeRange(olive::core::Rational(7),
							   olive::core::Rational(11)));
}

TEST(ClipBlock, InputTimeAdjustmentZeroSpeedHoldsAtMediaIn)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	clip->set_standard_value(olive::ClipBlock::k_speed_input, 0.0);
	clip->set_media_in(olive::core::Rational(5));

	// Zero speed collapses every sequence time onto the media in point
	const olive::TimeRange adjusted = clip->input_time_adjustment(
		olive::ClipBlock::k_buffer_in, -1,
		olive::TimeRange(olive::core::Rational(2), olive::core::Rational(4)),
		true);
	EXPECT_EQ(adjusted.in(), olive::core::Rational(5));
	EXPECT_EQ(adjusted.out(), olive::core::Rational(5));
}

TEST(ClipBlock, InputTimeAdjustmentAppliesReverse)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	clip->set_reverse(true);

	// Reverse mirrors time around the clip length; TimeRange normalizes the
	// resulting inverted range
	EXPECT_EQ(clip->input_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(3)),
				  true),
			  olive::TimeRange(olive::core::Rational(7),
							   olive::core::Rational(8)));
}

TEST(ClipBlock, InputTimeAdjustmentCombinesReverseSpeedAndMediaIn)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	clip->set_reverse(true);
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	clip->set_media_in(olive::core::Rational(5));

	// (10 - 2) * 2 + 5 = 21, (10 - 3) * 2 + 5 = 19, normalized to [19, 21]
	EXPECT_EQ(clip->input_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  olive::TimeRange(olive::core::Rational(2),
								   olive::core::Rational(3)),
				  true),
			  olive::TimeRange(olive::core::Rational(19),
							   olive::core::Rational(21)));
}

TEST(ClipBlock, InputTimeAdjustmentPassesThroughInfinities)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	clip->set_reverse(true);
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	clip->set_media_in(olive::core::Rational(5));

	const olive::core::Rational k_min(INT_MIN);
	const olive::core::Rational k_max(INT_MAX);
	const olive::TimeRange infinite(k_min, k_max);

	EXPECT_EQ(clip->input_time_adjustment(olive::ClipBlock::k_buffer_in, -1,
										infinite, true),
			  infinite);
	EXPECT_EQ(clip->output_time_adjustment(olive::ClipBlock::k_buffer_in, -1,
										 infinite),
			  infinite);
}

TEST(ClipBlock, OutputTimeAdjustmentInvertsInputAdjustment)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	clip->set_media_in(olive::core::Rational(5));

	// Media time is converted back by subtracting media in and dividing speed
	EXPECT_EQ(clip->output_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  olive::TimeRange(olive::core::Rational(5),
								   olive::core::Rational(9))),
			  olive::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(2)));

	// Round trip through both adjustments returns the original range
	const olive::TimeRange range(olive::core::Rational(1),
								 olive::core::Rational(3));
	EXPECT_EQ(clip->output_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  clip->input_time_adjustment(olive::ClipBlock::k_buffer_in, -1,
											range, true)),
			  range);

	// Round trip also holds in reverse
	clip->set_reverse(true);
	EXPECT_EQ(clip->output_time_adjustment(
				  olive::ClipBlock::k_buffer_in, -1,
				  clip->input_time_adjustment(olive::ClipBlock::k_buffer_in, -1,
											range, true)),
			  range);
}

TEST(ClipBlock, MediaRangeReflectsSpeedReverseAndMediaIn)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	EXPECT_EQ(clip->media_range(),
			  olive::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(10)));

	clip->set_media_in(olive::core::Rational(5));
	EXPECT_EQ(clip->media_range(),
			  olive::TimeRange(olive::core::Rational(5),
							   olive::core::Rational(15)));

	// A 2x clip consumes twice its length in media time
	clip->set_media_in(olive::core::Rational(0));
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	EXPECT_EQ(clip->media_range(),
			  olive::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(20)));

	// Reverse maps the same media extent (the range normalizes)
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 1.0);
	clip->set_media_in(olive::core::Rational(5));
	clip->set_reverse(true);
	EXPECT_EQ(clip->media_range(),
			  olive::TimeRange(olive::core::Rational(5),
							   olive::core::Rational(15)));
}

TEST(ClipBlock, SetLengthAndMediaOutInReversePreservesMediaOut)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	clip->set_reverse(true);

	// Trimming the out point of a reversed clip moves the media in point so
	// that the media out point is preserved
	clip->set_length_and_media_out(olive::core::Rational(4));

	EXPECT_EQ(clip->length(), olive::core::Rational(4));
	EXPECT_EQ(clip->media_in(), olive::core::Rational(6));
	EXPECT_EQ(clip->media_range(),
			  olive::TimeRange(olive::core::Rational(6),
							   olive::core::Rational(10)));
}

TEST(ClipBlock, SetLengthAndMediaInForwardAdjustsMediaIn)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	// Trimming the in point of a forward clip pushes the media in point
	// forward by the removed amount
	clip->set_length_and_media_in(olive::core::Rational(4));

	EXPECT_EQ(clip->length(), olive::core::Rational(4));
	EXPECT_EQ(clip->media_in(), olive::core::Rational(6));
	EXPECT_EQ(clip->media_range(),
			  olive::TimeRange(olive::core::Rational(6),
							   olive::core::Rational(10)));
}

TEST(ClipBlock, ConnectedCacheAccessors)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	EXPECT_EQ(clip->connected_video_cache(), nullptr);
	EXPECT_EQ(clip->connected_audio_cache(), nullptr);
	EXPECT_EQ(clip->thumbnails(), nullptr);
	EXPECT_EQ(clip->waveform(), nullptr);

	olive::SolidGenerator *solid = create_solid(&project);
	olive::Node::connect_edge(solid,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	EXPECT_EQ(clip->connected_video_cache(), solid->video_frame_cache());
	EXPECT_EQ(clip->connected_audio_cache(), solid->audio_playback_cache());
	EXPECT_EQ(clip->thumbnails(), solid->thumbnail_cache());
	EXPECT_EQ(clip->waveform(), solid->waveform_cache());
}

TEST(ClipBlock, InvalidateCacheTransformsMediaTimeToSequenceTime)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);

	olive::SolidGenerator *solid = create_solid(&project);
	olive::Node::connect_edge(solid,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	QVector<olive::TimeRange> invalidated;
	QObject::connect(clip->video_frame_cache(),
					 &olive::PlaybackCache::invalidated,
					 [&invalidated](const olive::TimeRange &r) {
						 invalidated.append(r);
					 });

	// An invalidation in media time [4,8] covers sequence time [2,4] at 2x
	solid->invalidate_cache(olive::TimeRange(olive::core::Rational(4),
											olive::core::Rational(8)),
						   olive::SolidGenerator::k_color_input);

	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(),
			  olive::TimeRange(olive::core::Rational(2),
							   olive::core::Rational(4)));
}

TEST(ClipBlock, InvalidateCacheReverseTransformsRange)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	clip->set_reverse(true);

	olive::SolidGenerator *solid = create_solid(&project);
	olive::Node::connect_edge(solid,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	QVector<olive::TimeRange> invalidated;
	QObject::connect(clip->video_frame_cache(),
					 &olive::PlaybackCache::invalidated,
					 [&invalidated](const olive::TimeRange &r) {
						 invalidated.append(r);
					 });

	// Media time [4,8] maps to sequence time [2,6] when reversed
	solid->invalidate_cache(olive::TimeRange(olive::core::Rational(4),
											olive::core::Rational(8)),
						   olive::SolidGenerator::k_color_input);

	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(),
			  olive::TimeRange(olive::core::Rational(2),
							   olive::core::Rational(6)));
}

TEST(ClipBlock, InvalidateCacheZeroSpeedInvalidatesWholeClip)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 0.0);

	olive::SolidGenerator *solid = create_solid(&project);
	olive::Node::connect_edge(solid,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	QVector<olive::TimeRange> invalidated;
	QObject::connect(clip->video_frame_cache(),
					 &olive::PlaybackCache::invalidated,
					 [&invalidated](const olive::TimeRange &r) {
						 invalidated.append(r);
					 });

	// With zero speed any media invalidation affects the whole clip
	solid->invalidate_cache(olive::TimeRange(olive::core::Rational(4),
											olive::core::Rational(8)),
						   olive::SolidGenerator::k_color_input);

	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(),
			  olive::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(10)));
}

TEST(ClipBlock, InvalidateCacheWithVideoTrackReachesConnectedCaches)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *track = new olive::Track();
	track->setParent(&project);
	track->set_type(olive::Track::k_video);

	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	track->append_block(clip);

	olive::SolidGenerator *solid = create_solid(&project);
	olive::Node::connect_edge(solid,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	QVector<olive::TimeRange> invalidated;
	QObject::connect(solid->video_frame_cache(),
					 &olive::PlaybackCache::invalidated,
					 [&invalidated](const olive::TimeRange &r) {
						 invalidated.append(r);
					 });
	QVector<olive::TimeRange> requested;
	QObject::connect(solid->video_frame_cache(),
					 &olive::PlaybackCache::requested,
					 [&requested](olive::ViewerOutput *, const olive::TimeRange &r) {
						 requested.append(r);
					 });

	// Without autocache the connected cache is invalidated but not requested
	clip->invalidate_cache(olive::TimeRange(olive::core::Rational(4),
										   olive::core::Rational(8)),
						  olive::ClipBlock::k_buffer_in, -1,
						  olive::Node::InvalidateCacheOptions());
	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(),
			  olive::TimeRange(olive::core::Rational(4),
							   olive::core::Rational(8)));
	EXPECT_TRUE(requested.isEmpty());

	// Enabling autocache re-requests everything currently invalidated (a
	// fresh cache has no validated ranges, so the full media range)
	clip->set_autocache(true);
	ASSERT_GE(requested.size(), 1);
	EXPECT_EQ(requested.first(),
			  olive::TimeRange(olive::core::Rational(0),
							   olive::core::Rational(10)));

	// With autocache on, invalidations are also requested
	invalidated.clear();
	requested.clear();
	clip->invalidate_cache(olive::TimeRange(olive::core::Rational(4),
										   olive::core::Rational(8)),
						  olive::ClipBlock::k_buffer_in, -1,
						  olive::Node::InvalidateCacheOptions());
	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(),
			  olive::TimeRange(olive::core::Rational(4),
							   olive::core::Rational(8)));
	ASSERT_EQ(requested.size(), 1);
	EXPECT_EQ(requested.first(),
			  olive::TimeRange(olive::core::Rational(4),
							   olive::core::Rational(8)));
}

TEST(ClipBlock, DiscardCacheInvalidatesConnectedNodeCache)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *track = new olive::Track();
	track->setParent(&project);
	track->set_type(olive::Track::k_video);

	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	track->append_block(clip);

	olive::SolidGenerator *solid = create_solid(&project);
	olive::Node::connect_edge(solid,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	QVector<olive::TimeRange> invalidated;
	QObject::connect(solid->video_frame_cache(),
					 &olive::PlaybackCache::invalidated,
					 [&invalidated](const olive::TimeRange &r) {
						 invalidated.append(r);
					 });

	clip->discard_cache();

	const olive::core::Rational k_min(INT_MIN);
	const olive::core::Rational k_max(INT_MAX);
	ASSERT_EQ(invalidated.size(), 1);
	EXPECT_EQ(invalidated.first(), olive::TimeRange(k_min, k_max));
}

TEST(ClipBlock, AddCachePassthroughFromUnvalidatedSourceAddsNoPassthroughs)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *a = create_clip(&project, olive::core::Rational(10));
	olive::ClipBlock *b = create_clip(&project, olive::core::Rational(10));

	// Passthroughs are only added for validated ranges of the source caches,
	// and a fresh cache has none
	b->add_cache_passthrough_from(a);

	EXPECT_TRUE(b->video_frame_cache()->get_passthroughs().empty());
	EXPECT_TRUE(b->audio_playback_cache()->get_passthroughs().empty());
}

TEST(ClipBlock, GetValueHintForBufferWithoutTrackHasNoPreference)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	// With no track the clip cannot prefer texture or samples and falls back
	// to the default (typeless) hint
	EXPECT_TRUE(clip->get_value_hint_for_input(olive::ClipBlock::k_buffer_in)
					.types()
					.isEmpty());
}

TEST(ClipBlock, FindMulticamReturnsNullWithoutMulticam)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	EXPECT_EQ(clip->find_multicam(), nullptr);

	olive::SolidGenerator *solid = create_solid(&project);
	olive::Node::connect_edge(solid,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));
	EXPECT_EQ(clip->find_multicam(), nullptr);
}

TEST(ClipTraverser, GenerateTablePropagatesSpeedAdjustedTime)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);

	auto *time = new olive::TimeInput();
	time->setParent(&project);
	olive::Node::connect_edge(time,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	// The connected node is evaluated in media time: sequence time 3 at 2x
	// speed reaches the time node as 6
	EXPECT_DOUBLE_EQ(generate_clip_time_value(clip, olive::core::Rational(3)),
					 6.0);
}

TEST(ClipTraverser, GenerateTablePropagatesMediaInOffset)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	clip->set_media_in(olive::core::Rational(5));

	auto *time = new olive::TimeInput();
	time->setParent(&project);
	olive::Node::connect_edge(time,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	EXPECT_DOUBLE_EQ(generate_clip_time_value(clip, olive::core::Rational(3)),
					 8.0);
}

TEST(ClipTraverser, GenerateTablePropagatesReverseTime)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	clip->set_reverse(true);

	auto *time = new olive::TimeInput();
	time->setParent(&project);
	olive::Node::connect_edge(time,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	// Reverse maps the frame range [t, t+1/30) onto media [7, 7-1/30), so the
	// connected node is evaluated at 7 - 1/30
	EXPECT_DOUBLE_EQ(generate_clip_time_value(clip, olive::core::Rational(3)),
					 7.0 - 1.0 / 30.0);
}

TEST(ClipTraverser, GenerateTableCombinesReverseSpeedAndMediaIn)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));
	clip->set_reverse(true);
	clip->set_standard_value(olive::ClipBlock::k_speed_input, 2.0);
	clip->set_media_in(olive::core::Rational(5));

	auto *time = new olive::TimeInput();
	time->setParent(&project);
	olive::Node::connect_edge(time,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	// (10 - 1/30 - 3) * 2 + 5 = 19 - 2/30
	EXPECT_DOUBLE_EQ(generate_clip_time_value(clip, olive::core::Rational(3)),
					 19.0 - 2.0 / 30.0);
}

TEST(ClipTraverser, GenerateTablePicksUpClipLoopMode)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();
	olive::ClipBlock *clip = create_clip(&project, olive::core::Rational(10));

	auto *probe = new LoopModeProbeNode();
	probe->setParent(&project);
	olive::Node::connect_edge(probe,
							 olive::NodeInput(clip, olive::ClipBlock::k_buffer_in));

	generate_clip_table(clip, olive::core::Rational(0));
	EXPECT_EQ(probe->last_loop_mode(), olive::LoopMode::k_loop_mode_off);

	clip->set_loop_mode(olive::LoopMode::k_loop_mode_loop);
	generate_clip_table(clip, olive::core::Rational(0));
	EXPECT_EQ(probe->last_loop_mode(), olive::LoopMode::k_loop_mode_loop);

	clip->set_loop_mode(olive::LoopMode::k_loop_mode_clamp);
	generate_clip_table(clip, olive::core::Rational(0));
	EXPECT_EQ(probe->last_loop_mode(), olive::LoopMode::k_loop_mode_clamp);
}

TEST(ClipTraverser, GenerateTableDefaultsToLoopModeOff)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *probe = new LoopModeProbeNode();
	probe->setParent(&project);

	olive::NodeTraverser traverser;
	traverser.generate_table(
		probe, olive::TimeRange(olive::core::Rational(0),
								olive::core::Rational(1, 30)));
	EXPECT_EQ(probe->last_loop_mode(), olive::LoopMode::k_loop_mode_off);
}

TEST(ClipTraverser, TransformAccumulatesGizmosAlongPath)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *start = new GizmoProbeNode();
	start->setParent(&project);
	start->set_gizmo_transform(QTransform().translate(1, 0));

	auto *middle = new GizmoProbeNode();
	middle->setParent(&project);
	middle->set_gizmo_transform(QTransform().translate(2, 0));

	auto *end = new GizmoProbeNode();
	end->setParent(&project);
	end->set_gizmo_transform(QTransform().translate(4, 0));

	olive::Node::connect_edge(start,
							 olive::NodeInput(middle, GizmoProbeNode::input_a()));
	olive::Node::connect_edge(middle,
							 olive::NodeInput(end, GizmoProbeNode::input_a()));

	const olive::TimeRange range(olive::core::Rational(0),
								 olive::core::Rational(1, 30));

	// The start node defines the reference frame, so only the gizmos of the
	// nodes between start and end (inclusive of end) are accumulated
	olive::NodeTraverser traverser;
	QTransform t;
	traverser.transform(&t, start, end, range);
	EXPECT_DOUBLE_EQ(t.dx(), 6.0);
	EXPECT_DOUBLE_EQ(t.dy(), 0.0);

	// Stopping at the middle node accumulates only its gizmo
	olive::NodeTraverser traverser2;
	QTransform t2;
	traverser2.transform(&t2, start, middle, range);
	EXPECT_DOUBLE_EQ(t2.dx(), 2.0);
	EXPECT_DOUBLE_EQ(t2.dy(), 0.0);
}

TEST(ClipTraverser, TransformIgnoresNodesOffThePath)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *start = new GizmoProbeNode();
	start->setParent(&project);
	start->set_gizmo_transform(QTransform().translate(1, 0));

	auto *end = new GizmoProbeNode();
	end->setParent(&project);
	end->set_gizmo_transform(QTransform().translate(4, 0));

	// Connected to the end node but not on the start->end path
	auto *off_path = new GizmoProbeNode();
	off_path->setParent(&project);
	off_path->set_gizmo_transform(QTransform().translate(100, 0));

	olive::Node::connect_edge(start,
							 olive::NodeInput(end, GizmoProbeNode::input_a()));
	olive::Node::connect_edge(off_path,
							 olive::NodeInput(end, GizmoProbeNode::input_b()));

	const olive::TimeRange range(olive::core::Rational(0),
								 olive::core::Rational(1, 30));

	olive::NodeTraverser traverser;
	QTransform t;
	traverser.transform(&t, start, end, range);
	EXPECT_DOUBLE_EQ(t.dx(), 4.0);
	EXPECT_DOUBLE_EQ(t.dy(), 0.0);
}

TEST(ClipTraverser, TransformWithSameStartAndEndIsIdentity)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *node = new GizmoProbeNode();
	node->setParent(&project);
	node->set_gizmo_transform(QTransform().translate(4, 0));

	const olive::TimeRange range(olive::core::Rational(0),
								 olive::core::Rational(1, 30));

	olive::NodeTraverser traverser;
	QTransform t;
	traverser.transform(&t, node, node, range);
	EXPECT_TRUE(t.isIdentity());
}

TEST(ClipTraverser, ViewerConnectedOutputsResolveThroughGraph)
{
	olive::ColorManager::set_up_default_config();
	olive::Project project;
	project.initialize();

	auto *viewer = new olive::ViewerOutput();
	viewer->setParent(&project);

	EXPECT_EQ(viewer->get_connected_texture_output(), nullptr);
	EXPECT_EQ(viewer->get_connected_sample_output(), nullptr);

	auto *texture_source = new GizmoProbeNode();
	texture_source->setParent(&project);
	auto *sample_source = new GizmoProbeNode();
	sample_source->setParent(&project);

	olive::Node::connect_edge(
		texture_source,
		olive::NodeInput(viewer, olive::ViewerOutput::k_texture_input));
	olive::Node::connect_edge(
		sample_source,
		olive::NodeInput(viewer, olive::ViewerOutput::k_samples_input));

	EXPECT_EQ(viewer->get_connected_texture_output(), texture_source);
	EXPECT_EQ(viewer->get_connected_sample_output(), sample_source);

	// The value hint getters delegate to the corresponding input hints
	EXPECT_EQ(viewer->get_connected_texture_value_hint().types(),
			  viewer->get_value_hint_for_input(olive::ViewerOutput::k_texture_input)
				  .types());
	EXPECT_EQ(viewer->get_connected_sample_value_hint().types(),
			  viewer->get_value_hint_for_input(olive::ViewerOutput::k_samples_input)
				  .types());

	olive::Node::disconnect_edge(
		texture_source,
		olive::NodeInput(viewer, olive::ViewerOutput::k_texture_input));
	EXPECT_EQ(viewer->get_connected_texture_output(), nullptr);
	EXPECT_EQ(viewer->get_connected_sample_output(), sample_source);
}
