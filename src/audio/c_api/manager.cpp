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

#include "audio/manager.h"

#include <cstring>

#include "audiomanager.h"

using olive::AudioManager;
using olive::core::AudioParams;
using olive::core::SampleFormat;

namespace
{

// Singleton semantics (mirrors oakcommon's OakCurrent): the ctx points to
// the process-wide instance, so addref/release never destroy anything.
void singleton_addref(void *ctx)
{
	(void) ctx;
}

void singleton_release(void *ctx)
{
	(void) ctx;
}

OakAudioManager wrap(AudioManager *m)
{
	OakAudioManager h = {};
	h.ctx = m;
	h.addref = &singleton_addref;
	h.release = &singleton_release;
	h.abi_version = OAKAUDIO_ABI_VERSION;
	return h;
}

AudioManager *impl(OakAudioManager self)
{
	return static_cast<AudioManager *>(self.ctx);
}

int write_error(const std::string &s, char *buf, int buf_size)
{
	if (buf && buf_size > 0) {
		const int n = std::min(int(s.size()), buf_size - 1);
		std::memcpy(buf, s.data(), size_t(n));
		buf[n] = '\0';
	}
	return int(s.size()) + 1;
}

} // namespace

extern "C" int oakaudio_manager_create_instance(void)
{
	if (!AudioManager::instance()) {
		try {
			AudioManager::create_instance();
		} catch (...) {
			return OAKAUDIO_E_NOMEM;
		}
	}
	return AudioManager::instance() ? OAKAUDIO_OK : OAKAUDIO_E_NOMEM;
}

extern "C" void oakaudio_manager_destroy_instance(void)
{
	AudioManager::destroy_instance();
}

extern "C" OakAudioManager oakaudio_manager_instance(void)
{
	return wrap(AudioManager::instance());
}

extern "C" void oakaudio_manager_free(OakAudioManager *self)
{
	// Singleton: releasing never destroys; just clear the caller's copy.
	if (self) {
		self->ctx = nullptr;
	}
}

extern "C" int oakaudio_manager_set_output_notify_interval(
		OakAudioManager self, int64_t bytes)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	if (bytes < 0) {
		return OAKAUDIO_E_INVALID;
	}
	m->set_output_notify_interval(bytes);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_push_to_output(OakAudioManager self,
		int rate, uint64_t layout, int format,
		const char *samples, int64_t samples_size,
		char *error_buf, int error_buf_size)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	if (rate <= 0 || !samples || samples_size < 0) {
		return OAKAUDIO_E_INVALID;
	}

	const AudioParams params(rate, layout,
						  SampleFormat(SampleFormat::Format(format)));
	std::string error;
	if (!m->push_to_output(params, samples, samples_size, &error)) {
		if (error_buf && error_buf_size > 0) {
			write_error(error, error_buf, error_buf_size);
		}
		return OAKAUDIO_E_FAILED;
	}
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_clear_buffered_output(OakAudioManager self)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	m->clear_buffered_output();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_stop_output(OakAudioManager self)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	m->stop_output();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_seconds(OakAudioManager self, double *out)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	if (!out) {
		return OAKAUDIO_E_INVALID;
	}
	*out = m->seconds();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_reset_output_clock(OakAudioManager self)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	m->reset_output_clock();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_get_output_device(OakAudioManager self)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	return int(m->get_output_device());
}

extern "C" int oakaudio_manager_set_output_device(OakAudioManager self,
		int device)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	m->set_output_device(PaDeviceIndex(device));
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_get_input_device(OakAudioManager self)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	return int(m->get_input_device());
}

extern "C" int oakaudio_manager_set_input_device(OakAudioManager self,
		int device)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	m->set_input_device(PaDeviceIndex(device));
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_hard_reset(OakAudioManager self)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	m->hard_reset();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_start_recording(OakAudioManager self,
		const oakcodec_encoding_params *params,
		char *error_buf, int error_buf_size)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	if (!params || !params->audio_enabled) {
		return OAKAUDIO_E_INVALID;
	}

	std::string error;
	if (!m->start_recording(*params, &error)) {
		if (error_buf && error_buf_size > 0) {
			write_error(error, error_buf, error_buf_size);
		}
		return OAKAUDIO_E_FAILED;
	}
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_stop_recording(OakAudioManager self)
{
	AudioManager *m = impl(self);
	if (!m) {
		return OAKAUDIO_E_STATE;
	}
	m->stop_recording();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_manager_find_config_device_by_name_s(
		int is_output_device)
{
	return int(AudioManager::find_config_device_by_name(is_output_device != 0));
}

extern "C" int oakaudio_manager_find_device_by_name_s(const char *name,
		int is_output_device)
{
	if (!name) {
		return OAKAUDIO_E_INVALID;
	}
	return int(AudioManager::find_device_by_name(name, is_output_device != 0));
}
