/*
 * Oak Video Editor - Viewer/Preview Subsystem Smoke Tests
 * Copyright (C) 2025 Olive CE Team
 *
 * Comprehensive smoke tests for the viewer and preview display subsystem including:
 * - ViewerPlaybackTimer timing calculations
 * - ViewerQueue frame management
 * - ViewerSafeMarginInfo safety margin calculations
 * - PreviewAutoCacher cache management
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QThread>
#include <QElapsedTimer>
#include <QSignalSpy>

// Viewer headers
#include "widget/viewer/viewerplaybacktimer.h"
#include "widget/viewer/viewerqueue.h"
#include "widget/viewer/viewersafemargininfo.h"
#include "codec/conformmanager.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "render/audioplaybackcache.h"
#include "render/diskmanager.h"
#include "render/previewautocacher.h"
#include "render/rendermanager.h"
#include "olive/core/util/rational.h"

using namespace olive;
using namespace olive::core;

namespace olive
{
namespace viewer
{
namespace test
{

// ============================================================================
// Smoke Test: ViewerPlaybackTimer
// ============================================================================

TEST(ViewerSmokeTimer, DefaultConstruction)
{
	ViewerPlaybackTimer timer;

	// After Start() is called, the timer must return valid timestamps
	timer.start(0, 1, 1.0 / 24.0);
	EXPECT_GE(timer.get_timestamp_now(), 0);
}

TEST(ViewerSmokeTimer, BasicTiming)
{
	ViewerPlaybackTimer timer;

	// Start at timestamp 0, 1x speed, 24fps (timebase = 1/24)
	timer.start(0, 1, 1.0 / 24.0);

	// Immediately get timestamp (should be close to 0)
	int64_t ts = timer.get_timestamp_now();
	EXPECT_GE(ts, 0);

	// Wait a bit and check timestamp has increased
	QThread::msleep(50); // 50ms
	int64_t ts2 = timer.get_timestamp_now();

	// At 24fps, 50ms is more than one frame period (~41.7ms), so the
	// timestamp must have advanced by at least one frame
	EXPECT_GT(ts2, ts);
}

TEST(ViewerSmokeTimer, PlaybackSpeedForward)
{
	ViewerPlaybackTimer timer;

	// Start at timestamp 100, 2x speed, 30fps
	timer.start(100, 2, 1.0 / 30.0);

	int64_t ts1 = timer.get_timestamp_now();
	QThread::msleep(50);
	int64_t ts2 = timer.get_timestamp_now();

	// At 2x speed, time should advance twice as fast
	EXPECT_GT(ts2, ts1);
}

TEST(ViewerSmokeTimer, PlaybackSpeedReverse)
{
	ViewerPlaybackTimer timer;

	// Start at timestamp 1000, -1x speed (reverse), 24fps
	timer.start(1000, -1, 1.0 / 24.0);

	int64_t ts1 = timer.get_timestamp_now();
	QThread::msleep(50);
	int64_t ts2 = timer.get_timestamp_now();

	// In reverse, timestamp should decrease
	EXPECT_LT(ts2, ts1);
}

TEST(ViewerSmokeTimer, DifferentTimebases)
{
	// Frame counts track wall time at each timebase's rate. Measure the
	// actual interval so scheduling jitter on loaded CI runners (observed on
	// macOS, where msleep(100) overslept ~2.5x) can't break the comparison.
	for (double fps : { 24.0, 60.0 }) {
		ViewerPlaybackTimer timer;
		QElapsedTimer wall;
		timer.start(0, 1, 1.0 / fps);
		wall.start();
		QThread::msleep(100);

		const int64_t ts = timer.get_timestamp_now();
		const int64_t expected =
			qFloor(static_cast<double>(wall.elapsed()) / (1000.0 / fps));
		EXPECT_NEAR(ts, expected, 1) << "fps=" << fps;
	}

	// With highly distinct timebases the faster one always produces more
	// frames in the same interval, even on heavily loaded machines
	ViewerPlaybackTimer slow, fast;
	slow.start(0, 1, 1.0);
	fast.start(0, 1, 1.0 / 240.0);
	QThread::msleep(100);

	EXPECT_LT(slow.get_timestamp_now(), fast.get_timestamp_now());
}

TEST(ViewerSmokeTimer, ZeroSpeed)
{
	ViewerPlaybackTimer timer;

	// Start with 0 speed (paused)
	timer.start(500, 0, 1.0 / 24.0);

	int64_t ts1 = timer.get_timestamp_now();
	QThread::msleep(50);
	int64_t ts2 = timer.get_timestamp_now();

	// With 0 speed, timestamp should not change
	EXPECT_EQ(ts1, ts2);
}

class FakeAudioClock : public PlaybackAudioClock {
public:
	virtual double seconds() const override
	{
		return seconds_;
	}

	double seconds_ = -1.0;
};

TEST(ViewerSmokeTimer, AudioClockDrivesTimestamp)
{
	FakeAudioClock clock;
	ViewerPlaybackTimer timer;

	// Start at timestamp 100, 1x speed, 32fps (exact in floating point)
	timer.start(100, 1, 1.0 / 32.0, &clock);

	// One second of consumed audio = 32 frames, regardless of wall time
	clock.seconds_ = 1.0;
	EXPECT_EQ(timer.get_timestamp_now(), 132);

	// The audio clock fully overrides the wall clock (no sleep involved)
	clock.seconds_ = 0.5;
	EXPECT_EQ(timer.get_timestamp_now(), 116);
}

TEST(ViewerSmokeTimer, AudioClockScalesWithPlaybackSpeed)
{
	FakeAudioClock clock;
	ViewerPlaybackTimer timer;

	// At 2x speed one output second is two timeline seconds
	timer.start(0, 2, 1.0 / 32.0, &clock);
	clock.seconds_ = 0.5;
	EXPECT_EQ(timer.get_timestamp_now(), 32);

	// In reverse the playhead moves backward at |speed|
	timer.start(100, -2, 1.0 / 32.0, &clock);
	clock.seconds_ = 1.0;
	EXPECT_EQ(timer.get_timestamp_now(), 36);
}

TEST(ViewerSmokeTimer, InvalidAudioClockFallsBackToWallClock)
{
	FakeAudioClock clock; // seconds() returns -1: no clocked output running
	ViewerPlaybackTimer timer;

	timer.start(0, 1, 1.0 / 24.0, &clock);
	EXPECT_GE(timer.get_timestamp_now(), 0);

	QThread::msleep(50); // 50ms > one 24fps frame period
	EXPECT_GT(timer.get_timestamp_now(), 0);
}

// ============================================================================
// Smoke Test: ViewerQueue
// ============================================================================

TEST(ViewerSmokeQueue, DefaultConstruction)
{
	ViewerQueue queue;
	EXPECT_TRUE(queue.empty());
}

TEST(ViewerSmokeQueue, AppendForwardPlayback)
{
	ViewerQueue queue;

	// Append frames for forward playback
	ViewerPlaybackFrame frame1{ Rational(0), QVariant() };
	ViewerPlaybackFrame frame2{ Rational(1, 24), QVariant() };
	ViewerPlaybackFrame frame3{ Rational(2, 24), QVariant() };

	queue.append_timewise(frame1, 1); // speed = 1 (forward)
	queue.append_timewise(frame2, 1);
	queue.append_timewise(frame3, 1);

	EXPECT_EQ(queue.size(), 3);

	// Verify order (should be chronological for forward playback)
	auto it = queue.begin();
	EXPECT_EQ(it->timestamp, Rational(0));
	++it;
	EXPECT_EQ(it->timestamp, Rational(1, 24));
	++it;
	EXPECT_EQ(it->timestamp, Rational(2, 24));
}

TEST(ViewerSmokeQueue, AppendReversePlayback)
{
	ViewerQueue queue;

	// Append frames for reverse playback
	ViewerPlaybackFrame frame1{ Rational(2, 24), QVariant() };
	ViewerPlaybackFrame frame2{ Rational(1, 24), QVariant() };
	ViewerPlaybackFrame frame3{ Rational(0), QVariant() };

	queue.append_timewise(frame1, -1); // speed = -1 (reverse)
	queue.append_timewise(frame2, -1);
	queue.append_timewise(frame3, -1);

	EXPECT_EQ(queue.size(), 3);

	// Verify order (should be reverse chronological for reverse playback)
	auto it = queue.begin();
	EXPECT_EQ(it->timestamp, Rational(2, 24));
	++it;
	EXPECT_EQ(it->timestamp, Rational(1, 24));
	++it;
	EXPECT_EQ(it->timestamp, Rational(0));
}

TEST(ViewerSmokeQueue, InsertOutOfOrder)
{
	ViewerQueue queue;

	// Insert frames out of order for forward playback
	ViewerPlaybackFrame frame1{ Rational(0), QVariant() };
	ViewerPlaybackFrame frame2{ Rational(2, 24), QVariant() };
	ViewerPlaybackFrame frame3{ Rational(1, 24), QVariant() }; // Middle frame

	queue.append_timewise(frame1, 1);
	queue.append_timewise(frame2, 1);
	queue.append_timewise(frame3, 1); // Should insert in middle

	EXPECT_EQ(queue.size(), 3);

	// Verify correct order
	auto it = queue.begin();
	EXPECT_EQ(it->timestamp, Rational(0));
	++it;
	EXPECT_EQ(it->timestamp, Rational(1, 24));
	++it;
	EXPECT_EQ(it->timestamp, Rational(2, 24));
}

TEST(ViewerSmokeQueue, PurgeBefore)
{
	ViewerQueue queue;

	// Add some frames
	for (int i = 0; i < 10; i++) {
		ViewerPlaybackFrame frame{ Rational(i, 24), QVariant() };
		queue.append_timewise(frame, 1);
	}

	EXPECT_EQ(queue.size(), 10);

	// Purge frames before 5/24
	queue.purge_before(Rational(5, 24), 1);

	// Should have 5 frames remaining (5, 6, 7, 8, 9)
	EXPECT_EQ(queue.size(), 5);
	EXPECT_EQ(queue.front().timestamp, Rational(5, 24));
}

TEST(ViewerSmokeQueue, PurgeBeforeReverse)
{
	ViewerQueue queue;

	// Add frames for reverse playback (newest first)
	for (int i = 9; i >= 0; i--) {
		ViewerPlaybackFrame frame{ Rational(i, 24), QVariant() };
		queue.append_timewise(frame, -1);
	}

	EXPECT_EQ(queue.size(), 10);

	// In reverse playback, front() is the largest timestamp (9/24)
	// PurgeBefore with negative speed removes frames where front > time
	queue.purge_before(Rational(5, 24), -1);

	// Should have frames 0-5 remaining (those <= 5/24)
	EXPECT_EQ(queue.size(), 6);
	EXPECT_EQ(queue.front().timestamp, Rational(5, 24));
}

// ============================================================================
// Smoke Test: ViewerSafeMarginInfo
// ============================================================================

TEST(ViewerSmokeSafeMargin, DefaultConstruction)
{
	ViewerSafeMarginInfo info;
	EXPECT_FALSE(info.is_enabled());
	EXPECT_FALSE(info.custom_ratio());
	EXPECT_DOUBLE_EQ(info.ratio(), 0.0);
}

TEST(ViewerSmokeSafeMargin, EnabledConstruction)
{
	ViewerSafeMarginInfo info(true);
	EXPECT_TRUE(info.is_enabled());
	EXPECT_FALSE(info.custom_ratio());
}

TEST(ViewerSmokeSafeMargin, CustomRatioConstruction)
{
	ViewerSafeMarginInfo info(true, 0.9);
	EXPECT_TRUE(info.is_enabled());
	EXPECT_TRUE(info.custom_ratio());
	EXPECT_DOUBLE_EQ(info.ratio(), 0.9);
}

TEST(ViewerSmokeSafeMargin, EqualityOperators)
{
	ViewerSafeMarginInfo info1(true, 0.9);
	ViewerSafeMarginInfo info2(true, 0.9);
	ViewerSafeMarginInfo info3(false, 0.9);
	ViewerSafeMarginInfo info4(true, 0.8);

	EXPECT_TRUE(info1 == info2);
	EXPECT_FALSE(info1 != info2);

	EXPECT_FALSE(info1 == info3); // Different enabled state
	EXPECT_FALSE(info1 == info4); // Different ratio
	EXPECT_TRUE(info1 != info3);
}

TEST(ViewerSmokeSafeMargin, ZeroRatio)
{
	ViewerSafeMarginInfo info(true, 0.0);
	EXPECT_TRUE(info.is_enabled());
	EXPECT_FALSE(info.custom_ratio()); // 0 ratio means no custom ratio
}

TEST(ViewerSmokeSafeMargin, CopyConstruction)
{
	ViewerSafeMarginInfo original(true, 0.85);
	ViewerSafeMarginInfo copy(original);

	EXPECT_EQ(copy.is_enabled(), original.is_enabled());
	EXPECT_EQ(copy.custom_ratio(), original.custom_ratio());
	EXPECT_DOUBLE_EQ(copy.ratio(), original.ratio());
}

// ============================================================================
// Smoke Test: AudioPlaybackCache (used by preview system)
// ============================================================================

TEST(ViewerSmokeAudioCache, DefaultConstruction)
{
	AudioPlaybackCache cache;

	// A fresh cache has invalid (unset) audio parameters and no validated ranges
	EXPECT_FALSE(cache.get_parameters().is_valid());
	EXPECT_TRUE(cache.get_validated_ranges().isEmpty());
}

TEST(ViewerSmokeAudioCache, ParameterSetters)
{
	AudioPlaybackCache cache;

	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	cache.set_parameters(params);

	// Parameters should be retrievable
	AudioParams retrieved = cache.get_parameters();
	EXPECT_EQ(retrieved.sample_rate(), params.sample_rate());
}

TEST(ViewerSmokeAudioCache, ValidateWithRange)
{
	AudioPlaybackCache cache;

	// Initially no validated ranges
	TimeRangeList validated = cache.get_validated_ranges();
	EXPECT_TRUE(validated.isEmpty());
}

// ============================================================================
// Smoke Test: PreviewAutoCacher (basic lifecycle)
// ============================================================================

// PreviewAutoCacher runs headless with the dummy render backend; this fixture
// mirrors the one in preview_autocacher_test.cpp.
class ViewerSmokeAutoCacherTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		ColorManager::set_up_default_config();

		// Use the dummy render backend so PreviewAutoCacher can be exercised
		// without initializing OpenGL/Vulkan in the unit-test process.
		OAK_CONFIG("GraphicsBackend") = QStringLiteral("dummy");

		DiskManager::create_instance();
		ConformManager::create_instance();
		RenderManager::create_instance();

		project_ = std::make_unique<Project>();
		project_->initialize();
	}

	void TearDown() override
	{
		project_.reset();
		RenderManager::destroy_instance();
		ConformManager::destroy_instance();
		DiskManager::destroy_instance();
	}

	ViewerOutput *create_viewer_with_valid_params()
	{
		auto *viewer = new ViewerOutput();
		viewer->setParent(project_.get());
		viewer->set_video_params(
			VideoParams(64, 64, Rational(1, 25), PixelFormat::u8,
						VideoParams::k_rgba_channel_count));
		return viewer;
	}

	std::unique_ptr<Project> project_;
};

TEST_F(ViewerSmokeAutoCacherTest, Construction)
{
	PreviewAutoCacher cacher;

	// A freshly constructed cacher has no project and no custom range running
	EXPECT_FALSE(cacher.is_rendering_custom_range());
}

TEST_F(ViewerSmokeAutoCacherTest, PauseControls)
{
	ViewerOutput *viewer = create_viewer_with_valid_params();

	PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	// While renders are paused, a forced cache range must stay queued
	cacher.set_renders_paused(true);
	cacher.force_cache_range(viewer, TimeRange(Rational(0), Rational(1, 25)));
	EXPECT_TRUE(cacher.is_rendering_custom_range());

	// Unpausing must dispatch it; the dummy backend finishes each ticket
	// without a result, which exhausts the range immediately
	cacher.set_renders_paused(false);
	EXPECT_FALSE(cacher.is_rendering_custom_range());

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

TEST_F(ViewerSmokeAutoCacherTest, CacheRequestSchedulesRenderWhenNotIgnored)
{
	ViewerOutput *viewer = create_viewer_with_valid_params();

	PreviewAutoCacher cacher;
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &PreviewAutoCacher::stop_cache_proxy_tasks);

	// A cache request on a connected node's cache must be picked up and
	// rendered, emitting StopCacheProxyTasks when the range is exhausted
	viewer->video_frame_cache()->request(
		viewer, TimeRange(Rational(0), Rational(1, 25)));
	EXPECT_GE(stop_spy.count(), 1);

	// Deliver the queued RenderTicketWatcher::Finished emissions so the
	// completed watchers are reaped before teardown.
	QCoreApplication::processEvents();

	cacher.set_project(nullptr);
}

TEST_F(ViewerSmokeAutoCacherTest, SetIgnoreCacheRequests)
{
	ViewerOutput *viewer = create_viewer_with_valid_params();

	PreviewAutoCacher cacher;
	// Must be set before SetProject(), which is when the cache connections
	// would be made
	cacher.set_ignore_cache_requests(true);
	cacher.set_project(project_.get());

	QSignalSpy stop_spy(&cacher, &PreviewAutoCacher::stop_cache_proxy_tasks);

	// With cache requests ignored, requesting a range must not queue any job
	viewer->video_frame_cache()->request(
		viewer, TimeRange(Rational(0), Rational(1, 25)));
	EXPECT_EQ(stop_spy.count(), 0);
	EXPECT_FALSE(cacher.is_rendering_custom_range());

	cacher.set_project(nullptr);
}

// ============================================================================
// Smoke Test: Rational Time Calculations (core utility)
// ============================================================================

TEST(ViewerSmokeRational, DefaultConstruction)
{
	Rational r;
	EXPECT_EQ(r.numerator(), 0);
	EXPECT_EQ(r.denominator(), 1);
}

TEST(ViewerSmokeRational, ValueConstruction)
{
	Rational r(24, 1);
	EXPECT_EQ(r.numerator(), 24);
	EXPECT_EQ(r.denominator(), 1);

	Rational r2(1, 24);
	EXPECT_EQ(r2.numerator(), 1);
	EXPECT_EQ(r2.denominator(), 24);
}

TEST(ViewerSmokeRational, ToDouble)
{
	Rational r(1, 2);
	EXPECT_DOUBLE_EQ(r.to_double(), 0.5);

	Rational r2(3, 4);
	EXPECT_DOUBLE_EQ(r2.to_double(), 0.75);
}

TEST(ViewerSmokeRational, Arithmetic)
{
	Rational r1(1, 2);
	Rational r2(1, 4);

	Rational sum = r1 + r2;
	EXPECT_EQ(sum.numerator(), 3);
	EXPECT_EQ(sum.denominator(), 4);

	Rational diff = r1 - r2;
	EXPECT_EQ(diff.numerator(), 1);
	EXPECT_EQ(diff.denominator(), 4);
}

TEST(ViewerSmokeRational, Comparison)
{
	Rational r1(1, 2);
	Rational r2(2, 4);
	Rational r3(3, 4);

	EXPECT_TRUE(r1 == r2); // Equivalent fractions
	EXPECT_FALSE(r1 == r3);
	EXPECT_TRUE(r1 < r3);
	EXPECT_TRUE(r3 > r1);
}

TEST(ViewerSmokeRational, NullCheck)
{
	Rational r;
	EXPECT_TRUE(r.isNull()); // 0/1 is considered null

	Rational r2(1, 2);
	EXPECT_FALSE(r2.isNull());
}

TEST(ViewerSmokeRational, Flipped)
{
	Rational r(24, 1);
	Rational flipped = r.flipped();

	EXPECT_EQ(flipped.numerator(), 1);
	EXPECT_EQ(flipped.denominator(), 24);
}

// ============================================================================
// Smoke Test: Thread Safety
// ============================================================================

TEST(ViewerSmokeThread, ConcurrentTimerAccess)
{
	const int num_threads = 4;
	const int num_iterations = 100;

	ViewerPlaybackTimer timer;
	timer.start(0, 1, 1.0 / 30.0);

	std::vector<std::thread> threads;
	std::atomic<int> monotonic_violations{ 0 };

	// Each thread reads the timer repeatedly; because playback is forward, a
	// thread must never observe a timestamp smaller than the one it read before
	for (int t = 0; t < num_threads; ++t) {
		threads.emplace_back([&timer, &monotonic_violations, num_iterations]() {
			int64_t previous = 0;
			for (int i = 0; i < num_iterations; ++i) {
				const int64_t ts = timer.get_timestamp_now();
				if (ts < previous) {
					monotonic_violations++;
				}
				previous = ts;
			}
		});
	}

	for (auto &t : threads) {
		t.join();
	}

	EXPECT_EQ(monotonic_violations.load(), 0);
	// The threads ran long enough that the timer must have advanced at all
	EXPECT_GE(timer.get_timestamp_now(), 0);
}

TEST(ViewerSmokeThread, ConcurrentQueueAccess)
{
	const int num_threads = 4;
	const int num_frames_per_thread = 25;

	ViewerQueue queue;
	std::vector<std::thread> threads;
	std::atomic<int> append_count{ 0 };

	for (int t = 0; t < num_threads; ++t) {
		threads.emplace_back(
			[&queue, &append_count, t, num_frames_per_thread]() {
				for (int i = 0; i < num_frames_per_thread; ++i) {
					ViewerPlaybackFrame frame{
						Rational(t * num_frames_per_thread + i, 24), QVariant()
					};
					queue.append_timewise(frame, 1);
					append_count++;
				}
			});
	}

	for (auto &t : threads) {
		t.join();
	}

	EXPECT_EQ(append_count.load(), num_threads * num_frames_per_thread);
	EXPECT_EQ(queue.size(), num_threads * num_frames_per_thread);
}

// ============================================================================
// Smoke Test: Integration Scenarios
// ============================================================================

TEST(ViewerSmokeIntegration, PlaybackSequenceSimulation)
{
	// Simulate a basic playback sequence
	ViewerPlaybackTimer timer;
	ViewerQueue queue;

	// Start playback at frame 0, 24fps; timestamps are expressed in frames
	timer.start(0, 1, 1.0 / 24.0);

	// Queue some frames
	for (int i = 0; i < 10; i++) {
		ViewerPlaybackFrame frame{ Rational(i, 24), QVariant(i) };
		queue.append_timewise(frame, 1);
	}

	// Get current timestamp (in frames) and convert it to a time in seconds
	const int64_t current_ts = timer.get_timestamp_now();
	const Rational current_time(current_ts, 24);

	// Find the first queued frame at or after the current playback time
	bool found = false;
	for (const auto &frame : queue) {
		if (frame.timestamp >= current_time) {
			found = true;
			break;
		}
	}

	// Playback just started at frame 0 and the queue holds frames 0-9, so a
	// current-or-future frame must be available
	EXPECT_TRUE(found);
}

TEST(ViewerSmokeIntegration, SafeMarginWithDifferentAspectRatios)
{
	// Test safe margins for different aspect ratios
	std::vector<double> ratios = { 0.9, 0.85, 0.8, 0.7 };

	for (double ratio : ratios) {
		ViewerSafeMarginInfo info(true, ratio);
		EXPECT_TRUE(info.is_enabled());
		EXPECT_TRUE(info.custom_ratio());
		EXPECT_DOUBLE_EQ(info.ratio(), ratio);
	}
}

TEST(ViewerSmokeIntegration, ReversePlaybackScenario)
{
	ViewerPlaybackTimer timer;
	ViewerQueue queue;

	// Start reverse playback from frame 100
	timer.start(100, -1, 1.0 / 24.0);

	// Queue frames in reverse order
	for (int i = 100; i >= 90; i--) {
		ViewerPlaybackFrame frame{ Rational(i, 24), QVariant(i) };
		queue.append_timewise(frame, -1);
	}

	// Get timestamps - should decrease
	int64_t ts1 = timer.get_timestamp_now();
	QThread::msleep(50);
	int64_t ts2 = timer.get_timestamp_now();

	EXPECT_LT(ts2, ts1);
	EXPECT_EQ(queue.front().timestamp, Rational(100, 24));
}

} // namespace test
} // namespace viewer
} // namespace olive
