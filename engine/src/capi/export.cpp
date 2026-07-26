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

#include "oakengine/exporter.h"

#include "exportinternal.h"

#include <atomic>
#include <cstdio>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QString>
#include <QThread>

#include "codec/conformmanager.h"
#include "codec/encoder.h"
#include "codec/ffmpeg/ffmpegencoder.h"
#include "coreengine.h"
#include "node/color/colormanager/colormanager.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "render/rendermanager.h"
#include "task/export/export.h"

namespace
{

// Last export error per thread.
thread_local QString g_last_error;

// Installed progress callback (per thread), NULL when unset.
thread_local oakengine_export_progress_fn g_progress_fn = nullptr;
thread_local void *g_progress_userdata = nullptr;

// Encoder-specific video options (per thread), applied to the next
// assembled EncodingParams.
thread_local QHash<QString, QString> g_video_options;

// The export currently driven by oakengine_export_render()/_ex(), for
// cross-thread cancellation (one at a time per process).
std::atomic<olive::ExportTask *> g_current_export{ nullptr };

void set_error(const QString &error)
{
	g_last_error = error;
}

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

// facade video codec -> (ExportCodec, container format); false when invalid.
bool map_video_codec(int codec, olive::ExportCodec::Codec *out_codec,
					 olive::ExportFormat::Format *out_format)
{
	switch (codec) {
	case OAKENGINE_EXPORT_VIDEO_H264:
		*out_codec = olive::ExportCodec::k_codec_h264;
		*out_format = olive::ExportFormat::k_format_mpe_g4_video;
		return true;
	case OAKENGINE_EXPORT_VIDEO_H265:
		*out_codec = olive::ExportCodec::k_codec_h265;
		*out_format = olive::ExportFormat::k_format_mpe_g4_video;
		return true;
	case OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE:
		*out_codec = olive::ExportCodec::k_codec_png;
		*out_format = olive::ExportFormat::k_format_png;
		return true;
	default:
		return false;
	}
}

bool map_audio_codec(int codec, olive::ExportCodec::Codec *out)
{
	switch (codec) {
	case OAKENGINE_EXPORT_AUDIO_AAC:
		*out = olive::ExportCodec::k_codec_aac;
		return true;
	case OAKENGINE_EXPORT_AUDIO_PCM:
		*out = olive::ExportCodec::k_codec_pcm;
		return true;
	default:
		return false;
	}
}

// Channel count -> ffmpeg-style layout mask (mono/stereo only).
bool layout_for_channels(int channels, uint64_t *layout)
{
	switch (channels) {
	case 1:
		*layout = 0x4; // AV_CH_LAYOUT_MONO
		return true;
	case 2:
		*layout = 0x3; // AV_CH_LAYOUT_STEREO
		return true;
	default:
		return false;
	}
}

// For image sequences: make sure the filename carries the engine's frame
// placeholder ("[#####]", the same bracketed-hash form the application
// uses), inserting one before the extension when absent.
QString image_sequence_filename(const QString &path)
{
	if (olive::Encoder::filename_contains_digit_placeholder(path)) {
		return path;
	}
	const QFileInfo fi(path);
	return fi.dir().filePath(fi.completeBaseName() +
							 QStringLiteral("_[#####].") + fi.suffix());
}

// Pre-generate audio conforms for every footage with audio streams in the
// project, using exactly the AudioParams the export render will request.
// This mirrors what the application gets for free from preview playback
// (conforms are usually already cached by export time there) and makes
// first-time exports of freshly imported media both faster and
// deterministic. Delivery to the TaskManager thread and the completion
// signal both go through this thread's event queue, so the wait pumps
// events like the render wait does. Returns false on timeout; the caller
// proceeds anyway since the render's own conform wait is the fallback.
bool prewarm_conforms(olive::Project *project,
					  const olive::AudioParams &params, QString *error)
{
	if (!olive::ConformManager::instance() || params.sample_rate() <= 0) {
		return true; // nothing to prewarm (or nothing to prewarm with)
	}
	const QString cache_path = project->cache_path();

	struct Pending {
		QString decoder_id;
		olive::Decoder::CodecStream stream;
	};
	QVector<Pending> pending;
	for (olive::Node *n : project->nodes()) {
		olive::Footage *footage = dynamic_cast<olive::Footage *>(n);
		if (!footage || !footage->is_valid()) {
			continue;
		}
		const QString decoder_id = footage->decoder().isEmpty() ?
									   QStringLiteral("ffmpeg") :
									   footage->decoder();
		for (int i = 0; i < footage->get_audio_stream_count(); i++) {
			const olive::AudioParams stream_params =
				footage->get_audio_params(i);
			olive::Decoder::CodecStream stream(footage->filename(),
											   stream_params.stream_index(),
											   nullptr);
			// Trigger (or adopt) the conform task.
			olive::ConformManager::instance()->get_conform_state(
				decoder_id, cache_path, stream, params, false);
			pending.append({ decoder_id, stream });
		}
	}

	constexpr qint64 k_prewarm_timeout_ms = 120000;
	QElapsedTimer timer;
	timer.start();
	for (const Pending &p : pending) {
		while (true) {
			const olive::ConformManager::Conform conform =
				olive::ConformManager::instance()->get_conform_state(
					p.decoder_id, cache_path, p.stream, params, false);
			if (conform.state == olive::ConformManager::k_conform_exists) {
				break;
			}
			if (timer.hasExpired(k_prewarm_timeout_ms)) {
				*error = QStringLiteral("conform prewarm timed out for %1")
						 .arg(p.stream.filename());
				return false;
			}
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(5);
		}
	}
	return true;
}

// Drive a fully-assembled EncodingParams through the ExportTask on a
// worker thread while this thread pumps events (see the comment in the
// body). Shared by oakengine_export_render() and _render_ex().
int render_internal(olive::Sequence *sequence, olive::Project *project,
					olive::EncodingParams &params, bool prewarm_audio,
					const olive::AudioParams &prewarm_params)
{
	try {
		// Pre-generate the audio conforms the render is about to need (the
		// headless equivalent of the application's preview-warmed cache).
		// A timeout here is not fatal: the render's own conform wait is the
		// fallback path.
		if (prewarm_audio) {
			QString prewarm_error;
			prewarm_conforms(project, prewarm_params, &prewarm_error);
		}

		olive::ExportTask task(sequence, project->color_manager(), params);
		g_current_export.store(&task);
		// The progress signal is emitted on the task thread, so the callback
		// (installed for the calling thread) is captured by value -- reading
		// the thread_local on the task thread would see NULL.
		const oakengine_export_progress_fn progress_fn = g_progress_fn;
		void *const progress_userdata = g_progress_userdata;
		if (progress_fn) {
			QObject::connect(&task, &olive::ExportTask::progress_changed,
							 [progress_fn, progress_userdata](double fraction) {
								 progress_fn(fraction, progress_userdata);
							 });
		}

		// Drive the task the way the application does: the task runs on a
		// worker thread while the calling thread keeps its event loop
		// spinning. A bare synchronous start() deadlocks on audio exports:
		// audio conforms are delivered to TaskManager via queued calls and
		// ConformManager::conform_task_finished is queued back to THIS
		// thread, which must therefore process events while waiting.
		std::atomic<bool> done{ false };
		bool result = false;
		QObject::connect(&task, &olive::ExportTask::finished,
						 [&done, &result](olive::Task *, bool r) {
							 result = r;
							 done.store(true);
						 });

		QThread task_thread;
		task.moveToThread(&task_thread);
		task_thread.start();
		QMetaObject::invokeMethod(&task, "start", Qt::QueuedConnection);
		while (!done.load()) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(5);
		}
		// Move the task back before tearing the thread down (QObjects must
		// not be destroyed while owned by a dead thread).
		task.moveToThread(QCoreApplication::instance()->thread());
		task_thread.quit();
		task_thread.wait();
		g_current_export.store(nullptr);

		if (task.is_cancelled()) {
			set_error(QStringLiteral("export cancelled"));
			return OAKENGINE_E_CANCELLED;
		}
		if (!result) {
			set_error(task.get_error().isEmpty() ?
						  QStringLiteral("export failed") :
						  task.get_error());
			return OAKENGINE_E_FAILED;
		}
	} catch (const std::exception &e) {
		g_current_export.store(nullptr);
		set_error(QStringLiteral("export failed: %1").arg(e.what()));
		return OAKENGINE_E_FAILED;
	}

