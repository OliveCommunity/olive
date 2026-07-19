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

#include "oakengine/renderer.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include <QByteArray>
#include <QElapsedTimer>
#include <QMutex>
#include <QString>
#include <QThread>

#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "render/colorprocessor.h"
#include "render/rendermanager.h"
#include "render/renderticket.h"

namespace
{

// Synchronous-render timeout for one ticket.
constexpr qint64 k_render_timeout_ms = 60000;

struct OakEngineRendererState {
	olive::Sequence *sequence = nullptr; // borrowed, owned by the project
	olive::VideoParams video_params;
	olive::AudioParams audio_params;
	olive::ColorProcessorPtr color_output; // null = reference space, no transform
	olive::RenderMode::Mode mode = olive::RenderMode::k_offline;
	olive::Rational time_base; // frame duration
	QString last_error;
	QMutex ticket_mutex;
	olive::RenderTicketPtr in_flight;
};

struct OakEngineFrameState {
	olive::FramePtr frame;
};

struct OakEngineAudioState {
	olive::SampleBuffer samples;
};

OakEngineRendererState *impl(OakEngineRenderer *h)
{
	return reinterpret_cast<OakEngineRendererState *>(h);
}

const OakEngineRendererState *impl(const OakEngineRenderer *h)
{
	return reinterpret_cast<const OakEngineRendererState *>(h);
}

OakEngineFrameState *impl(OakEngineFrame *h)
{
	return reinterpret_cast<OakEngineFrameState *>(h);
}

const OakEngineFrameState *impl(const OakEngineFrame *h)
{
	return reinterpret_cast<const OakEngineFrameState *>(h);
}

OakEngineAudioState *impl(OakEngineAudioBuffer *h)
{
	return reinterpret_cast<OakEngineAudioState *>(h);
}

const OakEngineAudioState *impl(const OakEngineAudioBuffer *h)
{
	return reinterpret_cast<const OakEngineAudioState *>(h);
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

void set_error(OakEngineRendererState *state, const QString &error)
{
	if (state) {
		state->last_error = error;
	}
}

// The color manager of the project the sequence belongs to.
olive::ColorManager *color_manager_of(olive::Sequence *seq)
{
	if (olive::Project *p = olive::Project::get_project_from_object(seq)) {
		return p->color_manager();
	}
	return nullptr;
}

// Block until the ticket finishes, the timeout elapses or the ticket is
// cancelled. Returns true when the ticket finished (inspect has_result()
// for whether it carries a value).
bool wait_for_ticket(OakEngineRendererState *state,
					 const olive::RenderTicketPtr &ticket)
{
	std::atomic<bool> finished{ false };
	// Functor connection without a context object is a direct connection:
	// the flag is set on whichever thread finishes the ticket.
	const QMetaObject::Connection conn =
		QObject::connect(ticket.get(), &olive::RenderTicket::finished,
						 [&finished]() { finished.store(true); });

	QElapsedTimer timer;
	timer.start();
	while (!finished.load() && !ticket->is_cancelled() &&
		   !timer.hasExpired(k_render_timeout_ms)) {
		QThread::msleep(5);
	}

	// The connection must not outlive this stack frame: the lambda captures
	// a local by reference, and the engine may finish the ticket again (e.g.
	// the worker pool finishing a cancelled ticket) after we stopped waiting.
	QObject::disconnect(conn);

	{
		QMutexLocker locker(&state->ticket_mutex);
		if (state->in_flight == ticket) {
			state->in_flight.reset();
		}
	}

	return finished.load();
}

} // namespace

extern "C"
{

OakEngineRenderer *oakengine_renderer_create(
	OakEngineSequence *seq, int width, int height, int pixel_format,
	int frame_rate_num, int frame_rate_den, const char *output_colorspace)
{
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || width <= 0 || height <= 0 || frame_rate_num <= 0 ||
		frame_rate_den <= 0 || pixel_format < 0 ||
		pixel_format >= olive::PixelFormat::count) {
		return nullptr;
	}

	auto *state = new OakEngineRendererState();
	state->sequence = sequence;
	state->time_base = olive::Rational(frame_rate_den, frame_rate_num);
	state->video_params =
		olive::VideoParams(width, height, state->time_base,
						   static_cast<olive::PixelFormat::Format>(pixel_format),
						   olive::VideoParams::k_internal_channel_count);
	state->audio_params = sequence->get_audio_params();
	if (state->audio_params.sample_rate() <= 0) {
		// Sequences created outside the facade may lack audio parameters;
		// fall back to the engine defaults (48 kHz stereo float).
		state->audio_params = olive::AudioParams(
			48000, olive::core::k_channel_layout_stereo,
			olive::core::SampleFormat::f32_p);
	}

	if (output_colorspace && output_colorspace[0] != '\0') {
		if (olive::ColorManager *colorman = color_manager_of(sequence)) {
			try {
				state->color_output = olive::ColorProcessor::create(
					colorman, colorman->get_reference_color_space(),
					olive::ColorTransform(
						QString::fromUtf8(output_colorspace)));
			} catch (const std::exception &e) {
				state->color_output = nullptr;
				state->last_error = QStringLiteral(
					"failed to create output color transform '%1': %2; "
					"rendering without an output transform")
										.arg(output_colorspace, e.what());
			}
		} else {
			state->last_error = QStringLiteral(
				"sequence is not part of a project; rendering without an "
				"output transform");
		}
	}

	return reinterpret_cast<OakEngineRenderer *>(state);
}

void oakengine_renderer_free(OakEngineRenderer *self)
{
	delete impl(self);
}

int oakengine_renderer_set_mode(OakEngineRenderer *self, int mode)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	switch (mode) {
	case 0:
		impl(self)->mode = olive::RenderMode::k_offline;
		return OAKENGINE_OK;
	case 1:
		impl(self)->mode = olive::RenderMode::k_online;
		return OAKENGINE_OK;
	default:
		return OAKENGINE_E_INVALID;
	}
}

