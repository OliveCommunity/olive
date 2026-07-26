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

#include "oakengine/sync.h"

#include <cmath>

#include <QString>

#include "audio/audiowaveformsync.h"
#include "node/block/clip/clip.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/renderer.h"
#include "render/rendermanager.h"

namespace
{

thread_local QString g_last_error;

void set_error(const QString &error)
{
	g_last_error = error;
}

int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

// Render the full on-track audio of a clip through the renderer
// family. Returns an unallocated buffer on failure (error reported).
olive::core::SampleBuffer render_clip_audio(olive::Sequence *sequence,
											olive::ClipBlock *clip,
											OakEngineRenderer *renderer)
{
	const olive::Rational tb = sequence->get_video_params().time_base();
	const int64_t in_ts = olive::core::Timecode::time_to_timestamp(
		clip->in(), tb, olive::core::Timecode::k_round);
	const int64_t out_ts = olive::core::Timecode::time_to_timestamp(
		clip->out(), tb, olive::core::Timecode::k_round);

	OakEngineAudioBuffer *buf =
		oakengine_renderer_render_audio(renderer, in_ts, out_ts - in_ts);
	if (!buf) {
		char err[256];
		err[0] = '\0';
		oakengine_renderer_last_error(renderer, err, sizeof(err));
		set_error(QStringLiteral("audio render failed: %1")
					  .arg(err[0] ? err : "(no error)"));
		return olive::core::SampleBuffer();
	}

	const olive::AudioParams params(
		oakengine_audio_sample_rate(buf),
		sequence->get_audio_params().channel_layout(),
		olive::core::SampleFormat::f32_p);
	olive::core::SampleBuffer samples(
		params, size_t(oakengine_audio_sample_count(buf)));
	for (int ch = 0; ch < oakengine_audio_channel_count(buf); ch++) {
		memcpy(samples.to_raw_ptrs()[ch], oakengine_audio_data(buf, ch),
			   size_t(oakengine_audio_sample_count(buf)) * sizeof(float));
	}
	oakengine_audio_free(buf);
	return samples;
}

// Shared front of both estimators: validation, then render both clips'
// audio and extract the RMS envelopes with the application's window
// parameters. Returns 0 on success (error reported otherwise).
int prepare_envelopes(OakEngineSequence *seq, OakEngineClip *reference,
					  OakEngineClip *target, QVector<double> *ref_envelope,
					  QVector<double> *target_envelope, int *sample_rate,
					  int64_t *max_offset_windows, size_t *window_samples)
{
	set_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	olive::ClipBlock *ref_clip =
		reinterpret_cast<olive::ClipBlock *>(reference);
	olive::ClipBlock *target_clip =
		reinterpret_cast<olive::ClipBlock *>(target);
	if (!sequence || !ref_clip || !target_clip) {
		set_error(QStringLiteral("invalid sequence or clip handle"));
		return OAKENGINE_E_INVALID;
	}
	if (!ref_clip->track() || !target_clip->track()) {
		set_error(QStringLiteral("clip is not on a track"));
		return OAKENGINE_E_INVALID;
	}
	const olive::AudioParams audio_params = sequence->get_audio_params();
	if (audio_params.channel_count() <= 0 ||
		audio_params.sample_rate() <= 0) {
		set_error(QStringLiteral("sequence has no audio"));
		return OAKENGINE_E_STATE;
	}
	if (!olive::RenderManager::instance()) {
		set_error(QStringLiteral("engine not initialized with "
								 "OAKENGINE_INIT_RENDER"));
		return OAKENGINE_E_STATE;
	}

	const olive::Rational frame_rate =
		sequence->get_video_params().frame_rate();
	const int fps_num = frame_rate.isNull() ? 30000 : frame_rate.numerator();
	const int fps_den = frame_rate.isNull() ? 1001 : frame_rate.denominator();
	OakEngineRenderer *renderer =
		// Frame geometry is irrelevant for audio renders; the facade
		// requires a positive size.
		oakengine_renderer_create(seq, 16, 16, 4, fps_num, fps_den, nullptr);
	if (!renderer) {
		set_error(QStringLiteral("failed to create the renderer"));
		return OAKENGINE_E_STATE;
	}

	const olive::core::SampleBuffer ref_samples =
		render_clip_audio(sequence, ref_clip, renderer);
	if (!ref_samples.is_allocated()) {
		oakengine_renderer_free(renderer);
		return OAKENGINE_E_STATE;
	}
	const olive::core::SampleBuffer target_samples =
		render_clip_audio(sequence, target_clip, renderer);
	oakengine_renderer_free(renderer);
	if (!target_samples.is_allocated()) {
		return OAKENGINE_E_STATE;
	}

	*sample_rate = audio_params.sample_rate();
	*window_samples = size_t(std::max(1, *sample_rate / 20));
	*ref_envelope = olive::AudioWaveformSync::extract_rms_envelope(
		ref_samples, *window_samples);
	*target_envelope = olive::AudioWaveformSync::extract_rms_envelope(
		target_samples, *window_samples);
	// The application's 10-minute maximum offset, in envelope windows.
	*max_offset_windows = (int64_t(*sample_rate) * 10 * 60) /
						  int64_t(*window_samples);
	return OAKENGINE_OK;
}

} // namespace