	return OAKENGINE_OK;
}

// Assemble EncodingParams from the extended POD. Returns an error string
// on invalid options.
QString params_from_ex(const oak_export_options_ex &o,
					   olive::Sequence *sequence, const char *path,
					   olive::EncodingParams *params)
{
	if (o.format < 0 || o.format >= olive::ExportFormat::k_format_count) {
		return QStringLiteral("unknown export format %1").arg(o.format);
	}
	if (o.video_codec < 0 || o.video_codec >= olive::ExportCodec::k_codec_count) {
		return QStringLiteral("unknown video codec %1").arg(o.video_codec);
	}
	if (o.audio_codec < 0 || o.audio_codec >= olive::ExportCodec::k_codec_count) {
		return QStringLiteral("unknown audio codec %1").arg(o.audio_codec);
	}
	if (o.subtitles_enabled &&
		(o.subtitles_codec < 0 ||
		 o.subtitles_codec >= olive::ExportCodec::k_codec_count)) {
		return QStringLiteral("unknown subtitle codec %1")
			.arg(o.subtitles_codec);
	}

	const olive::ExportCodec::Codec vcodec =
		static_cast<olive::ExportCodec::Codec>(o.video_codec);
	const olive::ExportCodec::Codec acodec =
		static_cast<olive::ExportCodec::Codec>(o.audio_codec);
	const olive::ExportFormat::Format format =
		static_cast<olive::ExportFormat::Format>(o.format);

	// Video params: sequence's own, with overrides applied.
	olive::VideoParams vp = sequence->get_video_params();
	if (o.video_width > 0) {
		vp.set_width(o.video_width);
	}
	if (o.video_height > 0) {
		vp.set_height(o.video_height);
	}
	if (o.frame_rate_num > 0 && o.frame_rate_den > 0) {
		vp.set_time_base(olive::Rational(o.frame_rate_den, o.frame_rate_num));
	}
	if (o.pixel_aspect_num > 0 && o.pixel_aspect_den > 0) {
		vp.set_pixel_aspect_ratio(
			olive::Rational(o.pixel_aspect_num, o.pixel_aspect_den));
	}
	if (o.interlacing >= 0) {
		vp.set_interlacing(
			static_cast<olive::VideoParams::Interlacing>(o.interlacing));
	}
	if (o.color_range >= 0) {
		vp.set_color_range(
			static_cast<olive::VideoParams::ColorRange>(o.color_range));
	}
	if (o.pixel_format >= 0) {
		vp.set_format(
			static_cast<olive::PixelFormat::Format>(o.pixel_format));
	}
	if (vp.frame_rate().isNull() || vp.frame_rate().isNaN()) {
		return QStringLiteral("sequence has no valid frame rate");
	}
	if (vp.width() <= 0 || vp.height() <= 0) {
		return QStringLiteral("sequence has no valid video dimensions");
	}

	params->set_format(format);
	QString filename = QString::fromUtf8(path);
	if (o.is_image_sequence) {
		params->set_video_is_image_sequence(true);
		filename = image_sequence_filename(filename);
	}
	params->set_filename(filename);

	if (o.video_enabled) {
		params->enable_video(vp, vcodec);
		if (o.video_bit_rate > 0) {
			params->set_video_bit_rate(o.video_bit_rate);
		}
		if (o.video_threads > 0) {
			params->set_video_threads(o.video_threads);
		}
		// Encoded pixel format by index into the codec's supported list.
		if (o.video_pix_fmt >= 0) {
			olive::FFmpegEncoder probe{ olive::EncodingParams() };
			const QStringList pix_fmts =
				probe.get_pixel_formats_for_codec(vcodec);
			if (o.video_pix_fmt >= pix_fmts.size()) {
				return QStringLiteral(
					"pixel format index %1 out of range for codec %2")
					.arg(o.video_pix_fmt)
					.arg(o.video_codec);
			}
			if (!pix_fmts.isEmpty()) {
				params->set_video_pix_fmt(
					pix_fmts.at(qMax(0, o.video_pix_fmt)));
			}
		}
	}

	if (o.audio_enabled) {
		olive::AudioParams ap = sequence->get_audio_params();
		const int sample_rate =
			o.audio_sample_rate > 0 ? o.audio_sample_rate : ap.sample_rate();
		const uint64_t layout = o.audio_channel_layout != 0 ?
									o.audio_channel_layout :
									ap.channel_layout();
		const olive::core::SampleFormat::Format sample_format =
			o.audio_sample_format > 0 ?
				static_cast<olive::core::SampleFormat::Format>(
					o.audio_sample_format) :
				olive::core::SampleFormat::f32_p;
		if (sample_rate <= 0) {
			return QStringLiteral("sequence has no valid audio sample rate");
		}
		ap = olive::AudioParams(sample_rate, layout, sample_format);
		params->enable_audio(ap, acodec);
		if (o.audio_bit_rate > 0) {
			params->set_audio_bit_rate(o.audio_bit_rate);
		}
	}

	if (o.subtitles_enabled) {
		if (o.subtitles_sidecar) {
			params->enable_sidecar_subtitles(
				static_cast<olive::ExportFormat::Format>(o.subtitles_format),
				static_cast<olive::ExportCodec::Codec>(o.subtitles_codec));
		} else {
			params->enable_subtitles(
				static_cast<olive::ExportCodec::Codec>(o.subtitles_codec));
		}
	}

	// Range.
	const olive::Rational tb = vp.frame_rate().flipped();
	switch (o.range_mode) {
	case OAKENGINE_EXPORT_RANGE_CUSTOM: {
		if (o.range_in_ts < 0 || o.range_out_ts <= o.range_in_ts) {
			return QStringLiteral("invalid custom range");
		}
		const olive::Rational in_time =
			olive::core::Timecode::timestamp_to_time(o.range_in_ts, tb);
		const olive::Rational out_time =
			olive::core::Timecode::timestamp_to_time(o.range_out_ts, tb);
		params->set_custom_range(olive::TimeRange(in_time, out_time));
		params->set_export_length(out_time - in_time);
		break;
	}
	case OAKENGINE_EXPORT_RANGE_STILL: {
		if (o.still_time_ts < 0) {
			return QStringLiteral("invalid still time");
		}
		const olive::Rational t =
			olive::core::Timecode::timestamp_to_time(o.still_time_ts, tb);
		params->set_custom_range(olive::TimeRange(t, t + tb));
		params->set_export_length(tb);
		break;
	}
	default:
		params->set_export_length(sequence->get_length());
		break;
	}

	// Scaling and color.
	if (o.scaling_method >= 0) {
		params->set_video_scaling_method(
			static_cast<olive::EncodingParams::VideoScalingMethod>(
				o.scaling_method));
	}
	switch (o.color_transform) {
	case OAKENGINE_EXPORT_COLOR_REC709_OETF:
		params->set_color_transform(
			olive::ColorTransform(QStringLiteral("Rec.709 OETF")));
		break;
	case OAKENGINE_EXPORT_COLOR_REFERENCE:
		params->set_color_transform(olive::ColorTransform());
		break;
	case OAKENGINE_EXPORT_COLOR_BT1886_EOTF:
		params->set_color_transform(
			olive::ColorTransform(QStringLiteral("BT.1886 EOTF")));
		break;
	case OAKENGINE_EXPORT_COLOR_CUSTOM:
		if (o.color_transform_name[0] == '\0') {
			return QStringLiteral(
				"color_transform is CUSTOM but color_transform_name is empty");
		}
		params->set_color_transform(olive::ColorTransform(
			QString::fromUtf8(o.color_transform_name)));
		break;
	default:
		// Same default output transform as the application's export dialog.
		params->set_color_transform(
			olive::ColorTransform(QStringLiteral("sRGB OETF")));
		break;
	}

	// Encoder-specific video options accumulated for this thread.
	for (auto it = g_video_options.cbegin(); it != g_video_options.cend();
		 ++it) {
		params->set_video_option(it.key(), it.value());
	}

	return QString();
}

} // namespace

