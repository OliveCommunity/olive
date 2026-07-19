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

#ifndef OAK_LIBOLIVECORE_SAMPLEBUFFER_H
#define OAK_LIBOLIVECORE_SAMPLEBUFFER_H

#include <vector>

#include "olive/core/oakcore/samplebuffer.h"
#include "olive/core/render/audioparams.h"
#include "olive/core/util/rational.h"

namespace olive::core
{

/**
 * @brief A buffer of audio samples
 *
 * Consumer-side wrapper over the liboakcore C ABI: the object only holds an
 * opaque OakSampleBuffer handle and forwards every call across the C boundary.
 * The public API is unchanged from the original implementation, except that
 * the getters return AudioParams/size_t by value instead of by const
 * reference.
 *
 * Audio samples in this structure are always stored in PLANAR (separated by
 * channel). This is done to simplify audio rendering code. This replaces the
 * old system of using QByteArrays (containing packed audio) and while
 * SampleBuffer replaces many of those in the rendering/processing side of
 * things, QByteArrays are currently still in use for playback, including
 * reading to and from the cache.
 */
class SampleBuffer {
public:
	SampleBuffer()
		: handle_(oakcore_samplebuffer_create())
	{
	}

	SampleBuffer(const AudioParams &audio_params, const Rational &length)
		: handle_(oakcore_samplebuffer_create_length(audio_params.handle(),
													 length.handle()))
	{
	}

	SampleBuffer(const AudioParams &audio_params, size_t samples_per_channel)
		: handle_(oakcore_samplebuffer_create_samples(audio_params.handle(),
													  samples_per_channel))
	{
	}

	SampleBuffer(const SampleBuffer &rhs)
		: handle_(oakcore_samplebuffer_copy(rhs.handle_))
	{
	}

	SampleBuffer(SampleBuffer &&rhs) noexcept
		: handle_(rhs.handle_)
	{
		rhs.handle_ = nullptr;
	}

	~SampleBuffer()
	{
		oakcore_samplebuffer_free(handle_);
	}

	SampleBuffer &operator=(const SampleBuffer &rhs)
	{
		if (this != &rhs) {
			oakcore_samplebuffer_free(handle_);
			handle_ = oakcore_samplebuffer_copy(rhs.handle_);
		}
		return *this;
	}

	SampleBuffer &operator=(SampleBuffer &&rhs) noexcept
	{
		if (this != &rhs) {
			oakcore_samplebuffer_free(handle_);
			handle_ = rhs.handle_;
			rhs.handle_ = nullptr;
		}
		return *this;
	}

	SampleBuffer rip_channel(int channel) const
	{
		return from_handle(oakcore_samplebuffer_rip_channel(handle_, channel));
	}

	std::vector<float> rip_channel_vector(int channel) const
	{
		const int size = oakcore_samplebuffer_rip_channel_vector(
			handle_, channel, nullptr, 0);
		std::vector<float> v(static_cast<size_t>(size));
		if (size > 0) {
			oakcore_samplebuffer_rip_channel_vector(handle_, channel, v.data(),
													size);
		}
		return v;
	}

	AudioParams audio_params() const
	{
		return AudioParams::from_handle(
			oakcore_samplebuffer_audio_params(handle_));
	}

	void set_audio_params(const AudioParams &params)
	{
		oakcore_samplebuffer_set_audio_params(handle_, params.handle());
	}

	size_t sample_count() const
	{
		return oakcore_samplebuffer_sample_count(handle_);
	}

	void set_sample_count(const size_t &sample_count)
	{
		oakcore_samplebuffer_set_sample_count(handle_, sample_count);
	}

	void set_sample_count(const Rational &length)
	{
		oakcore_samplebuffer_set_sample_count_length(handle_,
													 length.handle());
	}

	float *data(int channel)
	{
		return oakcore_samplebuffer_data(handle_, channel);
	}

	const float *data(int channel) const
	{
		return oakcore_samplebuffer_data(handle_, channel);
	}

	std::vector<float *> to_raw_ptrs()
	{
		std::vector<float *> r(static_cast<size_t>(channel_count()));
		if (!r.empty()) {
			oakcore_samplebuffer_to_raw_ptrs(handle_, r.data());
		}
		return r;
	}

	int channel_count() const
	{
		return oakcore_samplebuffer_channel_count(handle_);
	}

	bool is_allocated() const
	{
		return oakcore_samplebuffer_is_allocated(handle_) != 0;
	}

	void allocate()
	{
		oakcore_samplebuffer_allocate(handle_);
	}

	void destroy()
	{
		oakcore_samplebuffer_destroy(handle_);
	}

	void reverse()
	{
		oakcore_samplebuffer_reverse(handle_);
	}

	void speed(double speed)
	{
		oakcore_samplebuffer_speed(handle_, speed);
	}

	void transform_volume(float f)
	{
		oakcore_samplebuffer_transform_volume(handle_, f);
	}

	void transform_volume_for_channel(int channel, float volume)
	{
		oakcore_samplebuffer_transform_volume_for_channel(handle_, channel,
														  volume);
	}

	static void transform_volume(float f, const SampleBuffer *input,
								 SampleBuffer *output)
	{
		oakcore_samplebuffer_transform_volume_to(f, input->handle_,
												 output->handle_);
	}

	static void transform_volume_for_channel(int channel, float volume,
											 const SampleBuffer *input,
											 SampleBuffer *output)
	{
		oakcore_samplebuffer_transform_volume_for_channel_to(
			channel, volume, input->handle_, output->handle_);
	}

	void transform_volume_for_sample(size_t sample_index, float volume)
	{
		oakcore_samplebuffer_transform_volume_for_sample(handle_, sample_index,
														 volume);
	}

	void transform_volume_for_sample_on_channel(size_t sample_index,
												int channel, float volume)
	{
		oakcore_samplebuffer_transform_volume_for_sample_on_channel(
			handle_, sample_index, channel, volume);
	}

	void clamp()
	{
		oakcore_samplebuffer_clamp(handle_);
	}

	void silence()
	{
		oakcore_samplebuffer_silence(handle_);
	}

	void silence(size_t start_sample, size_t end_sample)
	{
		oakcore_samplebuffer_silence_range(handle_, start_sample, end_sample);
	}

	void silence_bytes(size_t start_byte, size_t end_byte)
	{
		oakcore_samplebuffer_silence_bytes(handle_, start_byte, end_byte);
	}

	void set(int channel, const float *data, size_t sample_offset,
			 size_t sample_length)
	{
		oakcore_samplebuffer_set(handle_, channel, data, sample_offset,
								 sample_length);
	}

	void set(int channel, const float *data, size_t sample_length)
	{
		set(channel, data, 0, sample_length);
	}

	void fast_set(const SampleBuffer &other, int to, int from = -1)
	{
		oakcore_samplebuffer_fast_set(handle_, other.handle_, to, from);
	}

	/**
	 * @brief The wrapped C handle, for cross-type wrappers and direct C API use
	 */
	OakSampleBuffer *handle() const
	{
		return handle_;
	}

	/**
	 * @brief Wraps an owned C handle (takes ownership)
	 */
	static SampleBuffer from_handle(OakSampleBuffer *handle)
	{
		return SampleBuffer(handle);
	}

private:
	explicit SampleBuffer(OakSampleBuffer *handle)
		: handle_(handle)
	{
	}

	OakSampleBuffer *handle_;
};

}

#endif // OAK_LIBOLIVECORE_SAMPLEBUFFER_H
