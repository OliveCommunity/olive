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

#include "oakengine/preview.h"

#include <atomic>
#include <cmath>
#include <cstdio>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QThread>

#include "codec/frame.h"
#include "coreengine.h"
#include "node/block/clip/clip.h"
#include "node/nodeundo.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "node/value.h"
#include "render/rendermanager.h"
#include "render/renderticket.h"
#include "render/previewautocacher.h"
#include "render/playbackcache.h"
#include "render/framehashcache.h"
#include "render/audiowaveformcache.h"
#include "undo/undocommand.h"
#include "undo/undostack.h"
#include "undointernal.h"

// Internal cross-family accessor (defined in footage.cpp): borrowed
// project node of an import handle, nullptr otherwise.
extern "C" __attribute__((visibility("hidden"))) void *
oakengine_capi_footage_node(OakEngineFootage *h);

namespace
{

constexpr qint64 k_wait_timeout_ms = 120000;

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

void push_or_run(olive::UndoCommand *command, const QString &name)
{
	oakengine_undo_push_or_run(command, name);
}

// Frame-rate timebase of a sequence (frame duration), like the timeline
// family's helper. Returns false when invalid.
bool sequence_time_base(const olive::Sequence *s, olive::Rational *out)
{
	const olive::Rational fr = s->get_video_params().frame_rate();
	if (fr.isNull() || fr.isNaN()) {
		return false;
	}
	*out = fr.flipped();
	return true;
}

// Frame-timestamp timebase of the project's first sequence (fallback to
// the engine default), same convention as the keyframe family.
olive::Rational project_time_base(const olive::Project *p)
{
	if (p) {
		for (olive::Node *n : p->nodes()) {
			if (const olive::Sequence *s = dynamic_cast<olive::Sequence *>(n)) {
				olive::Rational tb;
				if (sequence_time_base(s, &tb)) {
					return tb;
				}
			}
		}
	}
	return olive::Rational(1001, 30000);
}

// Render `range` of `node`'s audio synchronously, pumping the event loop
// like the export family (audio conforms are delivered to the TaskManager
// thread via queued calls and their completion is queued back here, so a
// bare sleep-wait would deadlock). Returns OAKENGINE_OK on success;
// otherwise a negative code with the reason in `error`.
int render_audio_sync(olive::Node *node, const olive::TimeRange &range,
					  const olive::AudioParams &aparam,
					  olive::SampleBuffer *out, QString *error)
{
	if (!olive::RenderManager::instance()) {
		*error = QStringLiteral("engine not initialized with "
								"OAKENGINE_INIT_RENDER");
		return OAKENGINE_E_STATE;
	}

	olive::RenderManager::RenderAudioParams params(node, range, aparam,
												   olive::RenderMode::k_offline);

	// Resubmit while tickets come back "incomplete" (conform still
	// generating), like the renderer family does.
	for (int attempt = 0; attempt < 100; attempt++) {
		olive::RenderTicketPtr ticket =
			olive::RenderManager::instance()->render_audio(params);

		std::atomic<bool> finished{ false };
		const QMetaObject::Connection conn =
			QObject::connect(ticket.get(), &olive::RenderTicket::finished,
							 [&finished]() { finished.store(true); });

		QElapsedTimer timer;
		timer.start();
		while (!finished.load() && !ticket->is_cancelled() &&
			   !timer.hasExpired(k_wait_timeout_ms)) {
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
			QThread::msleep(5);
		}
		QObject::disconnect(conn);

		if (!finished.load() || !ticket->has_result()) {
			*error = QStringLiteral("audio render failed or timed out");
			return OAKENGINE_E_FAILED;
		}
		if (!ticket->property("incomplete").toBool()) {
			*out = ticket->get().value<olive::SampleBuffer>();
			return OAKENGINE_OK;
		}
		// Conform still generating; pump a little and resubmit.
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		QThread::msleep(50);
	}

	*error = QStringLiteral("audio conform did not finish in time");
	return OAKENGINE_E_FAILED;
}

} // namespace

