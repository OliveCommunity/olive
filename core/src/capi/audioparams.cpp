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

#include "oakcore/audioparams.h"

#include "render/audioparams.h"

namespace
{

olive::core::internal::AudioParams *impl(OakAudioParams *h)
{
	return reinterpret_cast<olive::core::internal::AudioParams *>(h);
}

const olive::core::internal::AudioParams *impl(const OakAudioParams *h)
{
	return reinterpret_cast<const olive::core::internal::AudioParams *>(h);
}

OakAudioParams *wrap(olive::core::internal::AudioParams *p)
{
	return reinterpret_cast<OakAudioParams *>(p);
}

const olive::core::internal::Rational *impl(const OakRational *h)
{
	return reinterpret_cast<const olive::core::internal::Rational *>(h);
}

OakRational *wrap(olive::core::internal::Rational *r)
{
	return reinterpret_cast<OakRational *>(r);
}

olive::core::SampleFormat to_format(int f)
{
	return olive::core::SampleFormat(
		static_cast<olive::core::SampleFormat::Format>(f));
}

// The supported channel layouts / sample rates as constant data. These
// mirror AudioParams::k_supported_channel_layouts /
// k_supported_sample_rates (src/render/audioparams.cpp) and must be kept in
// sync with them. The C API serves these instead of reading the
// implementation's std::vector statics directly so that the functions are
// safe to call during a consumer's static initialization even when
// liboakcore is linked statically (cross-TU static init order is
// unspecified; the implementation's vectors may not be constructed yet).
constexpr uint64_t k_supported_channel_layouts[] = {
	olive::core::k_channel_layout_mono,
	olive::core::k_channel_layout_stereo,
	olive::core::k_channel_layout2_1,
	olive::core::k_channel_layout5_point1,
	olive::core::k_channel_layout7_point1,
};

constexpr int k_supported_sample_rates[] = {
	8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000,
};

template <typename T, size_t N>
T at_or_zero(const T (&arr)[N], int index)
{
	if (index < 0 || size_t(index) >= N) {
		return 0;
	}
	return arr[index];
}

} // namespace

