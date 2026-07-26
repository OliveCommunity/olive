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

#include "oakengine/audio.h"
#include "oakengine/encoding.h"

#include <algorithm>
#include <cstring>
#include <new>

#include <QByteArray>
#include <QString>

#include "audio/audiomanager.h"
#include "audio/audioprocessor.h"
#include "audio/audiosynchronizer.h"
#include "audio/audiowaveformsync.h"
#include "olive/core/oakcore/audioparams.h"
#include "olive/core/render/audioparams.h"

namespace
{

int write_string(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf8 = s.toUtf8();
	const int len = int(utf8.size());
	if (buf && buf_size > 0) {
		const int n = qMin(len, buf_size - 1);
		std::memcpy(buf, utf8.constData(), size_t(n));
		buf[n] = '\0';
	}
	return len;
}

olive::AudioManager *manager()
{
	return olive::AudioManager::instance();
}

} // namespace

extern "C" int oakengine_audio_create_instance(void)
{
	olive::AudioManager::create_instance();
	return manager() ? OAKENGINE_OK : OAKENGINE_E_FAILED;
}

extern "C" int oakengine_audio_destroy_instance(void)
{
	olive::AudioManager::destroy_instance();
	return OAKENGINE_OK;
}

extern "C" void *oakengine_audio_manager_handle(void)
{
	return manager();
}

extern "C" int64_t oakengine_audio_get_output_device(void)
{
	if (olive::AudioManager *m = manager()) {
		return static_cast<int64_t>(m->get_output_device());
	}
	return -1; // paNoDevice
}

