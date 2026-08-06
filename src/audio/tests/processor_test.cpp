/***

  Oak Video Editor - Non-Linear Video Editor
  Copyright (C) 2026 Oak Team

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

***/

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "audio/manager.h"
#include "audio/processor.h"

namespace
{

constexpr int kSampleFmtF32P = 4; // olive::core::SampleFormat::f32_p
constexpr uint64_t kLayoutStereo = 0x3;

struct ProcessorHandle {
	OakAudioProcessor h = oakaudio_processor_init();
	~ProcessorHandle() { oakaudio_processor_free(&h); }
};

// Feed a full buffer through the processor and return the total number of
// output frames produced (input drained + flushed).
int convert_all(OakAudioProcessor p, const std::vector<std::vector<float>> &in,
				int chunk)
{
	const int channels = int(in.size());
	const size_t nch = size_t(channels);
	std::vector<const float *> in_planes(nch);
	std::vector<std::vector<float>> out_store(nch);
	std::vector<float *> out_planes(nch);
	for (int ch = 0; ch < channels; ch++) {
		in_planes[size_t(ch)] = in[size_t(ch)].data();
		out_store[size_t(ch)].resize(size_t(chunk) * 4 + 4096);
		out_planes[size_t(ch)] = out_store[size_t(ch)].data();
	}

	int total = 0;
	const int frames = int(in[0].size());
	for (int pos = 0; pos < frames; pos += chunk) {
		const int n = std::min(chunk, frames - pos);
		std::vector<const float *> window(nch);
		for (int ch = 0; ch < channels; ch++) {
			window[size_t(ch)] = in[size_t(ch)].data() + pos;
		}
		const int produced = oakaudio_processor_convert(
			p, window.data(), n, out_planes.data(), int(out_store[0].size()));
		if (produced < 0) {
			return produced;
		}
		total += produced;
	}

	EXPECT_EQ(oakaudio_processor_flush(p), OAKAUDIO_OK);
	// Drain the resampler's internal delay
	for (int guard = 0; guard < 64; guard++) {
		const int produced = oakaudio_processor_convert(
			p, nullptr, 0, out_planes.data(), int(out_store[0].size()));
		if (produced <= 0) {
			break;
		}
		total += produced;
	}
	return total;
}

std::vector<std::vector<float>> make_sine(int channels, int frames, int rate)
{
	const size_t nch = size_t(channels);
	std::vector<std::vector<float>> data(nch);
	for (int ch = 0; ch < channels; ch++) {
		data[size_t(ch)].resize(size_t(frames));
		for (int i = 0; i < frames; i++) {
			data[size_t(ch)][size_t(i)] =
				0.5f * std::sin(2.0 * M_PI * 440.0 * i / rate);
		}
	}
	return data;
}

} // namespace

TEST(OakAudioProcessor, InitFree)
{
	const int before = oakaudio_debug_alive_count();
	{
		ProcessorHandle p;
		ASSERT_NE(p.h.ctx, nullptr);
		EXPECT_EQ(oakaudio_debug_alive_count(), before + 1);
	}
	EXPECT_EQ(oakaudio_debug_alive_count(), before);

	// free is a no-op on NULL / empty handles
	oakaudio_processor_free(nullptr);
	OakAudioProcessor empty = {};
	oakaudio_processor_free(&empty);
}

TEST(OakAudioProcessor, OpenCloseIsOpen)
{
	ProcessorHandle p;
	EXPECT_EQ(oakaudio_processor_is_open(p.h), 0);

	EXPECT_EQ(oakaudio_processor_open(p.h, 44100, kLayoutStereo, kSampleFmtF32P,
									48000, kLayoutStereo, kSampleFmtF32P, 1.0),
			  OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_processor_is_open(p.h), 1);

	// Error path: opening an open processor
	EXPECT_EQ(oakaudio_processor_open(p.h, 44100, kLayoutStereo, kSampleFmtF32P,
									48000, kLayoutStereo, kSampleFmtF32P, 1.0),
			  OAKAUDIO_E_STATE);

	EXPECT_EQ(oakaudio_processor_close(p.h), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_processor_is_open(p.h), 0);
}

TEST(OakAudioProcessor, OpenInvalidArgs)
{
	ProcessorHandle p;
	// Unsupported output format (only f32p is delivered)
	EXPECT_EQ(oakaudio_processor_open(p.h, 44100, kLayoutStereo, kSampleFmtF32P,
									48000, kLayoutStereo, 1, 1.0),
			  OAKAUDIO_E_INVALID);
	// Bad sample rate
	EXPECT_EQ(oakaudio_processor_open(p.h, 0, kLayoutStereo, kSampleFmtF32P,
									48000, kLayoutStereo, kSampleFmtF32P, 1.0),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_processor_is_open(p.h), 0);

	// Empty handle
	OakAudioProcessor empty = {};
	EXPECT_EQ(oakaudio_processor_open(empty, 44100, kLayoutStereo,
									kSampleFmtF32P, 48000, kLayoutStereo,
									kSampleFmtF32P, 1.0),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_processor_is_open(empty), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_processor_close(empty), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_processor_flush(empty), OAKAUDIO_E_INVALID);
}

