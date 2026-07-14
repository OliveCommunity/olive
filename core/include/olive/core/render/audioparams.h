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

#ifndef LIBOLIVECORE_AUDIOPARAMS_H
#define LIBOLIVECORE_AUDIOPARAMS_H
#include <cstring>

extern "C" {
#include <libavutil/channel_layout.h>
}

#include <assert.h>
#include <vector>

#include "sampleformat.h"
#include "../util/rational.h"

namespace olive::core
{

/**
 * @brief Audio parameters class managing audio stream configuration
 * 
 * CRITICAL NOTE: This class manages AVChannelLayout which contains dynamic memory
 * (custom channel maps via u.map pointer). Prior to the Rule of Three implementation,
 * shallow copies could occur when:
 * - AudioParams stored in QVector (QVector reallocations)
 * - AudioParams passed by value to RenderVideoParams
 * - AudioParams copied during Node graph duplication in ProjectCopier
 * 
 * When shallow copies occurred, one copy's set_channel_layout() could free the
 * shared u.map pointer, corrupting other copies. This manifested as:
 * - channel_layouts=0x0 errors in AudioProcessor::Open()
 * - is_valid() returning false unexpectedly
 * 
 * The Rule of Three (copy ctor, copy assignment, destructor) was added to ensure
 * proper deep copies of AVChannelLayout using av_channel_layout_copy().
 */
class AudioParams {
public:
	/**
	 * @brief Default constructor creates invalid AudioParams
	 * sample_rate=0, channel_layout empty, format=INVALID
	 */
	AudioParams()
		: sample_rate_(0)
		, channel_layout_{}
		, channel_count_(0)
		, format_(SampleFormat::INVALID)
	{
		set_default_footage_parameters();
	}

	/**
	 * @brief Constructor from AVChannelLayout (deep copy)
	 * @param sample_rate Audio sample rate (e.g., 48000)
	 * @param channel_layout FFmpeg channel layout (copied via av_channel_layout_copy)
	 * @param format Sample format (e.g., SampleFormat::F32P)
	 * 
	 * NOTE: The channel_layout parameter is deep-copied. The original can be
	 * safely uninit'd after this constructor returns.
	 */
	AudioParams(const int &sample_rate, const AVChannelLayout &channel_layout,
				const SampleFormat &format)
		: sample_rate_(sample_rate)
		, channel_layout_{}
		, channel_count_(0)
		, format_(format)
	{
		set_default_footage_parameters();
		timebase_ = sample_rate_as_time_base();
		av_channel_layout_uninit(&channel_layout_);
		av_channel_layout_copy(&channel_layout_, &channel_layout);

		// Cache channel count from the copied layout
		calculate_channel_count();
	}

	/**
	 * @brief Constructor from channel layout mask
	 * @param sample_rate Audio sample rate
	 * @param channel_layout Channel layout mask (e.g., AV_CH_LAYOUT_STEREO)
	 * @param format Sample format
	 * 
	 * This is the most common constructor used in Olive. The mask is converted
	 * to AVChannelLayout via av_channel_layout_from_mask().
	 */
	AudioParams(const int &sample_rate, uint64_t channel_layout,
				const SampleFormat &format)
		: sample_rate_(sample_rate)
		, channel_layout_{}
		, channel_count_(0)
		, format_(format)
	{
		set_default_footage_parameters();
		timebase_ = sample_rate_as_time_base();
		av_channel_layout_uninit(&channel_layout_);
		av_channel_layout_from_mask(&channel_layout_, channel_layout);
		// Cache channel count
		calculate_channel_count();
	}
	int sample_rate() const
	{
		return sample_rate_;
	}

	void set_sample_rate(int sample_rate)
	{
		sample_rate_ = sample_rate;
	}

	const AVChannelLayout &channel_layout() const
	{
		return channel_layout_;
	}

	/**
	 * @brief Set channel layout from AVChannelLayout (deep copy)
	 * @param channel_layout Source channel layout to copy
	 * 
	 * CRITICAL: This function first uninitializes the current layout (freeing any
	 * dynamic memory), then deep-copies the new layout. This is safe only if
	 * copies are properly managed via Rule of Three.
	 * 
	 * If called on a shallow-copied AudioParams, this would corrupt other copies
	 * that share the same u.map pointer.
	 */
	void set_channel_layout(const AVChannelLayout &channel_layout)
	{
		av_channel_layout_uninit(&channel_layout_);
		av_channel_layout_copy(&channel_layout_, &channel_layout);
		calculate_channel_count();
	}

	/**
	 * @brief Set channel layout from mask
	 * @param mask Channel layout mask (e.g., AV_CH_LAYOUT_STEREO)
	 */
	void set_channel_layout(uint64_t mask)
	{
		av_channel_layout_uninit(&channel_layout_);
		av_channel_layout_from_mask(&channel_layout_, mask);
		calculate_channel_count();
	}
	rational time_base() const
	{
		return timebase_;
	}