int oakengine_renderer_last_error(const OakEngineRenderer *self, char *buf,
								  int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->last_error, buf, buf_size);
}

OakEngineFrame *oakengine_renderer_render_frame(OakEngineRenderer *self,
												int64_t timestamp)
{
	if (!self) {
		return nullptr;
	}
	OakEngineRendererState *state = impl(self);
	set_error(state, QString());

	if (!olive::RenderManager::instance()) {
		set_error(state,
				  QStringLiteral("engine not initialized with "
								 "OAKENGINE_INIT_RENDER"));
		return nullptr;
	}

	try {
		const olive::Rational time =
			olive::core::Timecode::timestamp_to_time(timestamp,
													 state->time_base);

		olive::RenderManager::RenderVideoParams params(
			state->sequence, state->video_params, state->audio_params, time,
			color_manager_of(state->sequence), state->mode);
		params.force_size = QSize(state->video_params.width(),
								  state->video_params.height());
		params.force_format = state->video_params.format();
		params.force_channel_count = state->video_params.channel_count();
		params.force_color_output = state->color_output;
		params.return_type = olive::RenderManager::k_frame;

		olive::RenderTicketPtr ticket =
			olive::RenderManager::instance()->render_frame(params);
		{
			QMutexLocker locker(&state->ticket_mutex);
			state->in_flight = ticket;
		}

		if (!wait_for_ticket(state, ticket) || !ticket->has_result()) {
			set_error(state,
					  ticket->is_cancelled() ?
						  QStringLiteral("render cancelled") :
						  QStringLiteral(
							  "render produced no frame (timeout or failure)"));
			return nullptr;
		}

		olive::FramePtr frame = ticket->get().value<olive::FramePtr>();
		if (!frame || !frame->is_allocated()) {
			set_error(state, QStringLiteral("render result was empty"));
			return nullptr;
		}

		return reinterpret_cast<OakEngineFrame *>(
			new OakEngineFrameState{ frame });
	} catch (const std::exception &e) {
		set_error(state,
				  QStringLiteral("render failed: %1").arg(e.what()));
		return nullptr;
	}
}