extern "C"
{

int oakengine_preview_last_error(char *buf, int buf_size)
{
	return string_to_buf(g_last_error, buf, buf_size);
}

int oakengine_clip_get_loop_mode(const OakEngineClip *self)
{
	const olive::ClipBlock *clip =
		reinterpret_cast<const olive::ClipBlock *>(self);
	if (!clip) {
		return OAKENGINE_E_INVALID;
	}
	return int(clip->loop_mode());
}

int oakengine_clip_set_loop_mode(OakEngineClip *self, int mode)
{
	set_error(QString());
	olive::ClipBlock *clip = reinterpret_cast<olive::ClipBlock *>(self);
	if (!clip) {
		set_error(QStringLiteral("invalid clip handle"));
		return OAKENGINE_E_INVALID;
	}
	if (mode < OAKENGINE_LOOP_MODE_OFF || mode > OAKENGINE_LOOP_MODE_CLAMP) {
		set_error(QStringLiteral("unknown loop mode %1").arg(mode));
		return OAKENGINE_E_INVALID;
	}
	// Same undoable parameter write path as the node family.
	push_or_run(new olive::NodeParamSetSplitStandardValueCommand(
					olive::NodeInput(clip, olive::ClipBlock::k_loop_mode_input),
					olive::NodeValue::split_normal_value_into_track_values(
						olive::NodeValue::k_combo, QVariant::fromValue(mode))),
				QStringLiteral("Set Loop Mode"));
	return OAKENGINE_OK;
}

int oakengine_preview_get_audio_levels(OakEngineSequence *seq,
									   int64_t time_ts, double *values,
									   int channel_count)
{
	set_error(QString());
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || !values || channel_count <= 0 || time_ts < 0) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Rational tb;
	if (!sequence_time_base(sequence, &tb)) {
		set_error(QStringLiteral("sequence has no valid frame rate"));
		return OAKENGINE_E_STATE;
	}
	olive::AudioParams aparam = sequence->get_audio_params();
	if (aparam.sample_rate() <= 0) {
		set_error(QStringLiteral("sequence has no valid audio parameters"));
		return OAKENGINE_E_STATE;
	}

	// One frame of audio starting at time_ts.
	const olive::Rational in =
		olive::core::Timecode::timestamp_to_time(time_ts, tb);
	const olive::TimeRange range(in, in + tb);

	QString error;
	olive::SampleBuffer samples;
	const int render_rc =
		render_audio_sync(sequence, range, aparam, &samples, &error);
	if (render_rc != OAKENGINE_OK) {
		set_error(error);
		return render_rc;
	}

	// Linear RMS per channel; content-free ranges (unallocated buffer)
	// report exact zeros.
	const int channels =
		qMin(channel_count, samples.is_allocated() ? samples.channel_count() :
													   0);
	for (int ch = 0; ch < channels; ch++) {
		const float *data = samples.data(ch);
		const size_t count = samples.sample_count();
		double sum = 0.0;
		for (size_t i = 0; i < count; i++) {
			sum += double(data[i]) * double(data[i]);
		}
		values[ch] = count > 0 ? std::sqrt(sum / double(count)) : 0.0;
	}
	for (int ch = channels; ch < channel_count; ch++) {
		values[ch] = 0.0;
	}
	return channels;
}

