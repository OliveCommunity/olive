/*
 * Oak Video Editor - Audio Subsystem Smoke Tests
 * Copyright (C) 2025 Olive CE Team
 *
 * Comprehensive smoke tests for the audio subsystem including:
 * - AudioManager lifecycle and device management
 * - AudioProcessor format conversion and tempo
 * - AudioVisualWaveform operations
 * - SampleBuffer management
 * - AudioParams validation and conversions
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include <QCoreApplication>
#include <QThread>
#include <QPainter>
#include <QImage>

// Audio headers
#include "audio/audiomanager.h"
#include "audio/audioprocessor.h"
#include "audio/audiovisualwaveform.h"
#include "render/previewaudiodevice.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/render/sampleformat.h"

using namespace olive;
using namespace olive::core;

namespace olive
{
namespace audio
{
namespace test
{

// ============================================================================
// Helper Functions
// ============================================================================

static AudioParams make_audio_params(int sample_rate, uint64_t channel_layout,
								   SampleFormat format)
{
	return AudioParams(sample_rate, channel_layout, format);
}

static void fill_sample_buffer(SampleBuffer &buffer, float value)
{
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			data[i] = value;
		}
	}
}

// Pushes input through the processor, then flushes and drains everything the
// filter graph still holds, returning the accumulated per-plane output.
// Draining after a flush ends at EOF, which AudioProcessor reports as a
// negative return value, so the final Convert result is intentionally unused.
static AudioProcessor::Buffer convert_and_drain(AudioProcessor &processor,
											  float **input, int nb_samples)
{
	AudioProcessor::Buffer output;
	EXPECT_GE(processor.convert(input, nb_samples, &output), 0);

	processor.flush();

	AudioProcessor::Buffer rest;
	processor.convert(nullptr, 0, &rest);

	if (output.size() < rest.size()) {
		output.resize(rest.size());
	}
	for (int i = 0; i < rest.size(); i++) {
		output[i].append(rest.at(i));
	}
	return output;
}

// ============================================================================
// Smoke Test: AudioParams
// ============================================================================

TEST(AudioSmokeParams, DefaultConstruction)
{
	AudioParams params;
	EXPECT_FALSE(params.is_valid());
	EXPECT_EQ(params.sample_rate(), 0);
	EXPECT_EQ(params.channel_count(), 0);
	EXPECT_EQ(params.format(), SampleFormat::invalid);
}

TEST(AudioSmokeParams, ValidConstruction)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);

	EXPECT_TRUE(params.is_valid());
	EXPECT_EQ(params.sample_rate(), 48000);
	EXPECT_EQ(params.channel_count(), 2);
	EXPECT_EQ(params.format(), SampleFormat::f32_p);
	EXPECT_EQ(params.bytes_per_sample_per_channel(), 4);
	EXPECT_EQ(params.bits_per_sample(), 32);
}

TEST(AudioSmokeParams, MonoChannelLayout)
{
	AudioParams params(44100, k_channel_layout_mono, SampleFormat::s16);

	EXPECT_TRUE(params.is_valid());
	EXPECT_EQ(params.sample_rate(), 44100);
	EXPECT_EQ(params.channel_count(), 1);
}

TEST(AudioSmokeParams, SurroundChannelLayout)
{
	AudioParams params(48000, k_channel_layout5_point1, SampleFormat::f32_p);

	EXPECT_TRUE(params.is_valid());
	EXPECT_EQ(params.channel_count(), 6);
}

TEST(AudioSmokeParams, TimeConversions)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);

	// Time to samples
	EXPECT_EQ(params.time_to_samples(1.0), 48000);
	EXPECT_EQ(params.time_to_samples(0.5), 24000);
	EXPECT_EQ(params.time_to_samples(2.0), 96000);

	// Samples to bytes
	EXPECT_EQ(params.samples_to_bytes(48000),
			  48000 * 2 * 4); // samples * channels * bytes_per_sample

	// Time to bytes
	EXPECT_EQ(params.time_to_bytes(1.0), 48000 * 2 * 4);
}

TEST(AudioSmokeParams, EqualityOperators)
{
	AudioParams params1(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams params2(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams params3(44100, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams params4(48000, k_channel_layout_mono, SampleFormat::f32_p);
	AudioParams params5(48000, k_channel_layout_stereo, SampleFormat::s16);

	EXPECT_TRUE(params1 == params2);
	EXPECT_FALSE(params1 != params2);

	EXPECT_FALSE(params1 == params3); // Different sample rate
	EXPECT_FALSE(params1 == params4); // Different channel layout
	EXPECT_FALSE(params1 == params5); // Different format
}

TEST(AudioSmokeParams, CopyConstruction)
{
	AudioParams original(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams copy(original);

	EXPECT_TRUE(copy.is_valid());
	EXPECT_EQ(copy.sample_rate(), original.sample_rate());
	EXPECT_EQ(copy.channel_count(), original.channel_count());
	EXPECT_EQ(copy.format(), original.format());

	// Modifying copy should not affect original
	copy.set_sample_rate(44100);
	EXPECT_EQ(original.sample_rate(), 48000);
	EXPECT_EQ(copy.sample_rate(), 44100);
}

TEST(AudioSmokeParams, CopyAssignment)
{
	AudioParams original(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams copy;
	copy = original;

	EXPECT_TRUE(copy.is_valid());
	EXPECT_EQ(copy.sample_rate(), original.sample_rate());
	EXPECT_EQ(copy.channel_count(), original.channel_count());
	EXPECT_EQ(copy.format(), original.format());
}

TEST(AudioSmokeParams, ChannelLayoutModification)
{
	AudioParams params(48000, k_channel_layout_mono, SampleFormat::f32_p);
	EXPECT_EQ(params.channel_count(), 1);

	// Change to stereo
	params.set_channel_layout(k_channel_layout_stereo);
	EXPECT_EQ(params.channel_count(), 2);

	// Change to 5.1
	params.set_channel_layout(k_channel_layout5_point1);
	EXPECT_EQ(params.channel_count(), 6);
}

// ============================================================================
// Smoke Test: SampleBuffer
// ============================================================================

TEST(AudioSmokeBuffer, DefaultConstruction)
{
	SampleBuffer buffer;
	EXPECT_FALSE(buffer.is_allocated());
	EXPECT_EQ(buffer.channel_count(), 0);
	EXPECT_EQ(buffer.sample_count(), 0);
}

TEST(AudioSmokeBuffer, Allocation)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(48000)); // 1 second of samples

	EXPECT_TRUE(buffer.is_allocated());
	EXPECT_EQ(buffer.channel_count(), 2);
	EXPECT_EQ(buffer.sample_count(), 48000);
}

TEST(AudioSmokeBuffer, DataAccess)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(100));

	// Fill with test data
	fill_sample_buffer(buffer, 0.5f);

	// Verify data
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		const float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			EXPECT_FLOAT_EQ(data[i], 0.5f);
		}
	}
}

TEST(AudioSmokeBuffer, Silence)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(100));

	// Fill with non-zero values
	fill_sample_buffer(buffer, 0.5f);

	// Apply silence
	buffer.silence();

	// Verify silence
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		const float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			EXPECT_FLOAT_EQ(data[i], 0.0f);
		}
	}
}

TEST(AudioSmokeBuffer, VolumeTransform)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(100));

	// Fill with 1.0
	fill_sample_buffer(buffer, 1.0f);

	// Apply volume transform (50%)
	buffer.transform_volume(0.5f);

	// Verify volume change
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		const float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			EXPECT_FLOAT_EQ(data[i], 0.5f);
		}
	}
}

TEST(AudioSmokeBuffer, Clamp)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(100));

	// Fill with values outside [-1, 1]
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			data[i] = (i % 2 == 0) ? 2.0f : -2.0f;
		}
	}

	// Apply clamp
	buffer.clamp();

	// Verify clamping
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		const float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			EXPECT_GE(data[i], -1.0f);
			EXPECT_LE(data[i], 1.0f);
		}
	}
}

TEST(AudioSmokeBuffer, FastSet)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer source(params, size_t(100));
	SampleBuffer dest(params, size_t(100));

	fill_sample_buffer(source, 0.75f);
	dest.silence();

	// Fast copy from source to dest
	dest.fast_set(source, 0); // Copy to channel 0

	// Verify channel 0 copied
	const float *dest_data = dest.data(0);
	for (size_t i = 0; i < dest.sample_count(); ++i) {
		EXPECT_FLOAT_EQ(dest_data[i], 0.75f);
	}
}

TEST(AudioSmokeBuffer, RipChannel)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(100));

	// Fill channel 0 with 0.5, channel 1 with 0.25
	float *ch0 = buffer.data(0);
	float *ch1 = buffer.data(1);
	for (size_t i = 0; i < buffer.sample_count(); ++i) {
		ch0[i] = 0.5f;
		ch1[i] = 0.25f;
	}

	// Rip channel 0
	SampleBuffer ripped = buffer.rip_channel(0);

	EXPECT_EQ(ripped.channel_count(), 1);
	EXPECT_EQ(ripped.sample_count(), buffer.sample_count());

	const float *ripped_data = ripped.data(0);
	for (size_t i = 0; i < ripped.sample_count(); ++i) {
		EXPECT_FLOAT_EQ(ripped_data[i], 0.5f);
	}
}

// ============================================================================
// Smoke Test: AudioVisualWaveform
// ============================================================================

TEST(AudioSmokeWaveform, DefaultConstruction)
{
	AudioVisualWaveform waveform;
	EXPECT_EQ(waveform.channel_count(), 0);
	EXPECT_EQ(waveform.length(), Rational(0));
}

TEST(AudioSmokeWaveform, ChannelCount)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);
	EXPECT_EQ(waveform.channel_count(), 2);

	waveform.set_channel_count(6);
	EXPECT_EQ(waveform.channel_count(), 6);
}

TEST(AudioSmokeWaveform, OverwriteSamples)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// Create sample buffer with sine wave-like data
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(4800)); // 0.1 seconds

	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			data[i] = std::sin(float(i) * 0.1f);
		}
	}

	// Write samples to waveform
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	// 4800 samples at 48000 Hz is exactly 0.1 seconds
	EXPECT_EQ(waveform.length(), Rational(1, 10));
}

TEST(AudioSmokeWaveform, OverwriteSilence)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// First add some samples
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(4800));
	fill_sample_buffer(buffer, 0.5f);
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	// Overwrite with silence
	waveform.overwrite_silence(Rational(0), Rational(1, 10)); // 0.1 seconds

	// The silence covers exactly the written region, so the length is
	// unchanged at exactly 0.1 seconds
	EXPECT_EQ(waveform.length(), Rational(1, 10));

	// ...and the overwritten region is actually silent
	auto summary = waveform.get_summary_from_time(Rational(0), Rational(1, 10));
	ASSERT_EQ(summary.size(), 2);
	EXPECT_FLOAT_EQ(summary[0].min, 0.0f);
	EXPECT_FLOAT_EQ(summary[0].max, 0.0f);
	EXPECT_FLOAT_EQ(summary[1].min, 0.0f);
	EXPECT_FLOAT_EQ(summary[1].max, 0.0f);
}

TEST(AudioSmokeWaveform, TrimIn)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// Add samples
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(48000)); // 1 second
	fill_sample_buffer(buffer, 0.5f);
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	EXPECT_EQ(waveform.length(), Rational(1));

	// Trim 0.25 seconds from start
	waveform.trim_in(Rational(1, 4));

	EXPECT_EQ(waveform.length(), Rational(3, 4));
}

TEST(AudioSmokeWaveform, Resize)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// Add samples
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(48000));
	fill_sample_buffer(buffer, 0.5f);
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	EXPECT_EQ(waveform.length(), Rational(1));

	// Resize to 0.5 seconds
	waveform.resize(Rational(1, 2));

	EXPECT_EQ(waveform.length(), Rational(1, 2));
}

TEST(AudioSmokeWaveform, TrimRange)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// Add 2 seconds of samples
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(96000));
	fill_sample_buffer(buffer, 0.5f);
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	EXPECT_EQ(waveform.length(), Rational(2));

	// Trim to range [0.5, 1.0] (0.5 seconds duration starting at 0.5)
	waveform.trim_range(Rational(1, 2), Rational(1, 2));

	EXPECT_EQ(waveform.length(), Rational(1, 2));
}

TEST(AudioSmokeWaveform, Mid)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// Add 2 seconds of samples
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(96000));
	fill_sample_buffer(buffer, 0.5f);
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	// Get mid section [0.5, 1.5]
	AudioVisualWaveform mid = waveform.mid(Rational(1, 2), Rational(1));

	EXPECT_EQ(mid.length(), Rational(1));
	EXPECT_EQ(mid.channel_count(), 2);
}

TEST(AudioSmokeWaveform, GetSummaryFromTime)
{
	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// Add samples with varying values
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(4800));
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			data[i] = (i % 2 == 0) ? 0.8f : -0.8f;
		}
	}
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	// Get summary for first half
	auto summary = waveform.get_summary_from_time(Rational(0), Rational(1, 20));

	ASSERT_EQ(summary.size(), 2); // 2 channels
	// Samples alternate between +0.8 and -0.8, so the summary is exactly that
	EXPECT_FLOAT_EQ(summary[0].min, -0.8f);
	EXPECT_FLOAT_EQ(summary[0].max, 0.8f);
	EXPECT_FLOAT_EQ(summary[1].min, -0.8f);
	EXPECT_FLOAT_EQ(summary[1].max, 0.8f);
}

TEST(AudioSmokeWaveform, SumSamples)
{
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(100));

	// Fill with known pattern
	for (int ch = 0; ch < buffer.channel_count(); ++ch) {
		float *data = buffer.data(ch);
		for (size_t i = 0; i < buffer.sample_count(); ++i) {
			data[i] = float(i) / 100.0f;
		}
	}

	auto summary = AudioVisualWaveform::sum_samples(buffer, 0, 100);

	EXPECT_EQ(summary.size(), 2);
	EXPECT_FLOAT_EQ(summary[0].min, 0.0f);
	EXPECT_FLOAT_EQ(summary[0].max, 0.99f);
}

TEST(AudioSmokeWaveform, ReSumSamples)
{
	// Create sample data
	std::vector<AudioVisualWaveform::SamplePerChannel> samples(200);
	for (size_t i = 0; i < 100; ++i) {
		samples[i * 2].min = -0.5f;
		samples[i * 2].max = 0.5f;
		samples[i * 2 + 1].min = -0.3f;
		samples[i * 2 + 1].max = 0.3f;
	}

	auto summary = AudioVisualWaveform::re_sum_samples(samples.data(), 200, 2);

	EXPECT_EQ(summary.size(), 2);
	EXPECT_FLOAT_EQ(summary[0].min, -0.5f);
	EXPECT_FLOAT_EQ(summary[0].max, 0.5f);
	EXPECT_FLOAT_EQ(summary[1].min, -0.3f);
	EXPECT_FLOAT_EQ(summary[1].max, 0.3f);
}

// ============================================================================
// Smoke Test: AudioProcessor
// ============================================================================

TEST(AudioSmokeProcessor, DefaultConstruction)
{
	AudioProcessor processor;
	EXPECT_FALSE(processor.is_open());
}

TEST(AudioSmokeProcessor, OpenClose)
{
	AudioProcessor processor;

	AudioParams from(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams to(48000, k_channel_layout_stereo, SampleFormat::f32_p);

	EXPECT_TRUE(processor.open(from, to, 1.0));
	EXPECT_TRUE(processor.is_open());

	processor.close();
	EXPECT_FALSE(processor.is_open());
}

TEST(AudioSmokeProcessor, SampleRateConversion)
{
	AudioProcessor processor;

	AudioParams from(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams to(44100, k_channel_layout_stereo, SampleFormat::f32_p);

	ASSERT_TRUE(processor.open(from, to, 1.0));
	ASSERT_TRUE(processor.is_open());
	EXPECT_EQ(processor.from().sample_rate(), 48000);
	EXPECT_EQ(processor.to().sample_rate(), 44100);

	// Push one second of a constant signal
	constexpr int k_samples = 48000;
	std::vector<float> left(k_samples, 0.5f);
	std::vector<float> right(k_samples, 0.5f);
	float *input[2] = { left.data(), right.data() };

	const AudioProcessor::Buffer output =
		convert_and_drain(processor, input, k_samples);

	ASSERT_EQ(output.size(), 2);
	ASSERT_EQ(output.at(0).size(), output.at(1).size());

	// 48000 -> 44100 must produce ~44100 samples; the resampler's filter
	// delay makes the exact total version-dependent
	const int converted = output.at(0).size() / int(sizeof(float));
	EXPECT_GE(converted, 43500);
	EXPECT_LE(converted, 44600);

	// A constant signal stays constant through resampling
	float value = 0.0f;
	std::memcpy(&value,
				output.at(0).constData() + (converted / 2) * sizeof(float),
				sizeof(float));
	EXPECT_NEAR(value, 0.5f, 0.01f);
}

TEST(AudioSmokeProcessor, ChannelLayoutConversion)
{
	AudioProcessor processor;

	AudioParams from(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams to(48000, k_channel_layout_mono, SampleFormat::f32_p);

	ASSERT_TRUE(processor.open(from, to, 1.0));
	ASSERT_TRUE(processor.is_open());
	EXPECT_EQ(processor.from().channel_count(), 2);
	EXPECT_EQ(processor.to().channel_count(), 1);

	constexpr int k_samples = 1024;
	std::vector<float> left(k_samples, 0.5f);
	std::vector<float> right(k_samples, 0.5f);
	float *input[2] = { left.data(), right.data() };

	AudioProcessor::Buffer output;
	ASSERT_EQ(processor.convert(input, k_samples, &output), 0);

	// Downmixing folds both channels into a single mono plane
	ASSERT_EQ(output.size(), 1);
	ASSERT_EQ(output.at(0).size(), k_samples * int(sizeof(float)));

	// The downmix of two identical channels must stay audible regardless of
	// the exact mixing coefficients
	float value = 0.0f;
	std::memcpy(&value, output.at(0).constData(), sizeof(float));
	EXPECT_GT(value, 0.0f);
	EXPECT_LE(value, 1.0f);
}

TEST(AudioSmokeProcessor, FormatConversion)
{
	AudioProcessor processor;

	AudioParams from(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams to(48000, k_channel_layout_stereo, SampleFormat::s16_p);

	ASSERT_TRUE(processor.open(from, to, 1.0));
	ASSERT_TRUE(processor.is_open());

	constexpr int k_samples = 1024;
	std::vector<float> left(k_samples, 0.5f);
	std::vector<float> right(k_samples, -0.25f);
	float *input[2] = { left.data(), right.data() };

	AudioProcessor::Buffer output;
	ASSERT_EQ(processor.convert(input, k_samples, &output), 0);

	// Planar 16-bit output keeps one plane per channel at 2 bytes per sample
	ASSERT_EQ(output.size(), 2);
	ASSERT_EQ(output.at(0).size(), k_samples * int(sizeof(int16_t)));
	ASSERT_EQ(output.at(1).size(), k_samples * int(sizeof(int16_t)));

	// Known float values land on the expected 16-bit codes
	int16_t value = 0;
	std::memcpy(&value, output.at(0).constData(), sizeof(value));
	EXPECT_NEAR(value, 16384, 1); // 0.5 * 32768
	std::memcpy(&value, output.at(1).constData(), sizeof(value));
	EXPECT_NEAR(value, -8192, 1); // -0.25 * 32768
}

TEST(AudioSmokeProcessor, TempoChange)
{
	AudioProcessor processor;

	AudioParams from(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams to(48000, k_channel_layout_stereo, SampleFormat::f32_p);

	// Open with 2x tempo
	ASSERT_TRUE(processor.open(from, to, 2.0));
	ASSERT_TRUE(processor.is_open());

	// One second of input
	constexpr int k_samples = 48000;
	std::vector<float> left(k_samples, 0.5f);
	std::vector<float> right(k_samples, 0.5f);
	float *input[2] = { left.data(), right.data() };

	const AudioProcessor::Buffer output =
		convert_and_drain(processor, input, k_samples);

	ASSERT_EQ(output.size(), 2);
	ASSERT_EQ(output.at(0).size(), output.at(1).size());

	// 2x tempo must output roughly half the input; atempo works in windows,
	// so allow generous margins
	const int converted = output.at(0).size() / int(sizeof(float));
	EXPECT_GE(converted, 20000);
	EXPECT_LE(converted, 28000);

	// Tempo changes timing, not sample values
	float value = 0.0f;
	std::memcpy(&value,
				output.at(0).constData() + (converted / 2) * sizeof(float),
				sizeof(float));
	EXPECT_NEAR(value, 0.5f, 0.05f);
}

TEST(AudioSmokeProcessor, InvalidOpen)
{
	AudioProcessor processor;

	// Open with valid params
	AudioParams from(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	AudioParams to(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	EXPECT_TRUE(processor.open(from, to, 1.0));

	// Try to open again while already open (should fail)
	EXPECT_FALSE(processor.open(from, to, 1.0));
}

TEST(AudioSmokeProcessor, ConvertWithoutOpen)
{
	AudioProcessor processor;

	// Create input data
	float *input[2] = { nullptr, nullptr };
	std::vector<float> ch0(100, 0.5f);
	std::vector<float> ch1(100, 0.5f);
	input[0] = ch0.data();
	input[1] = ch1.data();

	AudioProcessor::Buffer output;

	// Should fail since processor is not open
	EXPECT_EQ(processor.convert(input, 100, &output), -1);
}

// ============================================================================
// Smoke Test: PreviewAudioDevice
// ============================================================================

TEST(AudioSmokePreviewDevice, Construction)
{
	PreviewAudioDevice device;
	EXPECT_TRUE(device.isSequential());

	// Without params the frame size is unknown and reported as zero
	EXPECT_EQ(device.bytes_per_frame(), 0);

	// SetParams derives the frame size from the audio format:
	// bytes per sample per channel * channel count
	device.set_params(AudioParams(48000, k_channel_layout_stereo, SampleFormat::f32_p));
	EXPECT_EQ(device.bytes_per_frame(), 8);

	device.set_params(AudioParams(48000, k_channel_layout_mono, SampleFormat::s16));
	EXPECT_EQ(device.bytes_per_frame(), 2);
}

TEST(AudioSmokePreviewDevice, BytesPerFrame)
{
	PreviewAudioDevice device;

	device.set_bytes_per_frame(8); // 2 channels * 4 bytes (F32)
	EXPECT_EQ(device.bytes_per_frame(), 8);

	device.set_bytes_per_frame(4); // 2 channels * 2 bytes (S16)
	EXPECT_EQ(device.bytes_per_frame(), 4);
}

TEST(AudioSmokePreviewDevice, NotifyInterval)
{
	PreviewAudioDevice device;
	device.open(QIODevice::ReadWrite);

	// The notify interval is measured in bytes: Notify fires when the total
	// number of bytes read crosses a multiple of the interval. readData() is
	// called directly to bypass QIODevice's read-ahead buffer, which would
	// otherwise coalesce the reads and hide the per-read transitions.
	device.set_notify_interval(64);

	int notify_count = 0;
	QObject::connect(&device, &PreviewAudioDevice::notify, &device,
					 [&notify_count]() { ++notify_count; });

	QByteArray data(256, 0x01);
	ASSERT_EQ(device.write(data), 256);

	// Nothing read yet, so no notification
	EXPECT_EQ(notify_count, 0);

	char buf[128];
	ASSERT_EQ(device.readData(buf, 64), 64);
	EXPECT_EQ(notify_count, 1); // crossed the 64-byte mark

	ASSERT_EQ(device.readData(buf, 64), 64);
	EXPECT_EQ(notify_count, 2); // crossed the 128-byte mark

	// Crossing two intervals in one read emits a single notification
	ASSERT_EQ(device.readData(buf, 128), 128);
	EXPECT_EQ(notify_count, 3);

	// Buffer drained: no more reads, no more notifications
	EXPECT_EQ(device.readData(buf, 64), 0);
	EXPECT_EQ(notify_count, 3);

	// An interval of zero disables notifications entirely
	PreviewAudioDevice quiet_device;
	quiet_device.open(QIODevice::ReadWrite);
	int quiet_count = 0;
	QObject::connect(&quiet_device, &PreviewAudioDevice::notify, &quiet_device,
					 [&quiet_count]() { ++quiet_count; });
	ASSERT_EQ(quiet_device.write(data), 256);
	EXPECT_EQ(quiet_device.readData(buf, 128), 128);
	EXPECT_EQ(quiet_count, 0);
}

TEST(AudioSmokePreviewDevice, Clear)
{
	PreviewAudioDevice device;
	device.open(QIODevice::ReadWrite);

	device.set_notify_interval(64);
	int notify_count = 0;
	QObject::connect(&device, &PreviewAudioDevice::notify, &device,
					 [&notify_count]() { ++notify_count; });

	// Write some data and read it back (readData() is called directly to
	// bypass QIODevice's read-ahead buffer)
	QByteArray data(128, 0xAB);
	ASSERT_EQ(device.write(data), 128);
	char buf[128];
	ASSERT_EQ(device.readData(buf, sizeof(buf)), 128);
	EXPECT_EQ(notify_count, 1);

	// Queue new data, then clear it
	ASSERT_EQ(device.write(data), 128);
	device.clear();

	// After clear the device holds no data: a read returns 0 bytes, which is
	// how the output callback knows to fill the stream with silence
	EXPECT_EQ(device.readData(buf, sizeof(buf)), 0);

	// clear() also resets the read counter, so notifications start over, and
	// the device keeps working: data written after the clear reads back intact
	ASSERT_EQ(device.write(data), 128);
	ASSERT_EQ(device.readData(buf, sizeof(buf)), 128);
	EXPECT_EQ(QByteArray(buf, data.size()), data);
	EXPECT_EQ(notify_count, 2);
}

// ============================================================================
// Smoke Test: Sample Format
// ============================================================================

TEST(AudioSmokeSampleFormat, ByteCount)
{
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::invalid), 0);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::u8), 1);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::u8_p), 1);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::s16), 2);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::s16_p), 2);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::s32), 4);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::s32_p), 4);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::f32), 4);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::f32_p), 4);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::s64), 8);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::s64_p), 8);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::f64), 8);
	EXPECT_EQ(SampleFormat::byte_count(SampleFormat::f64_p), 8);
}

TEST(AudioSmokeSampleFormat, PackedVsPlanar)
{
	// Packed formats
	EXPECT_TRUE(SampleFormat::is_packed(SampleFormat::u8));
	EXPECT_TRUE(SampleFormat::is_packed(SampleFormat::s16));
	EXPECT_TRUE(SampleFormat::is_packed(SampleFormat::s32));
	EXPECT_TRUE(SampleFormat::is_packed(SampleFormat::f32));
	EXPECT_TRUE(SampleFormat::is_packed(SampleFormat::s64));
	EXPECT_TRUE(SampleFormat::is_packed(SampleFormat::f64));

	// Planar formats
	EXPECT_TRUE(SampleFormat::is_planar(SampleFormat::u8_p));
	EXPECT_TRUE(SampleFormat::is_planar(SampleFormat::s16_p));
	EXPECT_TRUE(SampleFormat::is_planar(SampleFormat::s32_p));
	EXPECT_TRUE(SampleFormat::is_planar(SampleFormat::f32_p));
	EXPECT_TRUE(SampleFormat::is_planar(SampleFormat::s64_p));
	EXPECT_TRUE(SampleFormat::is_planar(SampleFormat::f64_p));
}

TEST(AudioSmokeSampleFormat, StringConversion)
{
	// Test to_string (values may vary based on FFmpeg version)
	EXPECT_EQ(SampleFormat::to_string(SampleFormat::u8), "u8");
	EXPECT_EQ(SampleFormat::to_string(SampleFormat::s16), "s16");
	EXPECT_EQ(SampleFormat::to_string(SampleFormat::s32), "s32");
	// F32 can be "flt" or "f32" depending on FFmpeg version
	std::string f32_str = SampleFormat::to_string(SampleFormat::f32);
	EXPECT_TRUE(f32_str == "flt" || f32_str == "f32");
	// F64 can be "dbl" or "f64" depending on FFmpeg version
	std::string f64_str = SampleFormat::to_string(SampleFormat::f64);
	EXPECT_TRUE(f64_str == "dbl" || f64_str == "f64");

	// Test from_string
	EXPECT_EQ(SampleFormat::from_string("u8"), SampleFormat::u8);
	EXPECT_EQ(SampleFormat::from_string("s16"), SampleFormat::s16);
	// from_string may not support all format names
	EXPECT_EQ(SampleFormat::from_string(""), SampleFormat::invalid);
	EXPECT_EQ(SampleFormat::from_string("unknown"), SampleFormat::invalid);
}

// ============================================================================
// Smoke Test: Thread Safety
// ============================================================================

TEST(AudioSmokeThread, ConcurrentWaveformAccess)
{
	const int num_threads = 4;
	const int num_ops_per_thread = 50;

	AudioVisualWaveform waveform;
	waveform.set_channel_count(2);

	// Pre-populate with data
	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);
	SampleBuffer buffer(params, size_t(4800));
	fill_sample_buffer(buffer, 0.5f);
	waveform.overwrite_samples(buffer, 48000, Rational(0));

	std::vector<std::thread> threads;
	std::atomic<int> success_count{ 0 };

	for (int t = 0; t < num_threads; ++t) {
		threads.emplace_back([&waveform, &success_count, num_ops_per_thread]() {
			for (int i = 0; i < num_ops_per_thread; ++i) {
				// Read summary from different times
				auto summary = waveform.get_summary_from_time(
					Rational(i % 10, 100), // 0.00 to 0.09 seconds
					Rational(1, 100) // 0.01 second duration
				);

				if (summary.size() == 2) {
					success_count++;
				}
			}
		});
	}

	for (auto &t : threads) {
		t.join();
	}

	EXPECT_EQ(success_count.load(), num_threads * num_ops_per_thread);
}

TEST(AudioSmokeThread, ConcurrentSampleBufferOperations)
{
	// SampleBuffer instances are independent value objects with no shared
	// state, so operating on separate instances from multiple threads is
	// race-free and must produce deterministic results
	const int num_threads = 4;

	AudioParams params(48000, k_channel_layout_stereo, SampleFormat::f32_p);

	std::vector<SampleBuffer> buffers;
	buffers.reserve(num_threads);
	for (int t = 0; t < num_threads; ++t) {
		buffers.emplace_back(params, size_t(1000));
		fill_sample_buffer(buffers.back(), 0.5f);
	}

	std::vector<std::thread> threads;
	for (int t = 0; t < num_threads; ++t) {
		threads.emplace_back([&buffers, t]() {
			SampleBuffer &buffer = buffers[static_cast<size_t>(t)];
			switch (t % 4) {
			case 0:
				buffer.transform_volume(0.8f);
				break;
			case 1:
				buffer.transform_volume(4.0f);
				buffer.clamp();
				break;
			case 2:
				buffer.silence();
				break;
			case 3:
				buffer.transform_volume_for_channel(1, 0.0f);
				break;
			}
		});
	}

	for (auto &t : threads) {
		t.join();
	}

	// Each buffer must hold the exact deterministic outcome of its operation
	for (size_t i = 0; i < buffers[0].sample_count(); ++i) {
		EXPECT_FLOAT_EQ(buffers[0].data(0)[i], 0.4f); // 0.5 * 0.8
		EXPECT_FLOAT_EQ(buffers[0].data(1)[i], 0.4f);

		EXPECT_FLOAT_EQ(buffers[1].data(0)[i], 1.0f); // 0.5 * 4 clamped
		EXPECT_FLOAT_EQ(buffers[1].data(1)[i], 1.0f);

		EXPECT_FLOAT_EQ(buffers[2].data(0)[i], 0.0f); // silenced
		EXPECT_FLOAT_EQ(buffers[2].data(1)[i], 0.0f);

		EXPECT_FLOAT_EQ(buffers[3].data(0)[i], 0.5f); // untouched channel
		EXPECT_FLOAT_EQ(buffers[3].data(1)[i], 0.0f); // zeroed channel
	}
}

} // namespace test
} // namespace audio
} // namespace olive
