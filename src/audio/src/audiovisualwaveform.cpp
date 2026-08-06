/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "audiovisualwaveform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "olive/core/util/cpuoptimize.h"

namespace olive
{

using core::Rational;

const Rational AudioVisualWaveform::k_minimum_sample_rate = Rational(1, 8);
const Rational AudioVisualWaveform::k_maximum_sample_rate = 1024;

AudioVisualWaveform::AudioVisualWaveform()
	: channels_(0)
{
	for (Rational i = k_minimum_sample_rate; i <= k_maximum_sample_rate; i *= 2) {
		mipmapped_data_.insert({ i, Sample() });
	}
}

void AudioVisualWaveform::overwrite_samples_from_buffer(
	const core::SampleBuffer &samples, int sample_rate, const Rational &start,
	double target_rate, Sample &data, size_t &start_index,
	size_t &samples_length)
{
	start_index = time_to_samples(start, target_rate);
	samples_length =
		time_to_samples(static_cast<double>(samples.sample_count()) /
							static_cast<double>(sample_rate),
						target_rate);

	size_t end_index = start_index + samples_length;
	if (data.size() < end_index) {
		data.resize(end_index);
	}

	double chunk_size = double(sample_rate) / double(target_rate);

	for (size_t i = 0; i < samples_length; i += channels_) {
		size_t src_start = size_t(std::llround(double(i) * chunk_size)) / channels_;
		size_t src_end = std::min(
			size_t(std::llround(double(i + channels_) * chunk_size)) / channels_,
			samples.sample_count());

		Sample summary = sum_samples(samples, src_start, src_end - src_start);

		memcpy(&data.data()[i + start_index], summary.data(),
			   summary.size() * sizeof(SamplePerChannel));
	}
}

void AudioVisualWaveform::overwrite_samples_from_mipmap(
	const AudioVisualWaveform::Sample &input, double input_sample_rate,
	size_t &input_start, size_t &input_length, const Rational &start,
	double output_rate, AudioVisualWaveform::Sample &output_data)
{
	size_t start_index = time_to_samples(start, output_rate);
	size_t samples_length = time_to_samples(
		static_cast<double>(input_length / channels_) / input_sample_rate,
		output_rate);

	size_t end_index = start_index + samples_length;
	if (output_data.size() < end_index) {
		output_data.resize(end_index);
	}

	// We guarantee mipmaps are powers of two so integer division should be perfectly accurate here
	size_t chunk_size = size_t(input_sample_rate / output_rate);

	for (size_t i = 0; i < samples_length; i += channels_) {
		Sample summary =
			re_sum_samples(&input.data()[input_start + (i * chunk_size)],
						 chunk_size * channels_, channels_);

		memcpy(&output_data.data()[i + start_index], summary.data(),
			   summary.size() * sizeof(SamplePerChannel));
	}

	input_start = start_index;
	input_length = samples_length;
}

void AudioVisualWaveform::validate_virtual_start(const Rational &new_start)
{
	if (length_ == 0) {
		virtual_start_ = new_start;
	} else if (virtual_start_ > new_start) {
		trim_in(new_start - virtual_start_);
	}
}

void AudioVisualWaveform::overwrite_samples(const core::SampleBuffer &samples,
										   int sample_rate,
										   const Rational &start)
{
	if (!channels_) {
		fprintf(stderr,
				"AudioVisualWaveform: failed to write samples - channel "
				"count is zero\n");
		return;
	}

	validate_virtual_start(start);

	// Process the largest mipmap directly for the samples
	auto current_mipmap = mipmapped_data_.rbegin();
	size_t input_start, input_length;
	overwrite_samples_from_buffer(samples, sample_rate, start - virtual_start_,
							   current_mipmap->first.to_double(),
							   current_mipmap->second, input_start,
							   input_length);

	while (true) {
		// For each smaller mipmap, we just process from the mipmap before it, making each one
		// exponentially faster to create
		auto previous_mipmap = current_mipmap;
		current_mipmap++;
		if (current_mipmap == mipmapped_data_.rend()) {
			break;
		}

		overwrite_samples_from_mipmap(
			previous_mipmap->second, previous_mipmap->first.to_double(),
			input_start, input_length, start - virtual_start_,
			current_mipmap->first.to_double(), current_mipmap->second);
	}

	Rational sample_length(int64_t(samples.sample_count()), sample_rate);
	length_ = std::max(length_, start + sample_length);
}

void AudioVisualWaveform::overwrite_sums(const AudioVisualWaveform &sums,
										const Rational &dest,
										const Rational &offset,
										const Rational &length)
{
	validate_virtual_start(dest);

	for (auto it = mipmapped_data_.begin(); it != mipmapped_data_.end(); it++) {
		Rational rate = it->first;

		Sample &our_arr = it->second;
		const Sample &their_arr = sums.mipmapped_data_.at(rate);

		double rate_dbl = rate.to_double();

		// Get our destination sample
		size_t our_start_index =
			time_to_samples(dest - virtual_start_, rate_dbl);

		// Get our source sample, indexing with the SOURCE's channel count
		size_t their_start_index = size_t(std::floor(offset.to_double() * rate_dbl)) *
								   size_t(sums.channel_count());
		if (their_start_index >= their_arr.size()) {
			continue;
		}

		// Determine how much we're copying
		size_t copy_len = their_arr.size() - their_start_index;
		if (!length.isNull()) {
			copy_len = std::min(copy_len, time_to_samples(length, rate_dbl));
			if (copy_len == 0) {
				continue;
			}
		}

		// Determine end index of our array
		size_t end_index = our_start_index + copy_len;
		if (our_arr.size() < end_index) {
			our_arr.resize(end_index);
		}

		memcpy(reinterpret_cast<char *>(our_arr.data()) +
				   our_start_index * sizeof(SamplePerChannel),
			   reinterpret_cast<const char *>(their_arr.data()) +
				   their_start_index * sizeof(SamplePerChannel),
			   copy_len * sizeof(SamplePerChannel));
	}

	length_ = std::max(length_, dest + ((length.isNull()) ? sums.length() - offset :
														length));
}

void AudioVisualWaveform::overwrite_silence(const Rational &start,
										   const Rational &length)
{
	validate_virtual_start(start);

	for (auto it = mipmapped_data_.begin(); it != mipmapped_data_.end(); it++) {
		Rational rate = it->first;

		Sample &our_arr = it->second;

		double rate_dbl = rate.to_double();

		// Get our destination sample
		size_t our_start_index =
			time_to_samples(start - virtual_start_, rate_dbl);
		size_t our_length_index = time_to_samples(length, rate_dbl);
		size_t our_end_index = our_start_index + our_length_index;

		if (our_arr.size() < our_end_index) {
			our_arr.resize(our_end_index);
		}

		memset(reinterpret_cast<char *>(our_arr.data()) +
				   our_start_index * sizeof(SamplePerChannel),
			   0, our_length_index * sizeof(SamplePerChannel));
	}

	length_ = std::max(length_, start + length);
}

void AudioVisualWaveform::trim_in(Rational length)
{
	if (length == 0) {
		return;
	}

	virtual_start_ += length;

	bool negative = (length < 0);
	if (negative) {
		length = -length;
	}

	for (auto it = mipmapped_data_.begin(); it != mipmapped_data_.end(); it++) {
		Rational rate = it->first;
		double rate_dbl = rate.to_double();
		Sample &data = it->second;

		size_t chop_length = time_to_samples(length, rate_dbl);
		if (chop_length == 0) {
			continue;
		}

		if (!negative) {
			data = Sample(data.begin() + chop_length, data.end());
		} else {
			data.insert(data.begin(), chop_length, SamplePerChannel());
		}
	}

	if (!negative) {
		length_ = std::max(Rational(0), length_ - length);
	}
	// Prepending grows the data before the existing start, so the absolute
	// end (which length_ tracks) is unchanged
}

AudioVisualWaveform AudioVisualWaveform::mid(const Rational &offset) const
{
	AudioVisualWaveform mid = *this;

	mid.trim_in(offset - virtual_start_);

	return mid;
}

AudioVisualWaveform AudioVisualWaveform::mid(const Rational &offset,
											 const Rational &length) const
{
	AudioVisualWaveform mid = *this;

	mid.trim_range(offset - virtual_start_, length);

	return mid;
}

void AudioVisualWaveform::resize(const Rational &length)
{
	if (length_ == length) {
		return;
	}

	for (auto it = mipmapped_data_.begin(); it != mipmapped_data_.end(); it++) {
		Rational rate = it->first;
		double rate_dbl = rate.to_double();
		Sample &data = it->second;

		size_t chop_length = time_to_samples(length, rate_dbl);

		data.resize(chop_length);
	}

	length_ = length;
}

void AudioVisualWaveform::trim_range(const Rational &in, const Rational &length)
{
	trim_in(in);
	resize(length);
}

AudioVisualWaveform::Sample
AudioVisualWaveform::get_summary_from_time(const Rational &start,
										const Rational &length) const
{
	// Find mipmap that requires
	auto using_mipmap = get_mipmap_for_scale(length.flipped().to_double());

	double rate_dbl = using_mipmap->first.to_double();

	size_t start_sample = time_to_samples(start - virtual_start_, rate_dbl);
	size_t sample_length = time_to_samples(length, rate_dbl);

	const Sample &mipmap_data = using_mipmap->second;

	// Determine if the array actually has this sample. Compare in signed
	// arithmetic so a start past the end of the data doesn't underflow.
	int64_t available = int64_t(mipmap_data.size()) - int64_t(start_sample);
	if (available > 0) {
		sample_length = std::min(sample_length, size_t(available));

		if (sample_length > 0) {
			return re_sum_samples(&mipmap_data.data()[start_sample],
								sample_length, channels_);
		}
	}

	// Return null samples
	return AudioVisualWaveform::Sample(size_t(channel_count()), { 0, 0 });
}

void expand_min_max_channel(const float *a, size_t length, float &min_val,
						 float &max_val)
{
#if defined(OLIVE_PROCESSOR_X86) || defined(OLIVE_PROCESSOR_ARM)
	// SSE optimized

	// load the first 4 elements of 'a' into min and max (they are 4 * 32 = 128 bits)
	__m128 max = _mm_loadu_ps(a);
	__m128 min = _mm_loadu_ps(a);

	// loop over 'a' and compare current elements with min and max 4 by 4.
	// we need to make sure we don't read out of boundaries should 'a' length be not mod. 4
	for (size_t i = 4; i < length - 4; i += 4) {
		__m128 cur = _mm_loadu_ps(a + i);
		max = _mm_max_ps(max, cur);
		min = _mm_min_ps(min, cur);
	}
	// so we read the last 4 (or less) elements in a safe manner.
	__m128 cur = _mm_loadu_ps(a + length - 4);
	max = _mm_max_ps(max, cur);
	min = _mm_min_ps(min, cur);
	// this potentially overlaps up to the last 3 elements but it's not an issue.

	// min and max will contain 4 min and max. To get the absolute min and max
	// we need to compare the 4 values over themselves by shuffling each time.
	for (size_t i = 0; i < 3; i++) {
		max = _mm_max_ps(max, _mm_shuffle_ps(max, max, 0x93));
		min = _mm_min_ps(min, _mm_shuffle_ps(min, min, 0x93));
	}
	// now min and max contain 4 identical items each representing min and max value respectively.

	// and we store the first one into a float variable.
	_mm_store_ss(&max_val, max);
	_mm_store_ss(&min_val, min);
	// I bet you don't find annotated low level code very often.
#else
	// Standard unoptimized function
	for (size_t i = 0; i < length; i++) {
		min_val = std::min(min_val, a[i]);
		max_val = std::max(max_val, a[i]);
	}
#endif
}

AudioVisualWaveform::Sample
AudioVisualWaveform::sum_samples(const core::SampleBuffer &samples,
								size_t start_index, size_t length)
{
	int channels = samples.audio_params().channel_count();
	const size_t channel_count = size_t(channels);
	AudioVisualWaveform::Sample summed_samples(channel_count);

	for (int channel = 0; channel < channels; channel++) {
		expand_min_max_channel(samples.data(channel) + start_index, length,
							summed_samples[size_t(channel)].min,
							summed_samples[size_t(channel)].max);
	}

	// for reference: this approximation is n x faster (and less accurate) for a n-tracks clip
	// for (size_t i=start_index; i<end_index; i++) {
	//   ExpandMinMax(summed_samples[i%channels], samples->data(i%channels)[i]);
	// }

	return summed_samples;
}

AudioVisualWaveform::Sample
AudioVisualWaveform::re_sum_samples(const SamplePerChannel *samples,
								  size_t nb_samples, int nb_channels)
{
	const size_t channel_count = size_t(nb_channels);
	AudioVisualWaveform::Sample summed_samples(channel_count);

	// Initialize from the first point instead of {0,0}: the engine version
	// started from zero-initialized pairs, which clamped all-positive
	// (resp. all-negative) ranges to a zero min (max). Fixed in oakaudio.
	if (nb_samples >= channel_count) {
		for (size_t j = 0; j < channel_count; j++) {
			summed_samples[j] = samples[j];
		}
	}

	for (size_t i = 0; i < nb_samples; i += size_t(nb_channels)) {
		for (int j = 0; j < nb_channels; j++) {
			const AudioVisualWaveform::SamplePerChannel &sample =
				samples[i + size_t(j)];

			if (sample.min < summed_samples[size_t(j)].min) {
				summed_samples[size_t(j)].min = sample.min;
			}

			if (sample.max > summed_samples[size_t(j)].max) {
				summed_samples[size_t(j)].max = sample.max;
			}
		}
	}

	return summed_samples;
}

size_t AudioVisualWaveform::time_to_samples(const Rational &time,
											double sample_rate) const
{
	return time_to_samples(time.to_double(), sample_rate);
}

size_t AudioVisualWaveform::time_to_samples(const double &time,
											double sample_rate) const
{
	return size_t(std::floor(time * sample_rate)) * size_t(channels_);
}

std::map<Rational, AudioVisualWaveform::Sample>::const_iterator
AudioVisualWaveform::get_mipmap_for_scale(double scale) const
{
	// Find largest mipmap for this scale (or the largest if we don't find one sufficient)
	for (auto it = mipmapped_data_.cbegin(); it != mipmapped_data_.cend();
		 it++) {
		if (it->first.to_double() >= scale) {
			return it;
		}
	}

	// We don't have a mipmap large enough for this scale, so just return the largest we have
	return std::prev(mipmapped_data_.cend());
}

}
