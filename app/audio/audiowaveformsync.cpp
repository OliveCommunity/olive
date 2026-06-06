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

QVector<double>
AudioWaveformSync::ExtractRmsEnvelope(const core::SampleBuffer &samples,
									  size_t window_samples)
{
	QVector<double> envelope;

	const int channel_count = samples.channel_count();
	const size_t sample_count = samples.sample_count();
	if (!channel_count || !sample_count || !window_samples) {
		return envelope;
	}

	const size_t window_count =
		(sample_count + window_samples - 1) / window_samples;
	envelope.resize(static_cast<int>(window_count));

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

		envelope[static_cast<int>(window)] =
			total ? std::sqrt(square_sum / static_cast<double>(total)) : 0.0;
	}

	return envelope;
}

AudioWaveformSync::OffsetResult AudioWaveformSync::EstimateOffset(
	const core::SampleBuffer &reference, const core::SampleBuffer &candidate,
	size_t window_samples, int64_t max_offset_samples)
{
	if (!window_samples) {
		return OffsetResult();
	}

	const QVector<double> reference_envelope =
		ExtractRmsEnvelope(reference, window_samples);
	const QVector<double> candidate_envelope =
		ExtractRmsEnvelope(candidate, window_samples);
	const int64_t max_offset_windows =
		max_offset_samples / static_cast<int64_t>(window_samples);

	return EstimateEnvelopeOffset(reference_envelope, candidate_envelope,
								  window_samples, max_offset_windows);
}

AudioWaveformSync::OffsetResult AudioWaveformSync::EstimateEnvelopeOffset(
	const QVector<double> &reference, const QVector<double> &candidate,
	size_t window_samples, int64_t max_offset_windows)
{
	OffsetResult result;
	if (reference.isEmpty() || candidate.isEmpty() || !window_samples) {
		return result;
	}

	double best_score = -2.0;
	int64_t best_lag = 0;

	for (int64_t lag = -max_offset_windows; lag <= max_offset_windows; lag++) {
		const int reference_start = static_cast<int>(std::max<int64_t>(0, -lag));
		const int candidate_start = static_cast<int>(std::max<int64_t>(0, lag));
		const int overlap = std::min(reference.size() - reference_start,
									 candidate.size() - candidate_start);

		if (overlap < 2) {
			continue;
		}

		double reference_mean = 0.0;
		double candidate_mean = 0.0;
		for (int i = 0; i < overlap; i++) {
			reference_mean += reference.at(reference_start + i);
			candidate_mean += candidate.at(candidate_start + i);
		}
		reference_mean /= static_cast<double>(overlap);
		candidate_mean /= static_cast<double>(overlap);

		double numerator = 0.0;
		double reference_energy = 0.0;
		double candidate_energy = 0.0;
		for (int i = 0; i < overlap; i++) {
			const double reference_value =
				reference.at(reference_start + i) - reference_mean;
			const double candidate_value =
				candidate.at(candidate_start + i) - candidate_mean;
			numerator += reference_value * candidate_value;
			reference_energy += reference_value * reference_value;
			candidate_energy += candidate_value * candidate_value;
		}

		if (qFuzzyIsNull(reference_energy) || qFuzzyIsNull(candidate_energy)) {
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
		result.offset_samples =
			best_lag * static_cast<int64_t>(window_samples);
	}

	return result;
}

}