OakEngineAudioBuffer *oakengine_renderer_render_audio(
	OakEngineRenderer *self, int64_t start_timestamp, int64_t length_timestamp)
{
	if (!self || length_timestamp < 0) {
		return nullptr;
	}
	OakEngineRendererState *state = impl(self);
	set_error(state, QString());

	if (!olive::RenderManager::instance()) {
		set_error(state,
				  QStringLiteral("engine not initialized with "
								 "OAKENGINE_INIT_RENDER"));
		return nullptr;
	}

	try {
		const olive::Rational in_time =
			olive::core::Timecode::timestamp_to_time(start_timestamp,
													 state->time_base);
		const olive::Rational length_time =
			olive::core::Timecode::timestamp_to_time(length_timestamp,
													 state->time_base);

		olive::RenderManager::RenderAudioParams params(
			state->sequence, olive::TimeRange(in_time, in_time + length_time),
			state->audio_params, state->mode);

		olive::RenderTicketPtr ticket =
			olive::RenderManager::instance()->render_audio(params);
		{
			QMutexLocker locker(&state->ticket_mutex);
			state->in_flight = ticket;
		}

		if (!wait_for_ticket(state, ticket) || !ticket->has_result()) {
			set_error(state,
					  ticket->is_cancelled() ?
						  QStringLiteral("render cancelled") :
						  QStringLiteral(
							  "audio render produced nothing (timeout or failure)"));
			return nullptr;
		}

		olive::SampleBuffer samples =
			ticket->get().value<olive::SampleBuffer>();
		if (!samples.is_allocated()) {
			set_error(state, QStringLiteral("audio render result was empty"));
			return nullptr;
		}

		return reinterpret_cast<OakEngineAudioBuffer *>(
			new OakEngineAudioState{ samples });
	} catch (const std::exception &e) {
		set_error(state,
				  QStringLiteral("audio render failed: %1").arg(e.what()));
		return nullptr;
	}
}

void oakengine_renderer_cancel(OakEngineRenderer *self)
{
	if (!self) {
		return;
	}
	OakEngineRendererState *state = impl(self);
	olive::RenderTicketPtr ticket;
	{
		QMutexLocker locker(&state->ticket_mutex);
		ticket = state->in_flight;
	}
	if (!ticket) {
		return;
	}
	ticket->cancel();
	if (olive::RenderManager::instance()) {
		olive::RenderManager::instance()->remove_ticket(ticket);
	}
}

/* ---- OakEngineFrame ----------------------------------------------------- */

int oakengine_frame_width(const OakEngineFrame *self)
{
	return self && impl(self)->frame ? impl(self)->frame->width() : 0;
}

int oakengine_frame_height(const OakEngineFrame *self)
{
	return self && impl(self)->frame ? impl(self)->frame->height() : 0;
}

int oakengine_frame_format(const OakEngineFrame *self)
{
	return self && impl(self)->frame ?
			   static_cast<int>(impl(self)->frame->format()) :
			   -1;
}

int oakengine_frame_channel_count(const OakEngineFrame *self)
{
	return self && impl(self)->frame ? impl(self)->frame->channel_count() : 0;
}

int oakengine_frame_linesize_bytes(const OakEngineFrame *self)
{
	return self && impl(self)->frame ? impl(self)->frame->linesize_bytes() : 0;
}

const void *oakengine_frame_data(const OakEngineFrame *self)
{
	return self && impl(self)->frame && impl(self)->frame->is_allocated() ?
			   impl(self)->frame->const_data() :
			   nullptr;
}

void oakengine_frame_free(OakEngineFrame *self)
{
	delete impl(self);
}

/* ---- OakEngineAudioBuffer ------------------------------------------------ */

int oakengine_audio_sample_rate(const OakEngineAudioBuffer *self)
{
	return self ? impl(self)->samples.audio_params().sample_rate() : 0;
}

int oakengine_audio_channel_count(const OakEngineAudioBuffer *self)
{
	return self ? impl(self)->samples.channel_count() : 0;
}

int64_t oakengine_audio_sample_count(const OakEngineAudioBuffer *self)
{
	return self ? int64_t(impl(self)->samples.sample_count()) : 0;
}

const float *oakengine_audio_data(const OakEngineAudioBuffer *self,
								  int channel)
{
	if (!self || channel < 0 ||
		channel >= impl(self)->samples.channel_count()) {
		return nullptr;
	}
	return impl(self)->samples.data(channel);
}

void oakengine_audio_free(OakEngineAudioBuffer *self)
{
	delete impl(self);
}

} // extern "C"
