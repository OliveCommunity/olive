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

#ifdef PA_HAS_JACK
#include <pa_jack.h>
#endif


#include "config/config.h"

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

void AudioManager::set_output_notify_interval(int n)
{
	output_buffer_->set_notify_interval(n);
}

int output_callback(const void *input, void *output, unsigned long frame_count,
				   const PaStreamCallbackTimeInfo *time_info,
				   PaStreamCallbackFlags status_flags, void *user_data)
{
	PreviewAudioDevice *device = static_cast<PreviewAudioDevice *>(user_data);

	qint64 max_read = frame_count * device->bytes_per_frame();
	qint64 read_count =
		device->read(reinterpret_cast<char *>(output), max_read);
	if (read_count < max_read) {
		memset(reinterpret_cast<uint8_t *>(output) + read_count, 0,
			   max_read - read_count);
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
	FFmpegEncoder *f = static_cast<FFmpegEncoder *>(user_data);

	AudioParams our_params = f->params().audio_params();
	our_params.set_format(
		f->params().audio_params().format().to_packed_equivalent());

	f->write_audio_data(our_params, reinterpret_cast<const uint8_t **>(&input),
					  frame_count);

	return paContinue;
}

bool AudioManager::push_to_output(const AudioParams &params,
								const QByteArray &samples, QString *error)
{
	if (output_device_ == paNoDevice) {
		if (error)
			*error = tr("No output device is set");
		return false;
	}

	if (output_params_ != params || output_stream_ == nullptr) {
		output_params_ = params;

		close_output_stream();

		PaStreamParameters p = get_port_audio_params(params, output_device_);

		// 0 = let PortAudio choose the buffer size
		const unsigned long frames_per_buffer =
			OAK_CONFIG("AudioOutputBufferSize").toUInt();

		PaError r = Pa_OpenStream(&output_stream_, nullptr, &p,
								  output_params_.sample_rate(),
								  frames_per_buffer, paNoFlag, output_callback,
								  output_buffer_);
		if (r != paNoError) {
			// Unhandled error
			//qCritical() << "Failed to open output stream:" << Pa_GetErrorText(r);
			qCritical() << "AudioManager::PushToOutput: Pa_OpenStream failed:"
					   << Pa_GetErrorText(r);
			if (error)
				*error = Pa_GetErrorText(r);
			return false;
		}
		qDebug() << "AudioManager::PushToOutput: opened stream with"
				 << params.channel_count() << "channels";

		output_buffer_->set_bytes_per_frame(output_params_.samples_to_bytes(1));
	}

	output_buffer_->write(samples);

	if (!Pa_IsStreamActive(output_stream_)) {
		PaError r = Pa_StartStream(output_stream_);
		qDebug() << "AudioManager::PushToOutput: Pa_StartStream returned"
				 << r << Pa_GetErrorText(r);
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

	return qMax(0.0, seconds);
}

void AudioManager::reset_output_clock()
{
	output_buffer_->reset_output_frames();
}

PaSampleFormat AudioManager::get_port_audio_sample_format(SampleFormat fmt)
{
	switch (fmt) {
	case SampleFormat::u8:
	case SampleFormat::u8_p:
		return paUInt8;
	case SampleFormat::s16:
	case SampleFormat::s16_p:
		return paInt16;
	case SampleFormat::s32:
	case SampleFormat::s32_p:
		return paInt32;
	case SampleFormat::f32:
	case SampleFormat::f32_p:
		return paFloat32;
	case SampleFormat::s64:
	case SampleFormat::s64_p:
	case SampleFormat::f64:
	case SampleFormat::f64_p:
	case SampleFormat::invalid:
	case SampleFormat::count:
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
		qInfo() << "No output device found";
	} else if (device < 0 || device >= Pa_GetDeviceCount()) {
		qWarning() << "Invalid output audio device index:" << device;
	} else {
		qInfo() << "Setting output audio device to"
				<< Pa_GetDeviceInfo(device)->name;
	}

	output_device_ = device;

	close_output_stream();

	emit output_params_changed();
}

void AudioManager::set_input_device(PaDeviceIndex device)
{
	if (device == paNoDevice) {
		qInfo() << "No input device found";
	} else if (device < 0 || device >= Pa_GetDeviceCount()) {
		qWarning() << "Invalid input audio device index:" << device;
	} else {
		qInfo() << "Setting input audio device to"
				<< Pa_GetDeviceInfo(device)->name;
	}

	input_device_ = device;
}

void AudioManager::hard_reset()
{
	close_output_stream();
	Pa_Terminate();
	Pa_Initialize();
}

bool AudioManager::start_recording(const EncodingParams &params,
								  QString *error_str)
{
	if (input_device_ == paNoDevice) {
		return false;
	}

	input_encoder_ = new FFmpegEncoder(params);
	if (!input_encoder_->open()) {
		qCritical() << "Failed to open encoder for recording";
		return false;
	}

	PaStreamParameters p =
		get_port_audio_params(params.audio_params(), input_device_);

	PaError r = Pa_OpenStream(&input_stream_, &p, nullptr,
							  params.audio_params().sample_rate(),
							  paFramesPerBufferUnspecified, paNoFlag,
							  input_callback, input_encoder_);
	if (r == paNoError) {
		//const PaStreamInfo* info = Pa_GetStreamInfo(input_stream_);
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

	if (input_encoder_) {
		input_encoder_->close();
		delete input_encoder_;
		input_encoder_ = nullptr;
	}
}

#ifdef Q_OS_LINUX
static bool is_preferred_linux_audio_host_api(const PaHostApiInfo *info)
{
	if (!info) {
		return false;
	}

	const QString name = QString::fromLatin1(info->name);
	return name.contains(QStringLiteral("PipeWire"), Qt::CaseInsensitive) ||
		   name.contains(QStringLiteral("JACK"), Qt::CaseInsensitive) ||
		   name.contains(QStringLiteral("PulseAudio"), Qt::CaseInsensitive);
}

static PaDeviceIndex get_preferred_linux_audio_device(bool is_output_device)
{
	// Prefer sound servers that provide mixing and desktop integration
	// (PipeWire, JACK, PulseAudio) over plain ALSA defaults, which often
	// fail to share the device on modern Linux desktops.
	const QStringList preferred_host_apis = {
		QStringLiteral("PipeWire"),
		QStringLiteral("JACK"),
		QStringLiteral("PulseAudio"),
	};

	for (const QString &preferred : preferred_host_apis) {
		for (PaHostApiIndex i = 0, end = Pa_GetHostApiCount(); i < end; i++) {
			const PaHostApiInfo *info = Pa_GetHostApiInfo(i);
			if (!info) {
				continue;
			}

			const QString name = QString::fromLatin1(info->name);
			if (name.contains(preferred, Qt::CaseInsensitive)) {
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
	QString entry = is_output_device ? QStringLiteral("AudioOutput") :
									   QStringLiteral("AudioInput");

	return find_device_by_name(OAK_CONFIG_STR(entry).toString(),
							is_output_device);
}

PaDeviceIndex AudioManager::find_device_by_name(const QString &s,
											 bool is_output_device)
{
	PaDeviceIndex exact_match = paNoDevice;

	if (!s.isEmpty()) {
		for (PaDeviceIndex i = 0, end = Pa_GetDeviceCount(); i < end; i++) {
			const PaDeviceInfo *device = Pa_GetDeviceInfo(i);
			if (!device) {
				continue;
			}

			if (((is_output_device && device->maxOutputChannels) ||
				 (!is_output_device && device->maxInputChannels)) &&
				!s.compare(device->name)) {
				exact_match = i;
				break;
			}
		}
	}

#ifdef Q_OS_LINUX
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
				qInfo() << "Overriding saved audio device" << s
						<< "with preferred Linux audio device"
						<< Pa_GetDeviceInfo(preferred)->name;
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

PaStreamParameters AudioManager::get_port_audio_params(const AudioParams &params,
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
	, input_encoder_(nullptr)
{
#ifdef PA_HAS_JACK
	// PortAudio doesn't do a strcpy, so we need a const char that's readily accessible (i.e. not
	// a QString converted to UTF-8)
	PaJack_SetClientName("Oak Video Editor");
#endif

	Pa_Initialize();

	// Get device from config
	PaDeviceIndex output_device = find_config_device_by_name(true);
	PaDeviceIndex input_device = find_config_device_by_name(false);

	qDebug() << "AudioManager: selected output device index=" << output_device
			 << "input device index=" << input_device;

	set_output_device(output_device);
	set_input_device(input_device);

	output_buffer_ = new PreviewAudioDevice(this);
	output_buffer_->open(PreviewAudioDevice::ReadWrite);
	connect(output_buffer_, &PreviewAudioDevice::notify, this,
			&AudioManager::output_notify);
}

AudioManager::~AudioManager()
{
	close_output_stream();

	Pa_Terminate();
}

}