extern "C" int oakengine_audio_set_output_device(int64_t device)
{
	if (olive::AudioManager *m = manager()) {
		m->set_output_device(static_cast<PaDeviceIndex>(device));
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int64_t oakengine_audio_get_input_device(void)
{
	if (olive::AudioManager *m = manager()) {
		return static_cast<int64_t>(m->get_input_device());
	}
	return -1; // paNoDevice
}

extern "C" int oakengine_audio_set_input_device(int64_t device)
{
	if (olive::AudioManager *m = manager()) {
		m->set_input_device(static_cast<PaDeviceIndex>(device));
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int oakengine_audio_hard_reset(void)
{
	if (olive::AudioManager *m = manager()) {
		m->hard_reset();
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int oakengine_audio_clear_buffered_output(void)
{
	if (olive::AudioManager *m = manager()) {
		m->clear_buffered_output();
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int oakengine_audio_push_to_output(const OakAudioParams *params,
											  const char *samples,
											  int64_t samples_size,
											  char *error_buf,
											  int error_buf_size)
{
	if (!params || !samples || samples_size < 0) {
		return OAKENGINE_E_INVALID;
	}

	olive::AudioManager *m = manager();
	if (!m) {
		return OAKENGINE_E_STATE;
	}

	// The C++ AudioParams wrapper owns the OakAudioParams handle; the caller
	// keeps ownership of `params`, so copy before wrapping.
	const olive::core::AudioParams cpp_params =
		olive::core::AudioParams::from_handle(
			oakcore_audioparams_copy(params));

	QString error;
	const QByteArray data = QByteArray::fromRawData(samples,
												static_cast<int>(samples_size));
	if (!m->push_to_output(cpp_params, data, &error)) {
		write_string(error, error_buf, error_buf_size);
		return OAKENGINE_E_FAILED;
	}
	return OAKENGINE_OK;
}

extern "C" int oakengine_audio_stop_recording(void)
{
	if (olive::AudioManager *m = manager()) {
		m->stop_recording();
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int oakengine_audio_stop_output(void)
{
	if (olive::AudioManager *m = manager()) {
		m->stop_output();
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int oakengine_audio_reset_output_clock(void)
{
	if (olive::AudioManager *m = manager()) {
		m->reset_output_clock();
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int oakengine_audio_set_output_notify_interval(int64_t bytes)
{
	if (olive::AudioManager *m = manager()) {
		m->set_output_notify_interval(static_cast<int>(bytes));
		return OAKENGINE_OK;
	}
	return OAKENGINE_E_STATE;
}

extern "C" int oakengine_audio_start_recording(
	OakEngineEncodingParams *params, char *error_buf, int error_buf_size)
{
	// Delegate to the encoding-family implementation which handles the
	// OakEngineEncodingParams -> EncodingParams conversion internally.
	return oakengine_encoding_start_audio_recording(params, error_buf,
													error_buf_size);
}


extern "C" int oakengine_audio_estimate_envelope_offset(
	const double *reference, int reference_len,
	const double *candidate, int candidate_len,
	const bool *reference_valid, int reference_valid_len,
	const bool *candidate_valid, int candidate_valid_len,
	uint64_t window_samples, int64_t max_offset_windows,
	oak_audio_waveform_offset *out)
{
	if (!out || !reference || !candidate || reference_len < 0 ||
		candidate_len < 0 || !window_samples) {
		return OAKENGINE_E_INVALID;
	}

	if ((reference_valid && reference_valid_len != reference_len) ||
		(candidate_valid && candidate_valid_len != candidate_len)) {
		return OAKENGINE_E_INVALID;
	}

	QVector<double> ref(reference_len);
	std::copy(reference, reference + reference_len, ref.begin());
	QVector<double> cand(candidate_len);
	std::copy(candidate, candidate + candidate_len, cand.begin());

	QVector<bool> ref_valid;
	if (reference_valid) {
		ref_valid.resize(reference_valid_len);
		std::copy(reference_valid, reference_valid + reference_valid_len,
				  ref_valid.begin());
	}

	QVector<bool> cand_valid;
	if (candidate_valid) {
		cand_valid.resize(candidate_valid_len);
		std::copy(candidate_valid, candidate_valid + candidate_valid_len,
				  cand_valid.begin());
	}

	const olive::AudioWaveformSync::OffsetResult result =
		olive::AudioWaveformSync::estimate_envelope_offset(
			ref, cand, ref_valid, cand_valid, window_samples, max_offset_windows);

	out->offset_samples = result.offset_samples;
	out->confidence = result.confidence;
	out->valid = result.valid ? 1 : 0;
	return OAKENGINE_OK;
}

extern "C" int oakengine_audio_estimate_stretch_and_offset(
	const double *reference, int reference_len,
	const double *candidate, int candidate_len,
	const bool *reference_valid, int reference_valid_len,
	const bool *candidate_valid, int candidate_valid_len,
	uint64_t window_samples, int64_t max_offset_windows,
	double min_rate, double max_rate, double rate_step,
	oak_audio_waveform_stretch_offset *out)
{
	if (!out || !reference || !candidate || reference_len < 0 ||
		candidate_len < 0 || !window_samples) {
		return OAKENGINE_E_INVALID;
	}

	if ((reference_valid && reference_valid_len != reference_len) ||
		(candidate_valid && candidate_valid_len != candidate_len)) {
		return OAKENGINE_E_INVALID;
	}

	QVector<double> ref(reference_len);
	std::copy(reference, reference + reference_len, ref.begin());
	QVector<double> cand(candidate_len);
	std::copy(candidate, candidate + candidate_len, cand.begin());

	QVector<bool> ref_valid;
	if (reference_valid) {
		ref_valid.resize(reference_valid_len);
		std::copy(reference_valid, reference_valid + reference_valid_len,
				  ref_valid.begin());
	}

	QVector<bool> cand_valid;
	if (candidate_valid) {
		cand_valid.resize(candidate_valid_len);
		std::copy(candidate_valid, candidate_valid + candidate_valid_len,
				  cand_valid.begin());
	}

	const olive::AudioWaveformSync::StretchOffsetResult result =
		olive::AudioWaveformSync::estimate_stretch_and_offset(
			ref, cand, ref_valid, cand_valid, window_samples, max_offset_windows,
			min_rate, max_rate, rate_step);

	out->rate = result.rate;
	out->offset_samples = result.offset_samples;
	out->confidence = result.confidence;
	out->valid = result.valid ? 1 : 0;
	return OAKENGINE_OK;
}

extern "C" int oakengine_audio_sync_place_by_source_time(
	const oak_audio_sync_source_clip *reference,
	const oak_audio_sync_source_clip *candidate,
	int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
	oak_audio_sync_placement *out)
{
	if (!reference || !candidate || !out) {
		return OAKENGINE_E_INVALID;
	}

	olive::AudioSynchronizer::SourceClip ref;
	ref.source_start_time = olive::core::Rational(
		reference->source_start_time_num, reference->source_start_time_den);
	ref.media_in =
		olive::core::Rational(reference->media_in_num, reference->media_in_den);
	ref.has_source_start_time = reference->has_source_start_time != 0;

	olive::AudioSynchronizer::SourceClip cand;
	cand.source_start_time = olive::core::Rational(
		candidate->source_start_time_num, candidate->source_start_time_den);
	cand.media_in =
		olive::core::Rational(candidate->media_in_num, candidate->media_in_den);
	cand.has_source_start_time = candidate->has_source_start_time != 0;

	const olive::core::Rational timeline_in(reference_timeline_in_num,
										reference_timeline_in_den);
	const olive::AudioSynchronizer::Placement placement =
		olive::AudioSynchronizer::place_by_source_time(ref, cand, timeline_in);

	out->timeline_in_num = placement.timeline_in.numerator();
	out->timeline_in_den = placement.timeline_in.denominator();
	out->valid = placement.valid ? 1 : 0;
	return OAKENGINE_OK;
}

extern "C" int oakengine_audio_sync_place_by_waveform_offset(
	int64_t reference_timeline_in_num, int64_t reference_timeline_in_den,
	int64_t candidate_offset_samples, int sample_rate,
	oak_audio_sync_placement *out)
{
	if (!out || sample_rate <= 0) {
		return OAKENGINE_E_INVALID;
	}

	const olive::core::Rational timeline_in(reference_timeline_in_num,
										reference_timeline_in_den);
	const olive::AudioSynchronizer::Placement placement =
		olive::AudioSynchronizer::place_by_waveform_offset(
			timeline_in, candidate_offset_samples, sample_rate);

	out->timeline_in_num = placement.timeline_in.numerator();
	out->timeline_in_den = placement.timeline_in.denominator();
	out->valid = placement.valid ? 1 : 0;
	return OAKENGINE_OK;
}

/* ---- Audio format processor (R6 P5) ------------------------------------- */

namespace
{

olive::core::AudioParams params_from_c(const OakAudioParams *p)
{
	// AudioParams takes ownership of the handle, so hand it a copy.
	return olive::core::AudioParams::from_handle(oakcore_audioparams_copy(p));
}

} // namespace

struct OakEngineAudioProcessor {
	olive::AudioProcessor proc;

	// Holds the packed output of the most recent convert() so the caller can
	// borrow the bytes across the C boundary.
	olive::AudioProcessor::Buffer buf;
};

extern "C" OakEngineAudioProcessor *oakengine_audio_processor_create(void)
{
	return new (std::nothrow) OakEngineAudioProcessor();
}

extern "C" void oakengine_audio_processor_free(OakEngineAudioProcessor *p)
{
	delete p;
}

extern "C" int oakengine_audio_processor_open(OakEngineAudioProcessor *p,
											  const OakAudioParams *from,
											  const OakAudioParams *to,
											  double tempo)
{
	if (!p || !from || !to) {
		return OAKENGINE_E_INVALID;
	}

	const olive::core::AudioParams cpp_from = params_from_c(from);
	const olive::core::AudioParams cpp_to = params_from_c(to);
	return p->proc.open(cpp_from, cpp_to, tempo) ? OAKENGINE_OK
												 : OAKENGINE_E_FAILED;
}

extern "C" void oakengine_audio_processor_close(OakEngineAudioProcessor *p)
{
	if (p) {
		p->buf.clear();
		p->proc.close();
	}
}

extern "C" int oakengine_audio_processor_is_open(OakEngineAudioProcessor *p)
{
	return (p && p->proc.is_open()) ? 1 : 0;
}

extern "C" int oakengine_audio_processor_convert(OakEngineAudioProcessor *p,
												 float **in, int nb_in_samples,
												 const void **out_data,
												 int *out_size)
{
	if (!p) {
		return OAKENGINE_E_INVALID;
	}

	if (out_data) {
		*out_data = nullptr;
	}
	if (out_size) {
		*out_size = 0;
	}

	p->buf.clear();
	const int r = p->proc.convert(in, nb_in_samples, &p->buf);
	if (r < 0) {
		return r;
	}

	if (!p->buf.empty()) {
		if (out_data) {
			*out_data = p->buf.at(0).constData();
		}
		if (out_size) {
			*out_size = p->buf.at(0).size();
		}
	}
	return r;
}

extern "C" OakAudioParams *oakengine_audio_processor_output_params(
	OakEngineAudioProcessor *p)
{
	if (!p || !p->proc.is_open()) {
		return nullptr;
	}
	return oakcore_audioparams_copy(p->proc.to().handle());
}
