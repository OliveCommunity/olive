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

#include "audiomanager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef PA_HAS_JACK
#include <pa_jack.h>
#endif

#include "configbridge.h"

namespace olive
{

AudioManager *AudioManager::instance_ = nullptr;

void AudioManager::create_instance()
{
	if (instance_ == nullptr) {
		instance_ = new AudioManager();
	}
}

void AudioManager::destroy_instance()
{
	delete instance_;
	instance_ = nullptr;
}

AudioManager *AudioManager::instance()
{
	return instance_;
}

void AudioManager::set_output_notify_interval(int64_t n)
{
	output_buffer_->set_notify_interval(n);
}

void AudioManager::set_output_notify_callback(std::function<void()> callback)
{
	output_buffer_->set_notify_callback(std::move(callback));
}

int output_callback(const void *input, void *output, unsigned long frame_count,
					   const PaStreamCallbackTimeInfo *time_info,
					   PaStreamCallbackFlags status_flags, void *user_data)
{
	(void) input;
	(void) time_info;
	(void) status_flags;

	PreviewAudioDevice *device = static_cast<PreviewAudioDevice *>(user_data);

	int64_t max_read = int64_t(frame_count) * device->bytes_per_frame();
	int64_t read_count =
		device->read(reinterpret_cast<char *>(output), max_read);
	if (read_count < max_read) {
		memset(reinterpret_cast<uint8_t *>(output) + read_count, 0,
			   size_t(max_read - read_count));
	}

	// Count all frames leaving the device (including zero-filled underrun
	// frames) so this can serve as the playback master clock
	device->add_output_frames(frame_count);

	return paContinue;
}

int input_callback(const void *input, void *output, unsigned long frame_count,
					  const PaStreamCallbackTimeInfo *time_info,
					  PaStreamCallbackFlags status_flags, void *user_data)
{
	(void) output;
	(void) time_info;
	(void) status_flags;

	// The oakcodec encoder write path accepts interleaved float32 only; the
	// input stream is opened with paFloat32 (see start_recording()).
	OakEncoder *encoder = static_cast<OakEncoder *>(user_data);

	oakcodec_encoder_write_audio(*encoder,
							 reinterpret_cast<const float *>(input),
							 int(frame_count));

	return paContinue;
}

bool AudioManager::push_to_output(const core::AudioParams &params,
								  const char *samples, int64_t samples_size,
								  std::string *error)
{
	if (output_device_ == paNoDevice) {
		if (error)
			*error = "No output device is set";
		return false;
	}

	if (output_params_ != params || output_stream_ == nullptr) {
		output_params_ = params;

		close_output_stream();

		PaStreamParameters p = get_port_audio_params(params, output_device_);

		// 0 = let PortAudio choose the buffer size
		const unsigned long frames_per_buffer =
			(unsigned long) audio_config::output_buffer_size();

		PaError r = Pa_OpenStream(&output_stream_, nullptr, &p,
								  output_params_.sample_rate(),
								  frames_per_buffer, paNoFlag, output_callback,
								  output_buffer_);
		if (r != paNoError) {
			// Unhandled error
			fprintf(stderr,
					"AudioManager::push_to_output: Pa_OpenStream failed: %s\n",
					Pa_GetErrorText(r));
			if (error)
				*error = Pa_GetErrorText(r);
			return false;
		}

		output_buffer_->set_bytes_per_frame(output_params_.samples_to_bytes(1));
	}

	output_buffer_->write(samples, samples_size);

	if (!Pa_IsStreamActive(output_stream_)) {
		PaError r = Pa_StartStream(output_stream_);
		if (r != paNoError) {
			fprintf(stderr,
					"AudioManager::push_to_output: Pa_StartStream returned "
					"%d %s\n",
					r, Pa_GetErrorText(r));
		}
	}

	return true;
}

void AudioManager::clear_buffered_output()
{
	output_buffer_->clear();
}

double AudioManager::seconds() const
{
	if (!output_stream_ || !Pa_IsStreamActive(output_stream_)) {
		return -1.0;
	}

	double seconds = double(output_buffer_->output_frames_consumed()) /
					 double(output_params_.sample_rate());

	// Compensate for output latency so the clock reflects what is audible
	if (const PaStreamInfo *info = Pa_GetStreamInfo(output_stream_)) {
		seconds -= info->outputLatency;
	}

	return std::max(0.0, seconds);
}

void AudioManager::reset_output_clock()
{
	output_buffer_->reset_output_frames();
}

PaSampleFormat AudioManager::get_port_audio_sample_format(core::SampleFormat fmt)
{
	switch (fmt) {
	case core::SampleFormat::u8:
	case core::SampleFormat::u8_p:
		return paUInt8;
	case core::SampleFormat::s16:
	case core::SampleFormat::s16_p:
		return paInt16;
	case core::SampleFormat::s32:
	case core::SampleFormat::s32_p:
		return paInt32;
	case core::SampleFormat::f32:
	case core::SampleFormat::f32_p:
		return paFloat32;
	case core::SampleFormat::s64:
	case core::SampleFormat::s64_p:
	case core::SampleFormat::f64:
	case core::SampleFormat::f64_p:
	case core::SampleFormat::invalid:
	case core::SampleFormat::count:
		break;
	}

	return 0;
}

void AudioManager::close_output_stream()
{
	if (output_stream_) {
		if (Pa_IsStreamActive(output_stream_)) {
			stop_output();
		}
		Pa_CloseStream(output_stream_);
		output_stream_ = nullptr;
	}
}

void AudioManager::stop_output()
{
	// Abort the stream so playback stops immediately
	if (output_stream_) {
		Pa_AbortStream(output_stream_);
		clear_buffered_output();
	}
}

void AudioManager::set_output_device(PaDeviceIndex device)
{
	if (device == paNoDevice) {
		fprintf(stderr, "AudioManager: no output device found\n");
	} else if (device < 0 || device >= Pa_GetDeviceCount()) {
		fprintf(stderr, "AudioManager: invalid output audio device index: "
						"%d\n",
				device);
	} else {
		fprintf(stderr, "AudioManager: setting output audio device to %s\n",
				Pa_GetDeviceInfo(device)->name);
	}

	output_device_ = device;

	close_output_stream();
}

void AudioManager::set_input_device(PaDeviceIndex device)
{
	if (device == paNoDevice) {
		fprintf(stderr, "AudioManager: no input device found\n");
	} else if (device < 0 || device >= Pa_GetDeviceCount()) {
		fprintf(stderr, "AudioManager: invalid input audio device index: %d\n",
				device);
	} else {
		fprintf(stderr, "AudioManager: setting input audio device to %s\n",
				Pa_GetDeviceInfo(device)->name);
	}

	input_device_ = device;
}

void AudioManager::hard_reset()
{
	close_output_stream();
	Pa_Terminate();
	Pa_Initialize();
}

bool AudioManager::start_recording(const oakcodec_encoding_params &params,
								  std::string *error_str)
{
	if (input_device_ == paNoDevice) {
		return false;
	}

	input_encoder_ = oakcodec_encoder_init(&params);
	if (!input_encoder_.ctx || oakcodec_encoder_open(input_encoder_) != 0) {
		fprintf(stderr,
				"AudioManager: failed to open encoder for recording\n");
		if (input_encoder_.ctx) {
			char buf[512];
			if (oakcodec_encoder_last_error(input_encoder_, buf,
											int(sizeof(buf))) > 0 &&
				error_str) {
				*error_str = buf;
			}
			oakcodec_encoder_free(&input_encoder_);
		}
		return false;
	}

	// The oakcodec encoder write path takes interleaved float32; capture in
	// that format regardless of the target encoding sample format.
	core::AudioParams stream_params(params.audio_sample_rate,
								 params.audio_channel_layout,
								 core::SampleFormat::f32);
	PaStreamParameters p =
		get_port_audio_params(stream_params, input_device_);

	PaError r = Pa_OpenStream(&input_stream_, &p, nullptr,
							  params.audio_sample_rate,
							  paFramesPerBufferUnspecified, paNoFlag,
							  input_callback, &input_encoder_);
	if (r == paNoError) {
		r = Pa_StartStream(input_stream_);
		if (r == paNoError) {
			return true;
		}
	}

	if (error_str) {
		*error_str = Pa_GetErrorText(r);
	}

	stop_recording();
	return false;
}

void AudioManager::stop_recording()
{
	if (input_stream_) {
		if (Pa_IsStreamActive(input_stream_)) {
			Pa_StopStream(input_stream_);
		}
		Pa_CloseStream(input_stream_);

		input_stream_ = nullptr;
	}

	if (input_encoder_.ctx) {
		oakcodec_encoder_flush(input_encoder_);
		oakcodec_encoder_free(&input_encoder_);
	}
}

#ifdef __linux__
static bool str_contains_ci(const char *haystack, const char *needle)
{
	const size_t needle_len = strlen(needle);
	if (!needle_len) {
		return true;
	}
	for (const char *p = haystack; *p; p++) {
		if (strncasecmp(p, needle, needle_len) == 0) {
			return true;
		}
	}
	return false;
}

static bool is_preferred_linux_audio_host_api(const PaHostApiInfo *info)
{
	if (!info) {
		return false;
	}

	return str_contains_ci(info->name, "PipeWire") ||
		   str_contains_ci(info->name, "JACK") ||
		   str_contains_ci(info->name, "PulseAudio");
}

static PaDeviceIndex get_preferred_linux_audio_device(bool is_output_device)
{
	// Prefer sound servers that provide mixing and desktop integration
	// (PipeWire, JACK, PulseAudio) over plain ALSA defaults, which often
	// fail to share the device on modern Linux desktops.
	static const char *const preferred_host_apis[] = {
		"PipeWire",
		"JACK",
		"PulseAudio",
	};

	for (const char *preferred : preferred_host_apis) {
		for (PaHostApiIndex i = 0, end = Pa_GetHostApiCount(); i < end; i++) {
			const PaHostApiInfo *info = Pa_GetHostApiInfo(i);
			if (!info) {
				continue;
			}

			if (str_contains_ci(info->name, preferred)) {
				PaDeviceIndex dev = is_output_device ? info->defaultOutputDevice :
												 info->defaultInputDevice;
				if (dev != paNoDevice) {
					return dev;
				}
			}
		}
	}

	return is_output_device ? Pa_GetDefaultOutputDevice() :
							  Pa_GetDefaultInputDevice();
}
#endif

PaDeviceIndex AudioManager::find_config_device_by_name(bool is_output_device)
{
	return find_device_by_name(
		audio_config::device_name(is_output_device), is_output_device);
}

PaDeviceIndex AudioManager::find_device_by_name(const std::string &s,
											 bool is_output_device)
{
	PaDeviceIndex exact_match = paNoDevice;

	if (!s.empty()) {
		for (PaDeviceIndex i = 0, end = Pa_GetDeviceCount(); i < end; i++) {
			const PaDeviceInfo *device = Pa_GetDeviceInfo(i);
			if (!device) {
				continue;
			}

			if (((is_output_device && device->maxOutputChannels) ||
				 (!is_output_device && device->maxInputChannels)) &&
				s == device->name) {
				exact_match = i;
				break;
			}
		}
	}

#ifdef __linux__
	// Even if the user/config picked a device by name, upgrade to a preferred
	// host API (PipeWire/JACK/PulseAudio) when one is available. This avoids
	// getting stuck on an ALSA device that cannot share the hardware.
	if (exact_match != paNoDevice) {
		const PaDeviceInfo *matched_info = Pa_GetDeviceInfo(exact_match);
		if (matched_info) {
			const PaHostApiInfo *host_api =
				Pa_GetHostApiInfo(matched_info->hostApi);
			if (is_preferred_linux_audio_host_api(host_api)) {
				// Keep an explicit choice that already uses a preferred API.
				return exact_match;
			}

			// Upgrade a non-preferred (e.g. ALSA) match to a preferred backend
			// when one is available.
			PaDeviceIndex preferred =
				get_preferred_linux_audio_device(is_output_device);
			if (preferred != paNoDevice) {
				return preferred;
			}

			// No preferred backend available; keep the saved device.
			return exact_match;
		}
	}

	return get_preferred_linux_audio_device(is_output_device);
#else
	if (exact_match != paNoDevice) {
		return exact_match;
	}

	return is_output_device ? Pa_GetDefaultOutputDevice() :
							  Pa_GetDefaultInputDevice();
#endif
}

PaStreamParameters AudioManager::get_port_audio_params(const core::AudioParams &params,
													PaDeviceIndex device)
{
	PaStreamParameters p;

	p.channelCount = params.channel_count();
	p.device = device;
	p.hostApiSpecificStreamInfo = nullptr;
	p.sampleFormat = get_port_audio_sample_format(params.format());

	if (device >= 0 && device < Pa_GetDeviceCount()) {
		p.suggestedLatency = Pa_GetDeviceInfo(device)->defaultLowOutputLatency;
	} else {
		p.suggestedLatency = 0;
	}

	return p;
}

AudioManager::AudioManager()
	: output_stream_(nullptr)
	, input_stream_(nullptr)
{
	input_encoder_.ctx = nullptr;
	input_encoder_.addref = nullptr;
	input_encoder_.release = nullptr;
	input_encoder_.abi_version = 0;

#ifdef PA_HAS_JACK
	// PortAudio doesn't do a strcpy, so we need a const char that's readily accessible
	PaJack_SetClientName("Oak Video Editor");
#endif

	Pa_Initialize();

	// Get device from config
	PaDeviceIndex output_device = find_config_device_by_name(true);
	PaDeviceIndex input_device = find_config_device_by_name(false);

	set_output_device(output_device);
	set_input_device(input_device);

	output_buffer_ = new PreviewAudioDevice();
}

AudioManager::~AudioManager()
{
	close_output_stream();

	delete output_buffer_;

	Pa_Terminate();
}

}
