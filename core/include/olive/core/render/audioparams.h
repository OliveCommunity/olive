/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
  Modifications Copyright (C) 2026 Oak Team

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

#ifndef OAK_LIBOLIVECORE_AUDIOPARAMS_H
#define OAK_LIBOLIVECORE_AUDIOPARAMS_H

#include <cstdint>
#include <vector>

#include "olive/core/oakcore/audioparams.h"

#include "channellayout.h"
#include "sampleformat.h"
#include "../util/rational.h"

namespace olive::core
{

/**
 * @brief Audio parameters class managing audio stream configuration
 *
 * Consumer-side wrapper over the liboakcore C ABI: the object only holds an
 * opaque OakAudioParams handle and forwards every call across the C boundary.
 * The public API is unchanged from the original implementation.
 */
class AudioParams {
public:
	/**
	 * @brief Default constructor creates invalid AudioParams
	 * sample_rate=0, channel_layout empty, format=INVALID
	 */
	AudioParams()
		: handle_(oakcore_audioparams_create_invalid())
	{
	}

	/**
	 * @brief Constructor from channel layout mask
	 * @param sample_rate Audio sample rate
	 * @param channel_layout Channel layout mask (e.g., kChannelLayoutStereo)
	 * @param format Sample format
	 */
	AudioParams(const int &sample_rate, uint64_t channel_layout,
				const SampleFormat &format)
		: handle_(oakcore_audioparams_create(sample_rate, channel_layout, format))
	{
	}

	AudioParams(const AudioParams &rhs)
		: handle_(oakcore_audioparams_copy(rhs.handle_))
	{
	}

	AudioParams(AudioParams &&rhs) noexcept
		: handle_(rhs.handle_)
	{
		rhs.handle_ = nullptr;
	}

	~AudioParams()
	{
		oakcore_audioparams_free(handle_);
	}

	AudioParams &operator=(const AudioParams &rhs)
	{
		if (this != &rhs) {
			oakcore_audioparams_free(handle_);
			handle_ = oakcore_audioparams_copy(rhs.handle_);
		}
		return *this;
	}

	AudioParams &operator=(AudioParams &&rhs) noexcept
	{
		if (this != &rhs) {
			oakcore_audioparams_free(handle_);
			handle_ = rhs.handle_;
			rhs.handle_ = nullptr;
		}
		return *this;
	}

	int sample_rate() const
	{
		return oakcore_audioparams_sample_rate(handle_);
	}

	void set_sample_rate(int sample_rate)
	{
		oakcore_audioparams_set_sample_rate(handle_, sample_rate);
	}

	/**
	 * @brief Channel layout as a 64-bit mask (0 if unspecified)
	 */
	uint64_t channel_layout() const
	{
		return oakcore_audioparams_channel_layout(handle_);
	}

	/**
	 * @brief Set channel layout from mask
	 * @param mask Channel layout mask (e.g., kChannelLayoutStereo)
	 */
	void set_channel_layout(uint64_t mask)
	{
		oakcore_audioparams_set_channel_layout(handle_, mask);
	}

	Rational time_base() const
	{
		return Rational::from_handle(oakcore_audioparams_time_base(handle_));
	}

	void set_time_base(const Rational &timebase)
	{
		oakcore_audioparams_set_time_base(handle_, timebase.handle());
	}

	Rational sample_rate_as_time_base() const
	{
		return Rational::from_handle(
			oakcore_audioparams_sample_rate_as_time_base(handle_));
	}

	SampleFormat format() const
	{
		return SampleFormat(
			static_cast<SampleFormat::Format>(oakcore_audioparams_format(handle_)));
	}

	void set_format(SampleFormat format)
	{
		oakcore_audioparams_set_format(handle_, format);
	}

	bool enabled() const
	{
		return oakcore_audioparams_enabled(handle_) != 0;
	}

	void set_enabled(bool e)
	{
		oakcore_audioparams_set_enabled(handle_, e ? 1 : 0);
	}

	int stream_index() const
	{
		return oakcore_audioparams_stream_index(handle_);
	}

	void set_stream_index(int s)
	{
		oakcore_audioparams_set_stream_index(handle_, s);
	}

	int64_t duration() const
	{
		return oakcore_audioparams_duration(handle_);
	}