extern "C"
{

int oakengine_sync_estimate_offset(OakEngineSequence *seq,
								   OakEngineClip *reference,
								   OakEngineClip *target,
								   double *out_offset_seconds,
								   double *out_confidence)
{
	QVector<double> ref_envelope, target_envelope;
	int sample_rate = 0;
	int64_t max_offset_windows = 0;
	size_t window_samples = 0;
	const int rc = prepare_envelopes(seq, reference, target, &ref_envelope,
									 &target_envelope, &sample_rate,
									 &max_offset_windows, &window_samples);
	if (rc != OAKENGINE_OK) {
		return rc;
	}

	const olive::AudioWaveformSync::OffsetResult result =
		olive::AudioWaveformSync::estimate_envelope_offset(
			ref_envelope, target_envelope, {}, {}, window_samples,
			max_offset_windows);
	if (out_confidence) {
		*out_confidence = result.confidence;
	}
	if (!result.valid) {
		if (out_offset_seconds) {
			*out_offset_seconds = 0.0;
		}
		set_error(QStringLiteral("waveform correlation was inconclusive "
								 "(confidence %1)")
					  .arg(result.confidence));
		return OAKENGINE_E_STATE;
	}
	if (out_offset_seconds) {
		*out_offset_seconds =
			double(result.offset_samples) / double(sample_rate);
	}
	return OAKENGINE_OK;
}

int oakengine_sync_estimate_stretch_offset(
	OakEngineSequence *seq, OakEngineClip *reference, OakEngineClip *target,
	double *out_stretch, double *out_offset_seconds, double *out_confidence)
{
	QVector<double> ref_envelope, target_envelope;
	int sample_rate = 0;
	int64_t max_offset_windows = 0;
	size_t window_samples = 0;
	const int rc = prepare_envelopes(seq, reference, target, &ref_envelope,
									 &target_envelope, &sample_rate,
									 &max_offset_windows, &window_samples);
	if (rc != OAKENGINE_OK) {
		return rc;
	}

	// The application's tighter 30-second offset radius for the stretch
	// search (keeps it interactive).
	const int64_t radius_windows = std::min<int64_t>(
		max_offset_windows,
		(int64_t(sample_rate) * 30) / int64_t(window_samples));
	const olive::AudioWaveformSync::StretchOffsetResult result =
		olive::AudioWaveformSync::estimate_stretch_and_offset(
			ref_envelope, target_envelope, {}, {}, window_samples,
			radius_windows, 0.75, 1.34, 0.005);
	if (out_confidence) {
		*out_confidence = result.confidence;
	}
	if (!result.valid) {
		if (out_stretch) {
			*out_stretch = 1.0;
		}
		if (out_offset_seconds) {
			*out_offset_seconds = 0.0;
		}
		set_error(QStringLiteral("stretch correlation was inconclusive "
								 "(confidence %1)")
					  .arg(result.confidence));
		return OAKENGINE_E_STATE;
	}
	if (out_stretch) {
		*out_stretch = result.rate;
	}
	if (out_offset_seconds) {
		*out_offset_seconds =
			double(result.offset_samples) / double(sample_rate);
	}
	return OAKENGINE_OK;
}

int oakengine_sync_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_last_error, buf, buf_size);
}

int oakengine_sync_place_by_source_time(
    int64_t ref_source_start_num, int64_t ref_source_start_den,
    int64_t ref_media_in_num, int64_t ref_media_in_den,
    int64_t cand_source_start_num, int64_t cand_source_start_den,
    int64_t cand_media_in_num, int64_t cand_media_in_den,
    int64_t anchor_num, int64_t anchor_den,
    oak_sync_placement *out)
{
	if (ref_source_start_den == 0 || ref_media_in_den == 0 ||
		cand_source_start_den == 0 || cand_media_in_den == 0 ||
		anchor_den == 0) {
		return OAKENGINE_E_INVALID;
	}

	const olive::core::Rational ref_source_start(ref_source_start_num,
												 ref_source_start_den);
	const olive::core::Rational ref_media_in(ref_media_in_num,
											ref_media_in_den);
	const olive::core::Rational cand_source_start(cand_source_start_num,
												  cand_source_start_den);
	const olive::core::Rational cand_media_in(cand_media_in_num,
											  cand_media_in_den);
	const olive::core::Rational anchor(anchor_num, anchor_den);

	const olive::core::Rational ref_head = ref_source_start + ref_media_in;
	const olive::core::Rational cand_head = cand_source_start + cand_media_in;
	const olive::core::Rational timeline_in = anchor + cand_head - ref_head;

	if (timeline_in.isNaN()) {
		return OAKENGINE_E_INVALID;
	}

	if (out) {
		out->timeline_in_num = timeline_in.numerator();
		out->timeline_in_den = timeline_in.denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_sync_place_by_waveform_offset(
    int64_t ref_timeline_in_num, int64_t ref_timeline_in_den,
    int64_t candidate_offset_samples, int sample_rate,
    oak_sync_placement *out)
{
	if (sample_rate <= 0 || ref_timeline_in_den == 0) {
		return OAKENGINE_E_INVALID;
	}

	const olive::core::Rational ref_timeline_in(ref_timeline_in_num,
												ref_timeline_in_den);
	const olive::core::Rational offset =
		olive::core::Rational::from_double(
			static_cast<double>(candidate_offset_samples) /
			static_cast<double>(sample_rate));
	const olive::core::Rational timeline_in = ref_timeline_in + offset;

	if (timeline_in.isNaN()) {
		return OAKENGINE_E_INVALID;
	}

	if (out) {
		out->timeline_in_num = timeline_in.numerator();
		out->timeline_in_den = timeline_in.denominator();
	}
	return OAKENGINE_OK;
}

} // extern "C"