extern "C"
{

OakAudioParams *oakcore_audioparams_create(int sample_rate,
										   uint64_t channel_layout, int format)
{
	return wrap(new olive::core::internal::AudioParams(
		sample_rate, channel_layout, to_format(format)));
}

OakAudioParams *oakcore_audioparams_create_invalid(void)
{
	return wrap(new olive::core::internal::AudioParams());
}

OakAudioParams *oakcore_audioparams_copy(const OakAudioParams *self)
{
	return wrap(new olive::core::internal::AudioParams(*impl(self)));
}

void oakcore_audioparams_free(OakAudioParams *self)
{
	delete impl(self);
}

int oakcore_audioparams_sample_rate(const OakAudioParams *self)
{
	return impl(self)->sample_rate();
}

void oakcore_audioparams_set_sample_rate(OakAudioParams *self, int sample_rate)
{
	impl(self)->set_sample_rate(sample_rate);
}

uint64_t oakcore_audioparams_channel_layout(const OakAudioParams *self)
{
	return impl(self)->channel_layout();
}

void oakcore_audioparams_set_channel_layout(OakAudioParams *self, uint64_t mask)
{
	impl(self)->set_channel_layout(mask);
}

OakRational *oakcore_audioparams_time_base(const OakAudioParams *self)
{
	return wrap(new olive::core::internal::Rational(impl(self)->time_base()));
}

void oakcore_audioparams_set_time_base(OakAudioParams *self,
									   const OakRational *timebase)
{
	impl(self)->set_time_base(*impl(timebase));
}

OakRational *oakcore_audioparams_sample_rate_as_time_base(
	const OakAudioParams *self)
{
	return wrap(
		new olive::core::internal::Rational(impl(self)->sample_rate_as_time_base()));
}

int oakcore_audioparams_format(const OakAudioParams *self)
{
	return int(impl(self)->format());
}

void oakcore_audioparams_set_format(OakAudioParams *self, int format)
{
	impl(self)->set_format(to_format(format));
}

int oakcore_audioparams_enabled(const OakAudioParams *self)
{
	return impl(self)->enabled() ? 1 : 0;
}

void oakcore_audioparams_set_enabled(OakAudioParams *self, int enabled)
{
	impl(self)->set_enabled(enabled != 0);
}

int oakcore_audioparams_stream_index(const OakAudioParams *self)
{
	return impl(self)->stream_index();
}

void oakcore_audioparams_set_stream_index(OakAudioParams *self, int stream_index)
{
	impl(self)->set_stream_index(stream_index);
}

int64_t oakcore_audioparams_duration(const OakAudioParams *self)
{
	return impl(self)->duration();
}

void oakcore_audioparams_set_duration(OakAudioParams *self, int64_t duration)
{
	impl(self)->set_duration(duration);
}

int64_t oakcore_audioparams_time_to_bytes(const OakAudioParams *self, double time)
{
	return impl(self)->time_to_bytes(time);
}

int64_t oakcore_audioparams_time_to_bytes_rational(const OakAudioParams *self,
												   const OakRational *time)
{
	return impl(self)->time_to_bytes(*impl(time));
}

int64_t oakcore_audioparams_time_to_bytes_per_channel(const OakAudioParams *self,
													  double time)
{
	return impl(self)->time_to_bytes_per_channel(time);
}

int64_t oakcore_audioparams_time_to_bytes_per_channel_rational(
	const OakAudioParams *self, const OakRational *time)
{
	return impl(self)->time_to_bytes_per_channel(*impl(time));
}

int64_t oakcore_audioparams_time_to_samples(const OakAudioParams *self,
											double time)
{
	return impl(self)->time_to_samples(time);
}

int64_t oakcore_audioparams_time_to_samples_rational(const OakAudioParams *self,
													 const OakRational *time)
{
	return impl(self)->time_to_samples(*impl(time));
}

int64_t oakcore_audioparams_samples_to_bytes(const OakAudioParams *self,
											 int64_t samples)
{
	return impl(self)->samples_to_bytes(samples);
}

int64_t oakcore_audioparams_samples_to_bytes_per_channel(
	const OakAudioParams *self, int64_t samples)
{
	return impl(self)->samples_to_bytes_per_channel(samples);
}

OakRational *oakcore_audioparams_samples_to_time(const OakAudioParams *self,
												 int64_t samples)
{
	return wrap(
		new olive::core::internal::Rational(impl(self)->samples_to_time(samples)));
}

int64_t oakcore_audioparams_bytes_to_samples(const OakAudioParams *self,
											 int64_t bytes)
{
	return impl(self)->bytes_to_samples(bytes);
}

OakRational *oakcore_audioparams_bytes_to_time(const OakAudioParams *self,
											   int64_t bytes)
{
	return wrap(
		new olive::core::internal::Rational(impl(self)->bytes_to_time(bytes)));
}

OakRational *oakcore_audioparams_bytes_per_channel_to_time(
	const OakAudioParams *self, int64_t bytes)
{
	return wrap(new olive::core::internal::Rational(
		impl(self)->bytes_per_channel_to_time(bytes)));
}

int oakcore_audioparams_channel_count(const OakAudioParams *self)
{
	return impl(self)->channel_count();
}

int oakcore_audioparams_bytes_per_sample_per_channel(const OakAudioParams *self)
{
	return impl(self)->bytes_per_sample_per_channel();
}

int oakcore_audioparams_bits_per_sample(const OakAudioParams *self)
{
	return impl(self)->bits_per_sample();
}

int oakcore_audioparams_is_valid(const OakAudioParams *self)
{
	return impl(self)->is_valid() ? 1 : 0;
}

int oakcore_audioparams_equals(const OakAudioParams *self,
							   const OakAudioParams *other)
{
	return (*impl(self) == *impl(other)) ? 1 : 0;
}

int oakcore_audioparams_supported_channel_layout_count(void)
{
	return int(sizeof(k_supported_channel_layouts) /
			   sizeof(k_supported_channel_layouts[0]));
}

uint64_t oakcore_audioparams_supported_channel_layout_at(int index)
{
	return at_or_zero(k_supported_channel_layouts, index);
}

int oakcore_audioparams_supported_sample_rate_count(void)
{
	return int(sizeof(k_supported_sample_rates) /
			   sizeof(k_supported_sample_rates[0]));
}

int oakcore_audioparams_supported_sample_rate_at(int index)
{
	return at_or_zero(k_supported_sample_rates, index);
}

} // extern "C"
