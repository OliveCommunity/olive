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

#include "oakcore/samplebuffer.h"

#include <algorithm>
#include <vector>

#include "render/audioparams.h"
#include "render/samplebuffer.h"
#include "util/rational.h"

namespace
{

olive::core::internal::SampleBuffer *impl(OakSampleBuffer *h)
{
	return reinterpret_cast<olive::core::internal::SampleBuffer *>(h);
}

const olive::core::internal::SampleBuffer *impl(const OakSampleBuffer *h)
{
	return reinterpret_cast<const olive::core::internal::SampleBuffer *>(h);
}

OakSampleBuffer *wrap(olive::core::internal::SampleBuffer *b)
{
	return reinterpret_cast<OakSampleBuffer *>(b);
}

const olive::core::internal::AudioParams *apimpl(const OakAudioParams *h)
{
	return reinterpret_cast<const olive::core::internal::AudioParams *>(h);
}

OakAudioParams *apwrap(olive::core::internal::AudioParams *p)
{
	return reinterpret_cast<OakAudioParams *>(p);
}

const olive::core::internal::Rational *rimpl(const OakRational *h)
{
	return reinterpret_cast<const olive::core::internal::Rational *>(h);
}

} // namespace

extern "C"
{

OakSampleBuffer *oakcore_samplebuffer_create(void)
{
	return wrap(new olive::core::internal::SampleBuffer());
}

OakSampleBuffer *oakcore_samplebuffer_create_length(
	const OakAudioParams *params, const OakRational *length)
{
	return wrap(new olive::core::internal::SampleBuffer(*apimpl(params),
														*rimpl(length)));
}

OakSampleBuffer *oakcore_samplebuffer_create_samples(
	const OakAudioParams *params, size_t samples_per_channel)
{
	return wrap(new olive::core::internal::SampleBuffer(*apimpl(params),
														samples_per_channel));
}

OakSampleBuffer *oakcore_samplebuffer_copy(const OakSampleBuffer *self)
{
	return wrap(new olive::core::internal::SampleBuffer(*impl(self)));
}

void oakcore_samplebuffer_free(OakSampleBuffer *self)
{
	delete impl(self);
}

OakSampleBuffer *oakcore_samplebuffer_rip_channel(const OakSampleBuffer *self,
												  int channel)
{
	return wrap(
		new olive::core::internal::SampleBuffer(impl(self)->rip_channel(channel)));
}

int oakcore_samplebuffer_rip_channel_vector(const OakSampleBuffer *self,
											int channel, float *out,
											int out_size)
{
	const std::vector<float> v = impl(self)->rip_channel_vector(channel);
	if (out && out_size > 0) {
		const size_t n = std::min(v.size(), size_t(out_size));
		std::copy(v.begin(), v.begin() + n, out);
	}
	return int(v.size());
}

OakAudioParams *oakcore_samplebuffer_audio_params(const OakSampleBuffer *self)
{
	return apwrap(
		new olive::core::internal::AudioParams(impl(self)->audio_params()));
}

void oakcore_samplebuffer_set_audio_params(OakSampleBuffer *self,
										   const OakAudioParams *params)
{
	impl(self)->set_audio_params(*apimpl(params));
}

size_t oakcore_samplebuffer_sample_count(const OakSampleBuffer *self)
{
	return impl(self)->sample_count();
}

void oakcore_samplebuffer_set_sample_count(OakSampleBuffer *self,
										   size_t sample_count)
{
	impl(self)->set_sample_count(sample_count);
}

void oakcore_samplebuffer_set_sample_count_length(OakSampleBuffer *self,
												  const OakRational *length)
{
	impl(self)->set_sample_count(*rimpl(length));
}

float *oakcore_samplebuffer_data(OakSampleBuffer *self, int channel)
{
	if (!impl(self)->is_allocated() || channel < 0 ||
		channel >= impl(self)->channel_count()) {
		return nullptr;
	}
	return impl(self)->data(channel);
}

void oakcore_samplebuffer_to_raw_ptrs(OakSampleBuffer *self, float **out)
{
	if (!out) {
		return;
	}
	const std::vector<float *> ptrs = impl(self)->to_raw_ptrs();
	std::copy(ptrs.begin(), ptrs.end(), out);
}

int oakcore_samplebuffer_channel_count(const OakSampleBuffer *self)
{
	return impl(self)->channel_count();
}

int oakcore_samplebuffer_is_allocated(const OakSampleBuffer *self)
{
	return impl(self)->is_allocated() ? 1 : 0;
}

void oakcore_samplebuffer_allocate(OakSampleBuffer *self)
{
	impl(self)->allocate();
}

void oakcore_samplebuffer_destroy(OakSampleBuffer *self)
{
	impl(self)->destroy();
}

void oakcore_samplebuffer_reverse(OakSampleBuffer *self)
{
	impl(self)->reverse();
}

void oakcore_samplebuffer_speed(OakSampleBuffer *self, double speed)
{
	impl(self)->speed(speed);
}

void oakcore_samplebuffer_transform_volume(OakSampleBuffer *self, float f)
{
	impl(self)->transform_volume(f);
}

void oakcore_samplebuffer_transform_volume_for_channel(OakSampleBuffer *self,
													   int channel,
													   float volume)
{
	impl(self)->transform_volume_for_channel(channel, volume);
}

void oakcore_samplebuffer_transform_volume_to(float f,
											  const OakSampleBuffer *input,
											  OakSampleBuffer *output)
{
	olive::core::internal::SampleBuffer::transform_volume(f, impl(input),
														  impl(output));
}

void oakcore_samplebuffer_transform_volume_for_channel_to(
	int channel, float volume, const OakSampleBuffer *input,
	OakSampleBuffer *output)
{
	olive::core::internal::SampleBuffer::transform_volume_for_channel(
		channel, volume, impl(input), impl(output));
}

void oakcore_samplebuffer_transform_volume_for_sample(OakSampleBuffer *self,
													  size_t sample_index,
													  float volume)
{
	impl(self)->transform_volume_for_sample(sample_index, volume);
}

void oakcore_samplebuffer_transform_volume_for_sample_on_channel(
	OakSampleBuffer *self, size_t sample_index, int channel, float volume)
{
	impl(self)->transform_volume_for_sample_on_channel(sample_index, channel,
													   volume);
}

void oakcore_samplebuffer_clamp(OakSampleBuffer *self)
{
	impl(self)->clamp();
}

void oakcore_samplebuffer_silence(OakSampleBuffer *self)
{
	impl(self)->silence();
}

void oakcore_samplebuffer_silence_range(OakSampleBuffer *self,
										size_t start_sample,
										size_t end_sample)
{
	impl(self)->silence(start_sample, end_sample);
}

void oakcore_samplebuffer_silence_bytes(OakSampleBuffer *self,
										size_t start_byte, size_t end_byte)
{
	impl(self)->silence_bytes(start_byte, end_byte);
}

void oakcore_samplebuffer_set(OakSampleBuffer *self, int channel,
							  const float *data, size_t sample_offset,
							  size_t sample_length)
{
	impl(self)->set(channel, data, sample_offset, sample_length);
}

void oakcore_samplebuffer_fast_set(OakSampleBuffer *self,
								   const OakSampleBuffer *other, int to,
								   int from)
{
	impl(self)->fast_set(*impl(other), to, from);
}

} // extern "C"
