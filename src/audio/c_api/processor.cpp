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

#include "audio/processor.h"

#include <cstring>
#include <vector>

#include "audioprocessor.h"
#include "refcounted.h"

using olive::AudioProcessor;
using olive::core::AudioParams;
using olive::core::SampleFormat;

extern "C" OakAudioProcessor oakaudio_processor_init(void)
{
	return oakaudio::make_handle_in_place<OakAudioProcessor, AudioProcessor>();
}

extern "C" void oakaudio_processor_free(OakAudioProcessor *self)
{
	oakaudio::free_handle(self);
}

extern "C" int oakaudio_processor_open(OakAudioProcessor self,
		int in_rate, uint64_t in_layout, int in_format,
		int out_rate, uint64_t out_layout, int out_format, double speed)
{
	AudioProcessor *p = oakaudio::handle_impl<AudioProcessor>(self.ctx);
	if (!p) {
		return OAKAUDIO_E_INVALID;
	}
	if (p->is_open()) {
		return OAKAUDIO_E_STATE;
	}
	if (in_rate <= 0 || out_rate <= 0 || speed <= 0.0) {
		return OAKAUDIO_E_INVALID;
	}

	// The C ABI delivers planar float output only; force the output format
	// stage to f32p (see OAKAUDIO_PROCESSOR_OUTPUT_FORMAT).
	if (out_format != OAKAUDIO_PROCESSOR_OUTPUT_FORMAT) {
		return OAKAUDIO_E_INVALID;
	}

	const AudioParams from(in_rate, in_layout,
						SampleFormat(SampleFormat::Format(in_format)));
	const AudioParams to(out_rate, out_layout,
					  SampleFormat(SampleFormat::Format(out_format)));

	return p->open(from, to, speed) ? OAKAUDIO_OK : OAKAUDIO_E_FAILED;
}

extern "C" int oakaudio_processor_close(OakAudioProcessor self)
{
	AudioProcessor *p = oakaudio::handle_impl<AudioProcessor>(self.ctx);
	if (!p) {
		return OAKAUDIO_E_INVALID;
	}
	p->close();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_processor_is_open(OakAudioProcessor self)
{
	AudioProcessor *p = oakaudio::handle_impl<AudioProcessor>(self.ctx);
	if (!p) {
		return OAKAUDIO_E_INVALID;
	}
	return p->is_open() ? 1 : 0;
}

extern "C" int oakaudio_processor_convert(OakAudioProcessor self,
		const float *const *in_planar, int in_frame_count,
		float *const *out_planar, int out_capacity_frames)
{
	AudioProcessor *p = oakaudio::handle_impl<AudioProcessor>(self.ctx);
	if (!p) {
		return OAKAUDIO_E_INVALID;
	}
	if (!p->is_open()) {
		return OAKAUDIO_E_STATE;
	}
	if (in_frame_count < 0 || out_capacity_frames < 0 ||
		(in_frame_count > 0 && !in_planar)) {
		return OAKAUDIO_E_INVALID;
	}

	const int channels = p->to().channel_count();
	if (channels <= 0) {
		return OAKAUDIO_E_STATE;
	}

	AudioProcessor::Buffer buf;
	int r = p->convert(const_cast<float **>(in_planar), in_frame_count,
				   out_planar ? &buf : nullptr);
	if (r < 0) {
		return OAKAUDIO_E_FAILED;
	}

	if (!out_planar) {
		return 0;
	}

	// Output is planar f32 (enforced by open()); each buffer entry is one
	// channel's float plane.
	const int out_frames = buf.empty() ? 0 :
						 int(buf[0].size() / sizeof(float));
	const int frames = std::min(out_frames, out_capacity_frames);
	for (int ch = 0; ch < channels && ch < int(buf.size()); ch++) {
		if (out_planar[ch]) {
			memcpy(out_planar[ch], buf[size_t(ch)].data(),
				   size_t(frames) * sizeof(float));
		}
	}
	return frames;
}

extern "C" int oakaudio_processor_flush(OakAudioProcessor self)
{
	AudioProcessor *p = oakaudio::handle_impl<AudioProcessor>(self.ctx);
	if (!p) {
		return OAKAUDIO_E_INVALID;
	}
	if (!p->is_open()) {
		return OAKAUDIO_E_STATE;
	}
	p->flush();
	return OAKAUDIO_OK;
}