int oakengine_export_render_internal(olive::Sequence *sequence,
									 olive::Project *project,
									 olive::EncodingParams &params,
									 bool prewarm_audio,
									 const olive::AudioParams &prewarm_params)
{
	return render_internal(sequence, project, params, prewarm_audio,
						   prewarm_params);
}

void oakengine_export_set_error_string(const QString &error)
{
	set_error(error);
}

extern "C"
{

int oakengine_export_render(OakEngineSequence *seq, const char *path,
							int64_t in_ts, int64_t out_ts, int width,
							int height, const oak_export_options *opts)
{
	set_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || !path || in_ts < 0 || out_ts <= in_ts) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (!olive::RenderManager::instance()) {
		set_error(QStringLiteral("engine not initialized with "
								 "OAKENGINE_INIT_RENDER"));
		return OAKENGINE_E_STATE;
	}

	oak_export_options o = {};
	if (opts) {
		o = *opts;
	}
	if (o.video_codec == 0 && o.audio_codec == 0 && o.video_bit_rate == 0 &&
		o.audio_sample_rate == 0 && o.audio_channel_count == 0 && !opts) {
		// All defaults.
	}
	if (o.video_codec < 0) {
		o.video_codec = OAKENGINE_EXPORT_VIDEO_H264;
	}
	if (o.audio_codec < 0 && o.audio_codec != OAKENGINE_EXPORT_AUDIO_NONE) {
		o.audio_codec = OAKENGINE_EXPORT_AUDIO_AAC;
	}

	olive::ExportCodec::Codec vcodec, acodec;
	olive::ExportFormat::Format format;
	if (!map_video_codec(o.video_codec, &vcodec, &format)) {
		set_error(QStringLiteral("unknown video codec %1")
					  .arg(o.video_codec));
		return OAKENGINE_E_INVALID;
	}
	const bool audio_enabled = o.audio_codec != OAKENGINE_EXPORT_AUDIO_NONE;
	if (audio_enabled && !map_audio_codec(o.audio_codec, &acodec)) {
		set_error(QStringLiteral("unknown audio codec %1")
					  .arg(o.audio_codec));
		return OAKENGINE_E_INVALID;
	}

	// Video parameters: the sequence's own, with dimensions/frame geometry
	// overridden as requested.
	olive::VideoParams vp = sequence->get_video_params();
	if (vp.frame_rate().isNull() || vp.frame_rate().isNaN()) {
		set_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_INVALID;
	}
	if (width <= 0) {
		width = vp.width();
	}
	if (height <= 0) {
		height = vp.height();
	}
	if (width <= 0 || height <= 0) {
		set_error(QStringLiteral("sequence has no valid video dimensions"));
		return OAKENGINE_E_INVALID;
	}
	vp.set_width(width);
	vp.set_height(height);

	// Audio parameters: the sequence's own, with rate/layout overridden.
	olive::AudioParams ap = sequence->get_audio_params();
	if (audio_enabled) {
		int sample_rate = o.audio_sample_rate > 0 ? o.audio_sample_rate :
													ap.sample_rate();
		uint64_t layout = ap.channel_layout();
		if (o.audio_channel_count > 0) {
			if (!layout_for_channels(o.audio_channel_count, &layout)) {
				set_error(QStringLiteral("unsupported audio channel count %1 "
										 "(1 = mono, 2 = stereo)")
						  .arg(o.audio_channel_count));
				return OAKENGINE_E_INVALID;
			}
		}
		if (sample_rate <= 0) {
			set_error(QStringLiteral("sequence has no valid audio sample "
									 "rate"));
			return OAKENGINE_E_INVALID;
		}
		ap = olive::AudioParams(sample_rate, layout, ap.format());
	}

	olive::Project *project = olive::Project::get_project_from_object(sequence);
	if (!project) {
		set_error(QStringLiteral("sequence is not part of a project"));
		return OAKENGINE_E_INVALID;
	}

	// Assemble the encoding parameters for the engine's export path.
	const olive::Rational tb = vp.frame_rate().flipped();
	const olive::Rational in_time =
		olive::core::Timecode::timestamp_to_time(in_ts, tb);
	const olive::Rational out_time =
		olive::core::Timecode::timestamp_to_time(out_ts, tb);

	olive::EncodingParams params;
	params.set_format(format);
	QString filename = QString::fromUtf8(path);
	if (o.video_codec == OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE) {
		params.set_video_is_image_sequence(true);
		filename = image_sequence_filename(filename);
	}
	params.set_filename(filename);
	params.enable_video(vp, vcodec);
	if (audio_enabled) {
		params.enable_audio(ap, acodec);
	}
	// The FFmpeg bridge rejects an empty pixel format ("Invalid video pixel
	// format: -1"); like the export dialog, default to the codec's
	// preferred pixel format (e.g. yuv420p for H.264).
	if (format == olive::ExportFormat::k_format_mpe_g4_video) {
		olive::FFmpegEncoder probe{ olive::EncodingParams() };
		const QStringList pix_fmts = probe.get_pixel_formats_for_codec(vcodec);
		if (!pix_fmts.isEmpty()) {
			params.set_video_pix_fmt(pix_fmts.first());
		}
	}
	if (o.video_bit_rate > 0) {
		params.set_video_bit_rate(o.video_bit_rate);
	}
	params.set_custom_range(olive::TimeRange(in_time, out_time));
	params.set_export_length(out_time - in_time);
	params.set_video_scaling_method(olive::EncodingParams::k_fit);
	// Same default output transform as the application's export dialog.
	params.set_color_transform(
		olive::ColorTransform(QStringLiteral("sRGB OETF")));

	try {
		return render_internal(sequence, project, params, audio_enabled, ap);
	} catch (const std::exception &e) {
		set_error(QStringLiteral("export failed: %1").arg(e.what()));
		return OAKENGINE_E_FAILED;
	}
}