	void set_duration(int64_t duration)
	{
		oakcore_audioparams_set_duration(handle_, duration);
	}

	int64_t time_to_bytes(const double &time) const
	{
		return oakcore_audioparams_time_to_bytes(handle_, time);
	}

	int64_t time_to_bytes(const Rational &time) const
	{
		return oakcore_audioparams_time_to_bytes_rational(handle_, time.handle());
	}

	int64_t time_to_bytes_per_channel(const double &time) const
	{
		return oakcore_audioparams_time_to_bytes_per_channel(handle_, time);
	}

	int64_t time_to_bytes_per_channel(const Rational &time) const
	{
		return oakcore_audioparams_time_to_bytes_per_channel_rational(handle_,
																	  time.handle());
	}

	int64_t time_to_samples(const double &time) const
	{
		return oakcore_audioparams_time_to_samples(handle_, time);
	}

	int64_t time_to_samples(const Rational &time) const
	{
		return oakcore_audioparams_time_to_samples_rational(handle_, time.handle());
	}

	int64_t samples_to_bytes(const int64_t &samples) const
	{
		return oakcore_audioparams_samples_to_bytes(handle_, samples);
	}

	int64_t samples_to_bytes_per_channel(const int64_t &samples) const
	{
		return oakcore_audioparams_samples_to_bytes_per_channel(handle_, samples);
	}

	Rational samples_to_time(const int64_t &samples) const
	{
		return Rational::from_handle(
			oakcore_audioparams_samples_to_time(handle_, samples));
	}

	int64_t bytes_to_samples(const int64_t &bytes) const
	{
		return oakcore_audioparams_bytes_to_samples(handle_, bytes);
	}

	Rational bytes_to_time(const int64_t &bytes) const
	{
		return Rational::from_handle(
			oakcore_audioparams_bytes_to_time(handle_, bytes));
	}

	Rational bytes_per_channel_to_time(const int64_t &bytes) const
	{
		return Rational::from_handle(
			oakcore_audioparams_bytes_per_channel_to_time(handle_, bytes));
	}

	int channel_count() const
	{
		return oakcore_audioparams_channel_count(handle_);
	}

	int bytes_per_sample_per_channel() const
	{
		return oakcore_audioparams_bytes_per_sample_per_channel(handle_);
	}

	int bits_per_sample() const
	{
		return oakcore_audioparams_bits_per_sample(handle_);
	}

	bool is_valid() const
	{
		return oakcore_audioparams_is_valid(handle_) != 0;
	}

	bool operator==(const AudioParams &other) const
	{
		return oakcore_audioparams_equals(handle_, other.handle_) != 0;
	}

	bool operator!=(const AudioParams &other) const
	{
		return !(*this == other);
	}

	static const std::vector<uint64_t> k_supported_channel_layouts;
	static const std::vector<int> k_supported_sample_rates;

	/**
	 * @brief The wrapped C handle, for cross-type wrappers and direct C API use
	 */
	OakAudioParams *handle() const
	{
		return handle_;
	}

	/**
	 * @brief Wraps an owned C handle (takes ownership)
	 */
	static AudioParams from_handle(OakAudioParams *handle)
	{
		return AudioParams(handle);
	}

private:
	explicit AudioParams(OakAudioParams *handle)
		: handle_(handle)
	{
	}

	OakAudioParams *handle_;
};

inline const std::vector<uint64_t> AudioParams::k_supported_channel_layouts = [] {
	std::vector<uint64_t> v;
	const int n = oakcore_audioparams_supported_channel_layout_count();
	v.reserve(size_t(n));
	for (int i = 0; i < n; i++) {
		v.push_back(oakcore_audioparams_supported_channel_layout_at(i));
	}
	return v;
}();

inline const std::vector<int> AudioParams::k_supported_sample_rates = [] {
	std::vector<int> v;
	const int n = oakcore_audioparams_supported_sample_rate_count();
	v.reserve(size_t(n));
	for (int i = 0; i < n; i++) {
		v.push_back(oakcore_audioparams_supported_sample_rate_at(i));
	}
	return v;
}();

}

#endif // OAK_LIBOLIVECORE_AUDIOPARAMS_H
