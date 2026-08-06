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

#include "audio/sync.h"

#include <cstring>
#include <vector>

#include "audiosynchronizer.h"
#include "audiowaveformsync.h"
#include "ffmpeg_bridge/ffmpeg_bridge.h"

using olive::AudioSynchronizer;
using olive::AudioWaveformSync;
using olive::core::AudioParams;
using olive::core::Rational;
using olive::core::SampleBuffer;
using olive::core::SampleFormat;

namespace
{

std::vector<char> to_mask(const uint8_t *valid, int len)
{
	std::vector<char> mask;
	if (valid) {
		mask.resize(size_t(len));
		for (int i = 0; i < len; i++) {
			mask[size_t(i)] = valid[i] ? 1 : 0;
		}
	}
	return mask;
}

} // namespace

extern "C" int oakaudio_sync_extract_rms_envelope(
		const float *const *planar, int channel_count, int frame_count,
		uint64_t window_samples, double *out, int capacity)
{
	if (!planar || channel_count <= 0 || frame_count < 0 ||
		!window_samples || capacity < 0) {
		return OAKAUDIO_E_INVALID;
	}

	AudioParams params(48000, fb_channel_layout_default(channel_count),
					SampleFormat(SampleFormat::f32_p));
	SampleBuffer buffer(params, Rational(frame_count, 48000));
	for (int ch = 0; ch < channel_count; ch++) {
		if (!planar[ch]) {
			return OAKAUDIO_E_INVALID;
		}
		if (frame_count > 0) {
			memcpy(buffer.data(ch), planar[ch],
				   size_t(frame_count) * sizeof(float));
		}
	}

	const std::vector<double> envelope =
		AudioWaveformSync::extract_rms_envelope(buffer, window_samples);
	const int windows = int(envelope.size());
	if (!out || capacity < windows) {
		return windows;
	}
	memcpy(out, envelope.data(), size_t(windows) * sizeof(double));
	return windows;
}

extern "C" int oakaudio_sync_estimate_envelope_offset(
		const double *reference, int reference_len,
		const double *candidate, int candidate_len,
		const uint8_t *reference_valid, const uint8_t *candidate_valid,
		uint64_t window_samples, int64_t max_offset_windows,
		oakaudio_offset_result *out)
{
	if (!out || !reference || !candidate || reference_len <= 0 ||
		candidate_len <= 0 || !window_samples || max_offset_windows < 0) {
		return OAKAUDIO_E_INVALID;
	}

	const std::vector<double> ref(reference, reference + reference_len);
	const std::vector<double> cand(candidate, candidate + candidate_len);
	const std::vector<char> ref_valid = to_mask(reference_valid, reference_len);
	const std::vector<char> cand_valid =
		to_mask(candidate_valid, candidate_len);

	const AudioWaveformSync::OffsetResult r =
		AudioWaveformSync::estimate_envelope_offset(
			ref, cand, ref_valid, cand_valid, window_samples,
			max_offset_windows);

	out->offset_samples = r.offset_samples;
	out->confidence = r.confidence;
	out->valid = r.valid ? 1 : 0;
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_sync_estimate_stretch_and_offset(
		const double *reference, int reference_len,
		const double *candidate, int candidate_len,
		const uint8_t *reference_valid, const uint8_t *candidate_valid,
		uint64_t window_samples, int64_t max_offset_windows,
		double min_rate, double max_rate, double rate_step,
		oakaudio_stretch_offset_result *out)
{
	if (!out || !reference || !candidate || reference_len <= 0 ||
		candidate_len <= 0 || !window_samples || max_offset_windows < 0 ||
		min_rate <= 0.0 || max_rate < min_rate || rate_step <= 0.0) {
		return OAKAUDIO_E_INVALID;
	}

	const std::vector<double> ref(reference, reference + reference_len);
	const std::vector<double> cand(candidate, candidate + candidate_len);
	const std::vector<char> ref_valid = to_mask(reference_valid, reference_len);
	const std::vector<char> cand_valid =
		to_mask(candidate_valid, candidate_len);

	const AudioWaveformSync::StretchOffsetResult r =
		AudioWaveformSync::estimate_stretch_and_offset(
			ref, cand, ref_valid, cand_valid, window_samples,
			max_offset_windows, min_rate, max_rate, rate_step);

	out->rate = r.rate;
	out->offset_samples = r.offset_samples;
	out->confidence = r.confidence;
	out->valid = r.valid ? 1 : 0;
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_sync_place_by_source_time(
		const oakaudio_source_clip *reference,
		const oakaudio_source_clip *candidate,
		int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
		int64_t *out_num, int64_t *out_den, int *out_valid)
{
	if (!reference || !candidate || !out_num || !out_den || !out_valid ||
		reference->source_start_time_den == 0 ||
		reference->media_in_den == 0 ||
		candidate->source_start_time_den == 0 ||
		candidate->media_in_den == 0 || reference_timeline_in_den == 0) {
		return OAKAUDIO_E_INVALID;
	}

	AudioSynchronizer::SourceClip ref;
	ref.source_start_time = Rational(int(reference->source_start_time_num),
								  int(reference->source_start_time_den));
	ref.media_in = Rational(int(reference->media_in_num),
						 int(reference->media_in_den));
	ref.has_source_start_time = reference->has_source_start_time != 0;

	AudioSynchronizer::SourceClip cand;
	cand.source_start_time = Rational(int(candidate->source_start_time_num),
								   int(candidate->source_start_time_den));
	cand.media_in = Rational(int(candidate->media_in_num),
						  int(candidate->media_in_den));
	cand.has_source_start_time = candidate->has_source_start_time != 0;

	const AudioSynchronizer::Placement p = AudioSynchronizer::place_by_source_time(
		ref, cand,
		Rational(int(reference_timeline_in_num),
				 int(reference_timeline_in_den)));

	*out_num = p.timeline_in.numerator();
	*out_den = p.timeline_in.denominator();
	*out_valid = p.valid ? 1 : 0;
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_sync_place_by_waveform_offset(
		int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
		int64_t candidate_offset_samples, int sample_rate,
		int64_t *out_num, int64_t *out_den, int *out_valid)
{
	if (!out_num || !out_den || !out_valid ||
		reference_timeline_in_den == 0) {
		return OAKAUDIO_E_INVALID;
	}

	const AudioSynchronizer::Placement p =
		AudioSynchronizer::place_by_waveform_offset(
			Rational(int(reference_timeline_in_num),
					 int(reference_timeline_in_den)),
			candidate_offset_samples, sample_rate);

	*out_num = p.timeline_in.numerator();
	*out_den = p.timeline_in.denominator();
	*out_valid = p.valid ? 1 : 0;
	return OAKAUDIO_OK;
}