int oakengine_export_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_last_error, buf, buf_size);
}

int oakengine_export_render_ex(OakEngineSequence *seq, const char *path,
							   const oak_export_options_ex *opts)
{
	set_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || !path || !opts) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	if (!olive::RenderManager::instance()) {
		set_error(QStringLiteral("engine not initialized with "
								 "OAKENGINE_INIT_RENDER"));
		return OAKENGINE_E_STATE;
	}
	olive::Project *project = olive::Project::get_project_from_object(sequence);
	if (!project) {
		set_error(QStringLiteral("sequence is not part of a project"));
		return OAKENGINE_E_INVALID;
	}

	oak_export_options_ex o = *opts;
	if (o.video_enabled == 0 && o.audio_enabled == 0 &&
		o.subtitles_enabled == 0) {
		// Preserve the simple entry point's defaults: video+audio enabled.
		o.video_enabled = 1;
		o.audio_enabled = 1;
	}

	olive::EncodingParams params;
	const QString error = params_from_ex(o, sequence, path, &params);
	if (!error.isEmpty()) {
		set_error(error);
		return OAKENGINE_E_INVALID;
	}

	try {
		const bool prewarm_audio = o.audio_enabled != 0;
		return render_internal(sequence, project, params, prewarm_audio,
							   params.audio_enabled() ?
								   params.audio_params() :
								   sequence->get_audio_params());
	} catch (const std::exception &e) {
		set_error(QStringLiteral("export failed: %1").arg(e.what()));
		return OAKENGINE_E_FAILED;
	}
}

