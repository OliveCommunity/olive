/***
  Oak Video Editor - Regression test for TimelineWidget waveform sync
  Copyright (C) 2026 Oak Team
***/

#include <gtest/gtest.h>

#include <cmath>

#include "audio/audiovisualwaveform.h"
#include "node/block/clip/clip.h"
#include "node/node.h"
#include "node/project/footage/footage.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/render/sampleformat.h"
#include "render/audiowaveformcache.h"
#include "widget/timelinewidget/timelinewidgetwaveformsync.h"

using namespace olive;
using namespace olive::core;

namespace
{

AudioParams make_mono_params(int sample_rate)
{
	return AudioParams(sample_rate, static_cast<uint64_t>(k_channel_layout_mono),
					   SampleFormat::f32_p);
}

SampleBuffer make_mono_buffer(int sample_rate, float value, int seconds)
{
	SampleBuffer buf(make_mono_params(sample_rate),
					 static_cast<size_t>(sample_rate * seconds));
	float *data = buf.data(0);
	for (size_t i = 0; i < buf.sample_count(); i++) {
		data[i] = value;
	}
	return buf;
}

void write_partial_waveform(AudioWaveformCache *cache, int sample_rate)
{
	const AudioParams params = make_mono_params(sample_rate);
	cache->set_parameters(params);

	// Fill seconds [1,2) with a loud constant signal.
	AudioVisualWaveform waveform;
	waveform.set_channel_count(1);
	SampleBuffer buf = make_mono_buffer(sample_rate, 1.0f, 1);
	waveform.overwrite_samples(buf, sample_rate, Rational(1));

	// Tell the cache that only the middle second is valid in a 3-second clip.
	cache->write_waveform(TimeRange(1, 2), TimeRangeList({ TimeRange(1, 2) }),
						 &waveform);
}

} // namespace

TEST(TimelineWaveformSync, ExtractEnvelopeUsesOnlyValidatedRanges)
{
	constexpr int k_sample_rate = 48000;
	constexpr size_t k_window_samples = k_sample_rate / 20; // 50 ms windows

	AudioWaveformCache cache;
	write_partial_waveform(&cache, k_sample_rate);

	WaveformSyncClip clip;
	clip.waveform = &cache;
	clip.media_range = TimeRange(0, 3);
	clip.sample_rate = k_sample_rate;

	const QVector<double> envelope =
		timeline_waveform_sync::extract_waveform_cache_envelope(clip, k_sample_rate,
														   k_window_samples);

	// 3 seconds at 20 windows per second == 60 windows.
	EXPECT_EQ(envelope.size(), 60);

	// Windows before the validated region are silent placeholders
	for (int i = 0; i < 20; ++i) {
		EXPECT_DOUBLE_EQ(envelope.at(i), 0.0);
	}

	// The validated second was filled with a constant 1.0 signal, so every
	// window inside it must peak at exactly 1.0
	for (int i = 20; i < 40; ++i) {
		EXPECT_DOUBLE_EQ(envelope.at(i), 1.0);
	}

	// Windows after the validated region are silent placeholders too
	for (int i = 40; i < 60; ++i) {
		EXPECT_DOUBLE_EQ(envelope.at(i), 0.0);
	}
}

TEST(TimelineWaveformSync, ExtractEnvelopeReportsValidityMask)
{
	constexpr int k_sample_rate = 48000;
	constexpr size_t k_window_samples = k_sample_rate / 20; // 50 ms windows

	AudioWaveformCache cache;
	write_partial_waveform(&cache, k_sample_rate);

	WaveformSyncClip clip;
	clip.waveform = &cache;
	clip.media_range = TimeRange(0, 3);
	clip.sample_rate = k_sample_rate;

	QVector<bool> valid_mask;
	const QVector<double> envelope =
		timeline_waveform_sync::extract_waveform_cache_envelope(
			clip, k_sample_rate, k_window_samples, &valid_mask);

	// One flag per envelope window
	ASSERT_EQ(valid_mask.size(), envelope.size());

	// Windows outside the validated second are flagged invalid, windows
	// inside it are flagged valid
	EXPECT_FALSE(valid_mask.at(0));
	EXPECT_FALSE(valid_mask.at(59));
	for (int i = 20; i < 40; ++i) {
		EXPECT_TRUE(valid_mask.at(i));
	}
}

TEST(TimelineWaveformSync, PartialCacheIsConsideredReady)
{
	constexpr int k_sample_rate = 48000;

	Footage footage;
	footage.set_valid();

	AudioWaveformCache *cache = footage.waveform_cache();
	write_partial_waveform(cache, k_sample_rate);

	ClipBlock clip;
	clip.set_length_and_media_out(Rational(3));
	clip.set_media_in(Rational(0));

	Node::connect_edge(&footage, NodeInput(&clip, ClipBlock::k_buffer_in));

	WaveformSyncClip out;
	EXPECT_TRUE(timeline_waveform_sync::get_waveform_sync_clip(reinterpret_cast<OakEngineBlock *>(&clip), &out));
	EXPECT_EQ(out.waveform, cache);
	EXPECT_EQ(out.sample_rate, k_sample_rate);
	EXPECT_EQ(out.media_range, TimeRange(0, 3));
}

TEST(TimelineWaveformSync, EmptyCacheIsNotReady)
{
	Footage footage;
	footage.set_valid();

	AudioParams params = make_mono_params(48000);
	footage.waveform_cache()->set_parameters(params);

	ClipBlock clip;
	clip.set_length_and_media_out(Rational(3));
	clip.set_media_in(Rational(0));

	Node::connect_edge(&footage, NodeInput(&clip, ClipBlock::k_buffer_in));

	WaveformSyncClip out;
	EXPECT_FALSE(timeline_waveform_sync::get_waveform_sync_clip(reinterpret_cast<OakEngineBlock *>(&clip), &out));
}