	void set_time_base(const rational &timebase)
	{
		timebase_ = timebase;
	}

	rational sample_rate_as_time_base() const
	{
		return rational(1, sample_rate());
	}

	SampleFormat format() const
	{
		return format_;
	}

	void set_format(SampleFormat format)
	{
		format_ = format;
	}

	bool enabled() const
	{
		return enabled_;
	}

	void set_enabled(bool e)
	{
		enabled_ = e;
	}

	int stream_index() const
	{
		return stream_index_;
	}

	void set_stream_index(int s)
	{
		stream_index_ = s;
	}

	int64_t duration() const
	{
		return duration_;
	}

	void set_duration(int64_t duration)
	{
		duration_ = duration;
	}

	int64_t time_to_bytes(const double &time) const;
	int64_t time_to_bytes(const rational &time) const;
	int64_t time_to_bytes_per_channel(const double &time) const;
	int64_t time_to_bytes_per_channel(const rational &time) const;
	int64_t time_to_samples(const double &time) const;
	int64_t time_to_samples(const rational &time) const;
	int64_t samples_to_bytes(const int64_t &samples) const;
	int64_t samples_to_bytes_per_channel(const int64_t &samples) const;
	rational samples_to_time(const int64_t &samples) const;
	int64_t bytes_to_samples(const int64_t &bytes) const;
	rational bytes_to_time(const int64_t &bytes) const;
	rational bytes_per_channel_to_time(const int64_t &bytes) const;
	int channel_count() const;
	int bytes_per_sample_per_channel() const;
	int bits_per_sample() const;
	bool is_valid() const;

	bool operator==(const AudioParams &other) const;
	bool operator!=(const AudioParams &other) const;

	/**
	 * @name Rule of Three Implementation
	 * 
	 * These are required because AVChannelLayout (FFmpeg >= 5.0) contains a union
	 * with a pointer member (u.map for custom channel maps). Without proper
	 * deep copy management:
	 * 
	 * 1. Default copy constructor: Shallow copies u.map pointer, leading to
	 *    double-free when original and copy are destroyed
	 * 2. Default copy assignment: Same issue as copy constructor
	 * 3. Default destructor: Doesn't free u.map, causing memory leaks
	 * 
	 * The implementations use av_channel_layout_copy() and av_channel_layout_uninit()
	 * for proper FFmpeg-managed memory handling.
	 * 
	 * Context where this matters:
	 * - QVector<AudioParams> in FootageDescription (vector reallocations)
	 * - RenderVideoParams passing AudioParams by value
	 * - ProjectCopier duplicating node graphs with audio parameters
	 */
	///@{
	AudioParams(const AudioParams &other);
	AudioParams &operator=(const AudioParams &other);
	~AudioParams();
	///@}

	static const std::vector<uint64_t> kSupportedChannelLayouts;
	static const std::vector<int> kSupportedSampleRates;

private:
	void set_default_footage_parameters()
	{
		enabled_ = true;
		stream_index_ = 0;
		duration_ = 0;
	}

	/**
	 * @brief Updates channel_count_ from the current channel_layout_
	 * Called after any channel layout modification.
	 */
	void calculate_channel_count();

	int sample_rate_;                    ///< Audio sample rate in Hz (e.g., 48000)

	/**
	 * @brief FFmpeg channel layout structure
	 * 
	 * WARNING: This struct contains a union with a pointer member (u.map) when
	 * using custom channel layouts (order == AV_CHANNEL_ORDER_CUSTOM). The pointer
	 * must be properly managed via av_channel_layout_copy/uninit.
	 * 
	 * Layout variants:
	 * - order == AV_CHANNEL_ORDER_UNSPEC: u.mask is undefined, nb_channels valid
	 * - order == AV_CHANNEL_ORDER_NATIVE: u.mask contains channel bitmask
	 * - order == AV_CHANNEL_ORDER_CUSTOM: u.map points to AVChannelCustom array
	 * 
	 * Corruption symptoms:
	 * - u.mask == 0 when order should be NATIVE
	 * - av_channel_layout_check() returns false
	 * - is_valid() returns false
	 */
	AVChannelLayout channel_layout_;

	int channel_count_;                  ///< Cached channel count from layout

	SampleFormat format_;                ///< Audio sample format

	// Footage-specific parameters (serialized with footage metadata)
	int enabled_; // Using int instead of bool fixes GCC 11 stringop-overflow issue (byte alignment)
	int stream_index_;                   ///< Index in the source file's stream list
	int64_t duration_;                   ///< Stream duration in timebase units
	rational timebase_;                  ///< Timebase for this audio stream
};

}

#endif // LIBOLIVECORE_AUDIOPARAMS_H