TEST(OakAudioProcessor, ConvertResample441To48)
{
	const int before = oakaudio_debug_alive_count();
	ProcessorHandle p;
	ASSERT_EQ(oakaudio_processor_open(p.h, 44100, kLayoutStereo,
									kSampleFmtF32P, 48000, kLayoutStereo,
									kSampleFmtF32P, 1.0),
			  OAKAUDIO_OK);

	const int in_frames = 44100; // one second
	const auto sine = make_sine(2, in_frames, 44100);
	const int produced = convert_all(p.h, sine, 4096);
	ASSERT_GE(produced, 0);

	// One second at 44.1k must become (within resampler tolerance) one
	// second at 48k.
	EXPECT_NEAR(produced, 48000, 200);

	// Re-open check for leaks
	oakaudio_processor_close(p.h);
	oakaudio_processor_free(&p.h);
	EXPECT_EQ(oakaudio_debug_alive_count(), before);
}

TEST(OakAudioProcessor, ConvertSilenceStaysSilent)
{
	ProcessorHandle p;
	ASSERT_EQ(oakaudio_processor_open(p.h, 48000, kLayoutStereo,
									kSampleFmtF32P, 48000, kLayoutStereo,
									kSampleFmtF32P, 1.0),
			  OAKAUDIO_OK);

	std::vector<std::vector<float>> silence(2, std::vector<float>(4096, 0.0f));
	std::vector<std::vector<float>> out(2, std::vector<float>(8192, -1.0f));
	std::vector<const float *> in_planes = { silence[0].data(),
										 silence[1].data() };
	std::vector<float *> out_planes = { out[0].data(), out[1].data() };

	const int produced = oakaudio_processor_convert(
		p.h, in_planes.data(), 4096, out_planes.data(), 8192);
	ASSERT_GT(produced, 0);
	EXPECT_EQ(produced, 4096); // same rate in/out: 1:1 frames
	for (int i = 0; i < produced; i++) {
		EXPECT_FLOAT_EQ(out[0][size_t(i)], 0.0f);
		EXPECT_FLOAT_EQ(out[1][size_t(i)], 0.0f);
	}
}

TEST(OakAudioProcessor, ConvertTempo)
{
	ProcessorHandle p;
	ASSERT_EQ(oakaudio_processor_open(p.h, 48000, kLayoutStereo,
									kSampleFmtF32P, 48000, kLayoutStereo,
									kSampleFmtF32P, 1.5),
			  OAKAUDIO_OK);

	const auto sine = make_sine(2, 48000, 48000);
	const int produced = convert_all(p.h, sine, 4096);
	ASSERT_GE(produced, 0);

	// 1.5x tempo: one second of input becomes roughly 2/3 second of
	// output (atempo works on correlated windows, so allow slack)
	EXPECT_NEAR(produced, int(48000 / 1.5), 3000);
}

TEST(OakAudioProcessor, ConvertErrorPaths)
{
	ProcessorHandle p;

	// Convert on a closed processor
	float dummy = 0.0f;
	float *out_planes[1] = { &dummy };
	const float *in_planes[1] = { &dummy };
	EXPECT_EQ(oakaudio_processor_convert(p.h, in_planes, 1, out_planes, 1),
			  OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_processor_flush(p.h), OAKAUDIO_E_STATE);

	// Empty handle
	OakAudioProcessor empty = {};
	EXPECT_EQ(oakaudio_processor_convert(empty, in_planes, 1, out_planes, 1),
			  OAKAUDIO_E_INVALID);

	// NULL input planes with frames
	ASSERT_EQ(oakaudio_processor_open(p.h, 48000, kLayoutStereo,
									kSampleFmtF32P, 48000, kLayoutStereo,
									kSampleFmtF32P, 1.0),
			  OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_processor_convert(p.h, nullptr, 10, out_planes, 1),
			  OAKAUDIO_E_INVALID);
	// Negative counts
	EXPECT_EQ(oakaudio_processor_convert(p.h, in_planes, -1, out_planes, 1),
			  OAKAUDIO_E_INVALID);

	EXPECT_EQ(oakaudio_processor_flush(p.h), OAKAUDIO_OK);
}