void oakengine_export_cancel(void)
{
	if (olive::ExportTask *task = g_current_export.load()) {
		task->cancel();
	}
}

void oakengine_export_set_video_option(const char *key, const char *value)
{
	if (!key) {
		return;
	}
	if (value) {
		g_video_options.insert(QString::fromUtf8(key),
							   QString::fromUtf8(value));
	} else {
		g_video_options.clear();
	}
}

int oakengine_export_has_video_codec(int codec)
{
	if (codec == OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE) {
		// PNG sequences go through the statically linked OIIO encoder.
		return 1;
	}
	olive::ExportCodec::Codec mapped;
	olive::ExportFormat::Format format;
	if (!map_video_codec(codec, &mapped, &format)) {
		return 0;
	}
	olive::FFmpegEncoder encoder{ olive::EncodingParams() };
	return !encoder.get_pixel_formats_for_codec(mapped).isEmpty() ? 1 : 0;
}

int oakengine_export_has_audio_codec(int codec)
{
	olive::ExportCodec::Codec mapped;
	if (!map_audio_codec(codec, &mapped)) {
		return 0;
	}
	olive::FFmpegEncoder encoder{ olive::EncodingParams() };
	return !encoder.get_sample_formats_for_codec(mapped).empty() ? 1 : 0;
}

void oakengine_export_set_progress_callback(oakengine_export_progress_fn fn,
											void *userdata)
{
	g_progress_fn = fn;
	g_progress_userdata = userdata;
}

} // extern "C"