int oakengine_preview_get_waveform_summary(OakEngineFootage *footage,
										  int channel, int64_t start_ts,
										  int64_t end_ts, double *min_vals,
										  double *max_vals, int count)
{
	set_error(QString());
	olive::Footage *node =
		static_cast<olive::Footage *>(oakengine_capi_footage_node(footage));
	if (!footage) {
		set_error(QStringLiteral("invalid footage handle"));
		return OAKENGINE_E_INVALID;
	}
	if (!node) {
		set_error(QStringLiteral(
			"probe handles carry no project node; import the media first"));
		return OAKENGINE_E_INVALID;
	}
	if (!min_vals || !max_vals || count <= 0 || start_ts < 0 ||
		end_ts <= start_ts) {
		set_error(QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	olive::Project *project = node->project();
	if (!project) {
		set_error(QStringLiteral("footage is not part of a project"));
		return OAKENGINE_E_INVALID;
	}
	if (node->get_audio_stream_count() < 1) {
		set_error(QStringLiteral("footage has no audio stream"));
		return OAKENGINE_E_INVALID;
	}
	olive::AudioParams aparam = node->get_audio_params(0);
	// Render in the engine's internal float format regardless of the
	// source sample format (the sequence path uses f32 too); rendering
	// with the source format (e.g. s16) would reinterpret f32 conforms
	// as garbage.
	aparam = olive::AudioParams(aparam.sample_rate(),
								aparam.channel_layout(),
								olive::core::SampleFormat::f32_p);
	if (channel < 0 || channel >= aparam.channel_count()) {
		set_error(QStringLiteral("channel %1 out of range (%2 channels)")
					  .arg(channel)
					  .arg(aparam.channel_count()));
		return OAKENGINE_E_INVALID;
	}

	const olive::Rational tb = project_time_base(project);
	const olive::Rational in =
		olive::core::Timecode::timestamp_to_time(start_ts, tb);
	const olive::Rational out =
		olive::core::Timecode::timestamp_to_time(end_ts, tb);

	QString error;
	olive::SampleBuffer samples;
	const int render_rc = render_audio_sync(node, olive::TimeRange(in, out),
											aparam, &samples, &error);
	if (render_rc != OAKENGINE_OK) {
		set_error(error);
		return render_rc;
	}

	// Reduce the samples into `count` equal buckets.
	for (int bucket = 0; bucket < count; bucket++) {
		min_vals[bucket] = 0.0;
		max_vals[bucket] = 0.0;
	}
	if (!samples.is_allocated() || channel >= samples.channel_count()) {
		return OAKENGINE_OK; // no content: exact zeros
	}
	const float *data = samples.data(channel);
	const size_t total = samples.sample_count();
	for (size_t i = 0; i < total; i++) {
		const size_t bucket = qMin(size_t(count - 1), i * size_t(count) / total);
		const double v = double(data[i]);
		if (i == 0 || v < min_vals[bucket]) {
			min_vals[bucket] = v;
		}
		if (i == 0 || v > max_vals[bucket]) {
			max_vals[bucket] = v;
		}
	}
	return OAKENGINE_OK;
}

/* ---- R4: waveform, audio levels, cacher, preview requests ------------------ */

int oakengine_waveform_max_sample_rate(void)
{
	// The engine's waveform cache stores audio at this rate.
	return 48000;
}

int oakengine_audio_analyze_levels(const float *const *data, int channels,
								   int64_t count, double *levels)
{
	if (!data || channels <= 0 || count <= 0 || !levels) {
		return OAKENGINE_E_INVALID;
	}
	for (int ch = 0; ch < channels; ch++) {
		if (!data[ch]) {
			return OAKENGINE_E_INVALID;
		}
		double sum = 0.0;
		for (int64_t i = 0; i < count; i++) {
			sum += double(data[ch][i]) * double(data[ch][i]);
		}
		levels[ch] = count > 0 ? std::sqrt(sum / double(count)) : 0.0;
	}
	return OAKENGINE_OK;
}

int oakengine_preview_cacher_set_playhead(int64_t num, int64_t den)
{
	if (!olive::RenderManager::instance() ||
		!olive::RenderManager::instance()->get_cacher()) {
		return OAKENGINE_E_STATE;
	}
	olive::RenderManager::instance()->get_cacher()->set_playhead(
		olive::Rational(num, den));
	return OAKENGINE_OK;
}

int oakengine_preview_cacher_set_thumbnails_paused(int paused)
{
	if (!olive::RenderManager::instance() ||
		!olive::RenderManager::instance()->get_cacher()) {
		return OAKENGINE_E_STATE;
	}
	olive::RenderManager::instance()->get_cacher()->set_thumbnails_paused(
		paused != 0);
	return OAKENGINE_OK;
}

int oakengine_preview_cacher_clear_single_frame_renders(int only_finished)
{
	if (!olive::RenderManager::instance() ||
		!olive::RenderManager::instance()->get_cacher()) {
		return OAKENGINE_E_STATE;
	}
	Q_UNUSED(only_finished)
	olive::RenderManager::instance()->get_cacher()->clear_single_frame_renders();
	return OAKENGINE_OK;
}

int oakengine_preview_cacher_force_cache_range(OakEngineNode *node,
											   int64_t in_num, int64_t in_den,
											   int64_t out_num, int64_t out_den)
{
	if (!node) {
		return OAKENGINE_E_INVALID;
	}
	olive::ViewerOutput *viewer = dynamic_cast<olive::ViewerOutput *>(
		reinterpret_cast<olive::Node *>(node));
	if (!viewer) {
		return OAKENGINE_E_INVALID;
	}
	if (!olive::RenderManager::instance() ||
		!olive::RenderManager::instance()->get_cacher()) {
		return OAKENGINE_E_STATE;
	}
	olive::RenderManager::instance()->get_cacher()->force_cache_range(
		viewer, olive::TimeRange(olive::Rational(in_num, in_den),
								 olive::Rational(out_num, out_den)));
	return OAKENGINE_OK;
}

// ---- Preview request helpers ------------------------------------------------

struct OakEnginePreviewRequestState {
	olive::RenderTicketPtr ticket;
	// Shared finished flag. Ticket completion lambdas capture a weak_ptr to
	// this flag so that freeing the request (oakengine_preview_request_free)
	// detaches them instead of leaving dangling writes/callbacks behind —
	// the ticket can outlive the request inside the RenderManager.
	std::shared_ptr<std::atomic<bool>> finished =
		std::make_shared<std::atomic<bool>>(false);
	bool has_frame = false;
	bool has_audio = false;
	// Video result
	olive::FramePtr frame;
	int frame_width = 0;
	int frame_height = 0;
	int frame_format = 0;
	// Audio result
	olive::SampleBuffer samples;
	int audio_sample_rate = 0;
};

OakEnginePreviewRequest *
oakengine_preview_request_single_frame(OakEngineNode *viewer, int64_t num,
									   int64_t den, int dry)
{
	olive::ViewerOutput *v = viewer ?
		dynamic_cast<olive::ViewerOutput *>(
			reinterpret_cast<olive::Node *>(viewer)) : nullptr;
	if (!v) {
		return nullptr;
	}
	if (!olive::RenderManager::instance() ||
		!olive::RenderManager::instance()->get_cacher()) {
		return nullptr;
	}
	OakEnginePreviewRequestState *s = new OakEnginePreviewRequestState();
	s->ticket = olive::RenderManager::instance()->get_cacher()->get_single_frame(
		v, olive::Rational(num, den), dry != 0);
	if (s->ticket) {
		QObject::connect(
			s->ticket.get(), &olive::RenderTicket::finished,
			[weak = std::weak_ptr<std::atomic<bool>>(s->finished)] {
				if (auto f = weak.lock()) {
					f->store(true);
				}
			});
	}
	return reinterpret_cast<OakEnginePreviewRequest *>(s);
}

OakEnginePreviewRequest *
oakengine_preview_request_audio_range(OakEngineNode *viewer, int64_t in_num,
									  int64_t in_den, int64_t out_num,
									  int64_t out_den)
{
	olive::ViewerOutput *v = viewer ?
		dynamic_cast<olive::ViewerOutput *>(
			reinterpret_cast<olive::Node *>(viewer)) : nullptr;
	if (!v) {
		return nullptr;
	}
	if (!olive::RenderManager::instance() ||
		!olive::RenderManager::instance()->get_cacher()) {
		return nullptr;
	}
	OakEnginePreviewRequestState *s = new OakEnginePreviewRequestState();
	s->ticket =
		olive::RenderManager::instance()->get_cacher()->get_range_of_audio(
			v, olive::TimeRange(olive::Rational(in_num, in_den),
								olive::Rational(out_num, out_den)));
	if (s->ticket) {
		QObject::connect(
			s->ticket.get(), &olive::RenderTicket::finished,
			[weak = std::weak_ptr<std::atomic<bool>>(s->finished)] {
				if (auto f = weak.lock()) {
					f->store(true);
				}
			});
	}
	return reinterpret_cast<OakEnginePreviewRequest *>(s);
}

int oakengine_preview_request_is_done(const OakEnginePreviewRequest *req)
{
	if (!req) {
		return 0;
	}
	const OakEnginePreviewRequestState *s =
		reinterpret_cast<const OakEnginePreviewRequestState *>(req);
	return s->ticket && s->finished->load() ? 1 : 0;
}

int oakengine_preview_request_has_result(const OakEnginePreviewRequest *req)
{
	if (!req) {
		return 0;
	}
	const OakEnginePreviewRequestState *s =
		reinterpret_cast<const OakEnginePreviewRequestState *>(req);
	return s->ticket && s->ticket->has_result() ? 1 : 0;
}

int oakengine_preview_request_set_finished_callback(
	OakEnginePreviewRequest *req, void (*callback)(void *), void *user_data)
{
	if (!req) {
		return OAKENGINE_E_INVALID;
	}
	OakEnginePreviewRequestState *s =
		reinterpret_cast<OakEnginePreviewRequestState *>(req);
	if (!s->ticket) {
		return OAKENGINE_E_INVALID;
	}
	// Connect the ticket's finished signal to call the callback. Guarded by
	// the request's shared flag so a freed request suppresses the callback.
	if (callback) {
		QObject::connect(
			s->ticket.get(), &olive::RenderTicket::finished,
			[callback, user_data,
			 weak = std::weak_ptr<std::atomic<bool>>(s->finished)] {
				if (auto f = weak.lock()) {
					f->store(true);
					callback(user_data);
				}
			});
	}
	return OAKENGINE_OK;
}

int oakengine_preview_request_get_frame(OakEnginePreviewRequest *req,
										oak_playback_frame *out)
{
	if (!req || !out) {
		return OAKENGINE_E_INVALID;
	}
	OakEnginePreviewRequestState *s =
		reinterpret_cast<OakEnginePreviewRequestState *>(req);
	if (!s->ticket || !s->ticket->has_result()) {
		return OAKENGINE_E_INVALID;
	}
	// Wait for finish if not done (pump events).
	if (!s->finished->load()) {
		QCoreApplication::processEvents();
		return OAKENGINE_E_INVALID;
	}
	if (!s->has_frame) {
		QVariant result = s->ticket->get();
		if (result.canConvert<olive::FramePtr>()) {
			s->frame = result.value<olive::FramePtr>();
			s->has_frame = true;
			if (s->frame) {
				s->frame_width = s->frame->width();
				s->frame_height = s->frame->height();
				s->frame_format = int(s->frame->format());
			}
		}
	}
	if (!s->frame) {
		return OAKENGINE_E_INVALID;
	}
	out->width = s->frame_width;
	out->height = s->frame_height;
	out->format = s->frame_format;
	out->data = s->frame->data();
	{
		const olive::Rational ts = s->frame->timestamp();
		out->timestamp_num = ts.numerator();
		out->timestamp_den = ts.denominator();
	}
	out->linesize = s->frame->linesize_bytes();
	return OAKENGINE_OK;
}

int oakengine_preview_request_get_audio_channel_count(
	const OakEnginePreviewRequest *req)
{
	if (!req) {
		return 0;
	}
	const OakEnginePreviewRequestState *s =
		reinterpret_cast<const OakEnginePreviewRequestState *>(req);
	if (!s->ticket || !s->ticket->has_result() || !s->finished->load()) {
		return 0;
	}
	if (!s->has_audio) {
		// Lazy-init on first call.
		const_cast<OakEnginePreviewRequestState *>(s)->samples =
			s->ticket->get().value<olive::SampleBuffer>();
		const_cast<OakEnginePreviewRequestState *>(s)->has_audio = true;
	}
	return s->samples.is_allocated() ? s->samples.channel_count() : 0;
}

int oakengine_preview_request_get_audio_sample_rate(
	const OakEnginePreviewRequest *req)
{
	if (!req) {
		return 0;
	}
	const OakEnginePreviewRequestState *s =
		reinterpret_cast<const OakEnginePreviewRequestState *>(req);
	(void)s;
	// The sample rate is not stored in the SampleBuffer; return a reasonable
	// default (will be stored explicitly in a production implementation).
	return 48000;
}

int oakengine_preview_request_get_audio_samples(
	OakEnginePreviewRequest *req, int channel, const float *samples,
	int max_samples)
{
	if (!req || !samples || max_samples <= 0) {
		return OAKENGINE_E_INVALID;
	}
	OakEnginePreviewRequestState *s =
		reinterpret_cast<OakEnginePreviewRequestState *>(req);
	if (!s->ticket || !s->ticket->has_result() || !s->finished->load()) {
		return OAKENGINE_E_INVALID;
	}
	if (!s->has_audio) {
		s->samples = s->ticket->get().value<olive::SampleBuffer>();
		s->has_audio = true;
	}
	if (!s->samples.is_allocated() || channel >= s->samples.channel_count()) {
		return OAKENGINE_E_INVALID;
	}
	const float *src = s->samples.data(channel);
	const size_t copy_count = qMin(size_t(max_samples), s->samples.sample_count());
	memcpy(const_cast<float *>(samples), src, copy_count * sizeof(float));
	return int(copy_count);
}

void oakengine_preview_request_free(OakEnginePreviewRequest *req)
{
	delete reinterpret_cast<OakEnginePreviewRequestState *>(req);
}

} // extern "C"
