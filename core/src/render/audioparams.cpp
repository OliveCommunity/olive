/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
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

#include "render/audioparams.h"

#include <cmath>

namespace olive::core
{

const std::vector<int> AudioParams::kSupportedSampleRates = {
	8000, // 8000 Hz
	11025, // 11025 Hz
	16000, // 16000 Hz
	22050, // 22050 Hz
	24000, // 24000 Hz
	32000, // 32000 Hz
	44100, // 44100 Hz
	48000, // 48000 Hz
	88200, // 88200 Hz
	96000 // 96000 Hz
};

const std::vector<uint64_t> AudioParams::kSupportedChannelLayouts = {
	AV_CH_LAYOUT_MONO, AV_CH_LAYOUT_STEREO, AV_CH_LAYOUT_2_1,
	AV_CH_LAYOUT_5POINT1, AV_CH_LAYOUT_7POINT1
};

bool AudioParams::operator==(const AudioParams &other) const
{
	return format() == other.format() && sample_rate() == other.sample_rate() &&
		   time_base() == other.time_base() &&
		   av_channel_layout_compare(&channel_layout_,
									 &other.channel_layout()) == 0;
}

bool AudioParams::operator!=(const AudioParams &other) const
{
	return !(*this == other);
}

int64_t AudioParams::time_to_bytes(const double &time) const
{
	return time_to_bytes_per_channel(time) * channel_count();
}

int64_t AudioParams::time_to_bytes(const rational &time) const
{
	return time_to_bytes(time.toDouble());
}

int64_t AudioParams::time_to_bytes_per_channel(const double &time) const
{
	assert(is_valid());

	return int64_t(time_to_samples(time)) * bytes_per_sample_per_channel();
}

int64_t AudioParams::time_to_bytes_per_channel(const rational &time) const
{
	return time_to_bytes_per_channel(time.toDouble());
}

int64_t AudioParams::time_to_samples(const double &time) const
{
	assert(is_valid());

	return std::round(double(sample_rate()) * time);
}

int64_t AudioParams::time_to_samples(const rational &time) const
{
	return time_to_samples(time.toDouble());
}

int64_t AudioParams::samples_to_bytes(const int64_t &samples) const
{
	assert(is_valid());

	return samples_to_bytes_per_channel(samples) * channel_count();
}

int64_t AudioParams::samples_to_bytes_per_channel(const int64_t &samples) const
{
	assert(is_valid());

	return samples * bytes_per_sample_per_channel();
}

rational AudioParams::samples_to_time(const int64_t &samples) const
{
	return sample_rate_as_time_base() * samples;
}

int64_t AudioParams::bytes_to_samples(const int64_t &bytes) const
{
	assert(is_valid());

	return bytes / (channel_count() * bytes_per_sample_per_channel());
}

rational AudioParams::bytes_to_time(const int64_t &bytes) const
{
	assert(is_valid());

	return samples_to_time(bytes_to_samples(bytes));
}

rational AudioParams::bytes_per_channel_to_time(const int64_t &bytes) const
{
	assert(is_valid());

	return samples_to_time(bytes_to_samples(bytes * channel_count()));
}

int AudioParams::channel_count() const
{
	return channel_count_;
}

int AudioParams::bytes_per_sample_per_channel() const
{
	return format_.byte_count();
}

int AudioParams::bits_per_sample() const
{
	return bytes_per_sample_per_channel() * 8;
}

bool AudioParams::is_valid() const
{
	return (!time_base().isNull() &&
			av_channel_layout_check(&channel_layout_) &&
			format_ > SampleFormat::INVALID && format_ < SampleFormat::COUNT);
}

void AudioParams::calculate_channel_count()
{
	channel_count_ = channel_layout().nb_channels;
}

/**
 * @brief Copy constructor - deep copies AVChannelLayout
 * 
 * This is critical because AVChannelLayout::u.map is a pointer for custom
 * channel layouts. Default copy would share the pointer, leading to double-free.
 * 
 * The member initializer list initializes channel_layout_ to zero ({}),
 * then av_channel_layout_copy performs the deep copy from other.
 * 
 * @param other Source AudioParams to copy from
 */
AudioParams::AudioParams(const AudioParams &other)
	: sample_rate_(other.sample_rate_)
	, channel_layout_{}  // Zero-initialize before FFmpeg copy
	, channel_count_(other.channel_count_)
	, format_(other.format_)
	, enabled_(other.enabled_)
	, stream_index_(other.stream_index_)
	, duration_(other.duration_)
	, timebase_(other.timebase_)
{
	// Deep copy AVChannelLayout using FFmpeg API
	// This handles all layout types: unspecified, native (mask), and custom (map)
	av_channel_layout_copy(&channel_layout_, &other.channel_layout_);
}

/**
 * @brief Copy assignment - cleans up existing layout before copying
 * 
 * CRITICAL ORDER OF OPERATIONS:
 * 1. Check for self-assignment (this != &other)
 * 2. Copy all scalar members
 * 3. Uninitialize current channel_layout_ (frees old u.map if present)
 * 4. Deep copy from other's channel_layout_
 * 
 * Step 3 must happen before step 4 to avoid memory leaks. If we copied first,
 * we'd lose the pointer to the old u.map that needs to be freed.
 * 
 * @param other Source AudioParams to copy from
 * @return Reference to this for chaining
 */
AudioParams &AudioParams::operator=(const AudioParams &other)
{
	if (this != &other) {
		// Copy scalar members first (no dependencies)
		sample_rate_ = other.sample_rate_;
		format_ = other.format_;
		channel_count_ = other.channel_count_;
		enabled_ = other.enabled_;
		stream_index_ = other.stream_index_;
		duration_ = other.duration_;
		timebase_ = other.timebase_;
		
		// Free current layout's dynamic memory (u.map if custom)
		av_channel_layout_uninit(&channel_layout_);
		
		// Deep copy from other (includes allocating new u.map if needed)
		av_channel_layout_copy(&channel_layout_, &other.channel_layout_);
	}
	return *this;
}

/**
 * @brief Destructor - frees AVChannelLayout dynamic memory
 * 
 * av_channel_layout_uninit() handles all cases:
 * - Unspecified/Native: No-op (no dynamic memory)
 * - Custom: Frees u.map array
 * 
 * Without this, custom channel layouts would leak memory.
 */
AudioParams::~AudioParams()
{
	av_channel_layout_uninit(&channel_layout_);
}

}
