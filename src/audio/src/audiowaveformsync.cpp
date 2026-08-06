/***

  Oak - Non-Linear Video Editor
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

#include "audiowaveformsync.h"

#include <algorithm>
#include <cmath>

namespace olive
{

std::vector<double>
AudioWaveformSync::extract_rms_envelope(const core::SampleBuffer &samples,
									  size_t window_samples)
{
	std::vector<double> envelope;

	const int channel_count = samples.channel_count();
	const size_t sample_count = samples.sample_count();
	if (!channel_count || !sample_count || !window_samples) {
		return envelope;
	}

	const size_t window_count =
		(sample_count + window_samples - 1) / window_samples;
	envelope.resize(window_count);

	for (size_t window = 0; window < window_count; window++) {
		const size_t start = window * window_samples;
		const size_t end = std::min(start + window_samples, sample_count);
		double square_sum = 0.0;
		size_t total = 0;

		for (int channel = 0; channel < channel_count; channel++) {
			const float *data = samples.data(channel);
			for (size_t sample = start; sample < end; sample++) {
				const double value = data[sample];
				square_sum += value * value;
				total++;
			}
		}

		envelope[window] =
			total ? std::sqrt(square_sum / static_cast<double>(total)) : 0.0;
	}

	return envelope;
}

AudioWaveformSync::OffsetResult AudioWaveformSync::estimate_offset(
	const core::SampleBuffer &reference, const core::SampleBuffer &candidate,
	size_t window_samples, int64_t max_offset_samples)
{
	if (!window_samples) {
		return OffsetResult();
	}

	const std::vector<double> reference_envelope =
		extract_rms_envelope(reference, window_samples);
	const std::vector<double> candidate_envelope =
		extract_rms_envelope(candidate, window_samples);
	const int64_t max_offset_windows =
		max_offset_samples / static_cast<int64_t>(window_samples);

	return estimate_envelope_offset(reference_envelope, candidate_envelope,
								  window_samples, max_offset_windows);
}

AudioWaveformSync::OffsetResult AudioWaveformSync::estimate_envelope_offset(
	const std::vector<double> &reference, const std::vector<double> &candidate,
	size_t window_samples, int64_t max_offset_windows)
{
	return estimate_envelope_offset(reference, candidate, std::vector<char>(),
								  std::vector<char>(), window_samples,
								  max_offset_windows);
}

AudioWaveformSync::OffsetResult AudioWaveformSync::estimate_envelope_offset(
	const std::vector<double> &reference, const std::vector<double> &candidate,
	const std::vector<char> &reference_valid,
	const std::vector<char> &candidate_valid,
	size_t window_samples, int64_t max_offset_windows)
{
	OffsetResult result;
	if (reference.empty() || candidate.empty() || !window_samples) {
		return result;
	}

	const auto is_valid = [](const std::vector<char> &mask, size_t size,
						  size_t index) {
		return mask.size() != size || mask.at(index);
	};

	double best_score = -2.0;
	int64_t best_lag = 0;

	const int reference_size = static_cast<int>(reference.size());
	const int candidate_size = static_cast<int>(candidate.size());

	for (int64_t lag = -max_offset_windows; lag <= max_offset_windows; lag++) {
		const int reference_start =
			static_cast<int>(std::max<int64_t>(0, -lag));
		const int candidate_start = static_cast<int>(std::max<int64_t>(0, lag));
		const int overlap = std::min(reference_size - reference_start,
									 candidate_size - candidate_start);

		if (overlap < 2) {
			continue;
		}

		// Only windows marked valid on both sides participate in the score
		double reference_mean = 0.0;
		double candidate_mean = 0.0;
		int valid_count = 0;
		for (int i = 0; i < overlap; i++) {
			const int reference_index = reference_start + i;
			const int candidate_index = candidate_start + i;
			if (!is_valid(reference_valid, reference.size(),
						  size_t(reference_index)) ||
				!is_valid(candidate_valid, candidate.size(),
						  size_t(candidate_index))) {
				continue;
			}
			reference_mean += reference.at(size_t(reference_index));
			candidate_mean += candidate.at(size_t(candidate_index));
			valid_count++;
		}

		if (valid_count < 2) {
			continue;
		}

		reference_mean /= static_cast<double>(valid_count);
		candidate_mean /= static_cast<double>(valid_count);

		double numerator = 0.0;
		double reference_energy = 0.0;
		double candidate_energy = 0.0;
		for (int i = 0; i < overlap; i++) {
			const int reference_index = reference_start + i;
			const int candidate_index = candidate_start + i;
			if (!is_valid(reference_valid, reference.size(),
						  size_t(reference_index)) ||
				!is_valid(candidate_valid, candidate.size(),
						  size_t(candidate_index))) {
				continue;
			}
			const double reference_value =
				reference.at(size_t(reference_index)) - reference_mean;
			const double candidate_value =
				candidate.at(size_t(candidate_index)) - candidate_mean;
			numerator += reference_value * candidate_value;
			reference_energy += reference_value * reference_value;
			candidate_energy += candidate_value * candidate_value;
		}

		// qFuzzyIsNull(double): |x| < 1e-12
		if (std::abs(reference_energy) < 1e-12 ||
			std::abs(candidate_energy) < 1e-12) {
			continue;
		}

		const double score =
			numerator / std::sqrt(reference_energy * candidate_energy);
		if (score > best_score) {
			best_score = score;
			best_lag = lag;
		}
	}

	if (best_score > -2.0) {
		result.valid = true;
		result.confidence = std::max(0.0, best_score);
		result.offset_samples = best_lag * static_cast<int64_t>(window_samples);
	}

	return result;
}

AudioWaveformSync::StretchOffsetResult AudioWaveformSync::estimate_stretch_and_offset(
	const std::vector<double> &reference, const std::vector<double> &candidate,
	const std::vector<char> &reference_valid,
	const std::vector<char> &candidate_valid,
	size_t window_samples, int64_t max_offset_windows, double min_rate,
	double max_rate, double rate_step)
{
	StretchOffsetResult result;
	if (reference.empty() || candidate.empty() || !window_samples ||
		min_rate <= 0.0 || max_rate < min_rate || rate_step <= 0.0) {
		return result;
	}

	double best_confidence = -2.0;

	for (double rate = min_rate; rate <= max_rate + rate_step * 0.5;
		 rate += rate_step) {
		// Resample the candidate envelope so that window i of the resampled
		// envelope corresponds to window i*rate of the original
		const int resampled_size =
			static_cast<int>(candidate.size() / rate);
		if (resampled_size < 2) {
			continue;
		}

		const size_t resampled_len = size_t(resampled_size);
		std::vector<double> resampled(resampled_len);
		std::vector<char> resampled_valid(resampled_len);
		for (int i = 0; i < resampled_size; i++) {
			const double position = i * rate;
			const int lower = static_cast<int>(position);
			const int upper =
				std::min(lower + 1, static_cast<int>(candidate.size()) - 1);
			const double fraction = position - lower;

			resampled[size_t(i)] = candidate.at(size_t(lower)) * (1.0 - fraction) +
							   candidate.at(size_t(upper)) * fraction;

			resampled_valid[size_t(i)] =
				(candidate_valid.size() != candidate.size() ||
				 (candidate_valid.at(size_t(lower)) &&
				  candidate_valid.at(size_t(upper))));
		}

		const OffsetResult offset = estimate_envelope_offset(
			reference, resampled, reference_valid, resampled_valid,
			window_samples, max_offset_windows);

		if (offset.valid && offset.confidence > best_confidence) {
			best_confidence = offset.confidence;
			result.valid = true;
			result.rate = rate;
			result.confidence = offset.confidence;
			result.offset_samples = offset.offset_samples;
		}
	}

	return result;
}

}
