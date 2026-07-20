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

// For PNG sequences: make sure the filename carries a frame placeholder
// ("-%04d"), inserting one before the extension when absent.
QString image_sequence_filename(const QString &path)
{
	if (olive::Encoder::filename_contains_digit_placeholder(path)) {
		return path;
	}
	const QFileInfo fi(path);
	return fi.dir().filePath(fi.completeBaseName() + QStringLiteral("-%04d.") +
							 fi.suffix());
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

} // namespace

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
		// Pre-generate the audio conforms the render is about to need (the
		// headless equivalent of the application's preview-warmed cache).
		// A timeout here is not fatal: the render's own conform wait is the
		// fallback path.
		if (audio_enabled) {
			QString prewarm_error;
			prewarm_conforms(project, ap, &prewarm_error);
		}

		olive::ExportTask task(sequence, project->color_manager(), params);
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

		if (!result) {
			set_error(task.get_error().isEmpty() ?
						  QStringLiteral("export failed") :
						  task.get_error());
			return OAKENGINE_E_FAILED;
		}
	} catch (const std::exception &e) {
		set_error(QStringLiteral("export failed: %1").arg(e.what()));
		return OAKENGINE_E_FAILED;
	}

	return OAKENGINE_OK;
}

int oakengine_export_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_last_error, buf, buf_size);
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
