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

#include "oakengine/playback.h"

#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <QThread>

#include "audio/audiomanager.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/renderer.h"
#include "render/rendermanager.h"

namespace
{

// Audio block length rendered per wall interval (the viewer's
// k_audio_playback_interval), and how many blocks are kept queued.
constexpr double k_audio_interval_s = 0.25;
constexpr double k_audio_ahead_s = 2 * k_audio_interval_s;

constexpr int k_state_stopped = 0;
constexpr int k_state_playing = 1;
constexpr int k_state_paused = 2;

struct OakEnginePlaybackState {
	olive::Sequence *sequence = nullptr; // borrowed, owned by the project
	OakEngineRenderer *renderer = nullptr;
	olive::Rational time_base; // frame duration in seconds
	double fps = 0.0; // frames per second

	void (*frame_cb)(const oak_playback_frame *, void *) = nullptr;
	void *frame_cb_data = nullptr;
	void (*audio_cb)(const oak_playback_audio *, void *) = nullptr;
	void *audio_cb_data = nullptr;

	std::thread pull_thread;
	// Set by the pull thread right before it exits; lets free() wait out a
	// detached thread (stop called from inside a callback detaches, see
	// stop_and_join) before the state is deleted.
	std::atomic<bool> thread_done{ true };
	std::atomic<int> state{ k_state_stopped };
	std::atomic<double> speed{ 1.0 };
	// Anchors for the clock-driven position (see get_position).
	std::atomic<int64_t> anchor_ts{ 0 }; // sequence position at anchor
	std::atomic<double> anchor_clock{ 0.0 }; // master clock at anchor
	std::atomic<int64_t> next_frame_ts{ 0 };
	std::atomic<double> next_audio_time{ 0.0 }; // seconds
	std::atomic<int64_t> paused_ts{ 0 };
	std::atomic<int64_t> last_start_ts{ 0 };
	QElapsedTimer wall_timer;
	QString last_error;
};

OakEnginePlaybackState *impl(OakEnginePlayback *h)
{
	return reinterpret_cast<OakEnginePlaybackState *>(h);
}

const OakEnginePlaybackState *impl(const OakEnginePlayback *h)
{
	return reinterpret_cast<const OakEnginePlaybackState *>(h);
}

int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

void set_error(OakEnginePlaybackState *state, const QString &error)
{
	if (state) {
		state->last_error = error;
	}
}

// The master clock in seconds: the audio output clock when an
// AudioManager instance is consuming (latency-compensated), otherwise
// the wall clock of this instance.
double master_clock_seconds(const OakEnginePlaybackState *state)
{
	// Trust the audio output clock only while this instance is actually
	// feeding it: on a silent sequence nothing is consumed, so the output
	// clock never advances and would freeze the position at the anchor.
	if (state->sequence->get_audio_params().channel_count() > 0 &&
		olive::AudioManager::instance()) {
		const double s = olive::AudioManager::instance()->seconds();
		if (s >= 0.0) {
			return s;
		}
	}
	return double(state->wall_timer.elapsed()) / 1000.0;
}

int64_t position_ts_of(const OakEnginePlaybackState *state, double clock_s)
{
	const double advanced =
		(clock_s - state->anchor_clock.load()) * state->speed.load();
	return state->anchor_ts.load() + int64_t(advanced * state->fps);
}

// Deliver one rendered frame through the callback; the payload stays
// valid for the duration of the call only.
void deliver_frame(OakEnginePlaybackState *state, OakEngineFrame *frame,
				   int64_t ts)
{
	if (!state->frame_cb) {
		oakengine_frame_free(frame);
		return;
	}
	oak_playback_frame payload;
	payload.timestamp = ts;
	payload.width = oakengine_frame_width(frame);
	payload.height = oakengine_frame_height(frame);
	payload.format = oakengine_frame_format(frame);
	payload.linesize = oakengine_frame_linesize_bytes(frame);
	payload.data = oakengine_frame_data(frame);
	state->frame_cb(&payload, state->frame_cb_data);
	oakengine_frame_free(frame);
}

// Deliver one rendered audio block through the AudioManager (when an
// instance exists) and the callback; the payload stays valid for the
// duration of the call only.
void deliver_audio(OakEnginePlaybackState *state, OakEngineAudioBuffer *buf,
				   int64_t start_ts)
{
	const int channels = oakengine_audio_channel_count(buf);
	const int64_t count = oakengine_audio_sample_count(buf);
	const int rate = oakengine_audio_sample_rate(buf);

	std::vector<const float *> channel_ptrs(size_t(channels), nullptr);
	for (int i = 0; i < channels; i++) {
		channel_ptrs[size_t(i)] = oakengine_audio_data(buf, i);
	}

	if (olive::AudioManager::instance() && count > 0) {
		// Pack planar float into interleaved float32 for the output.
		QByteArray pack;
		pack.resize(int(count * channels * int64_t(sizeof(float))));
		auto *dst = reinterpret_cast<float *>(pack.data());
		for (int64_t i = 0; i < count; i++) {
			for (int ch = 0; ch < channels; ch++) {
				*dst++ = channel_ptrs[size_t(ch)][i];
			}
		}
		const olive::AudioParams params(
			rate, state->sequence->get_audio_params().channel_layout(),
			olive::core::SampleFormat::f32);
		olive::AudioManager::instance()->push_to_output(params, pack);
	}

	if (state->audio_cb) {
		oak_playback_audio payload;
		payload.start_ts = start_ts;
		payload.channels = channels;
		payload.sample_rate = rate;
		payload.sample_count = count;
		payload.channel_data = channel_ptrs.data();
		state->audio_cb(&payload, state->audio_cb_data);
	}
	oakengine_audio_free(buf);
}

void pull_loop(OakEnginePlaybackState *state)
{
	const olive::Rational video_length = state->sequence->get_length();
	// A sequence without timeline clips (node graph only) has no defined
	// end: treat it as unbounded instead of stopping instantly.
	const bool bounded = video_length > 0;
	const int64_t end_ts =
		bounded ? olive::core::Timecode::time_to_timestamp(
			video_length, state->time_base, olive::core::Timecode::k_round) :
				  INT64_MAX;
	const double end_time = bounded ? video_length.to_double() : DBL_MAX;

	while (state->state.load() != k_state_stopped) {
		if (state->state.load() == k_state_paused) {
			QThread::msleep(5);
			continue;
		}

		const double clock_s = master_clock_seconds(state);
		const double speed = state->speed.load();
		const int64_t step = std::max<int64_t>(1, llround(speed));

		// Video: deliver every due frame (rendering is synchronous, so
		// the pull thread is the pacer; when rendering is slower than
		// realtime the loop naturally delivers late).
		const int64_t due_ts = position_ts_of(state, clock_s);
		while (state->state.load() == k_state_playing &&
			   state->next_frame_ts.load() <= due_ts &&
			   state->next_frame_ts.load() < end_ts) {
			const int64_t ts = state->next_frame_ts.load();
			OakEngineFrame *frame =
				oakengine_renderer_render_frame(state->renderer, ts);
			if (!frame) {
				// Skip a failed frame instead of stalling the loop; the
				// renderer's error is mirrored into our handle.
				char err[256];
				err[0] = '\0';
				oakengine_renderer_last_error(state->renderer, err,
											  sizeof(err));
				set_error(state, QString::fromUtf8(err));
				state->next_frame_ts.store(ts + step);
				continue;
			}
			deliver_frame(state, frame, ts);
			state->next_frame_ts.store(ts + step);
		}

		// Audio: keep k_audio_ahead_s of sequence time queued.
		if (state->sequence->get_audio_params().channel_count() > 0) {
			const double audio_due =
				state->anchor_ts.load() * state->time_base.to_double() +
				(clock_s - state->anchor_clock.load()) * speed +
				k_audio_ahead_s;
			while (state->state.load() == k_state_playing &&
				   state->next_audio_time.load() <
					   std::min(audio_due, end_time)) {
				const double t = state->next_audio_time.load();
				const int64_t start_ts =
					olive::core::Timecode::time_to_timestamp(
						olive::Rational::from_double(t), state->time_base,
						olive::core::Timecode::k_round);
				const int64_t len_ts =
					std::max<int64_t>(1, llround(k_audio_interval_s * speed *
												 state->fps));
				OakEngineAudioBuffer *buf =
					oakengine_renderer_render_audio(state->renderer, start_ts,
													len_ts);
				if (!buf) {
					char err[256];
					err[0] = '\0';
					oakengine_renderer_last_error(state->renderer, err,
												  sizeof(err));
					set_error(state, QString::fromUtf8(err));
				} else {
					deliver_audio(state, buf, start_ts);
				}
				state->next_audio_time.store(t + k_audio_interval_s * speed);
			}
		}

		// End of stream: both queues exhausted.
		const bool video_done = state->next_frame_ts.load() >= end_ts;
		const bool audio_done =
			state->sequence->get_audio_params().channel_count() == 0 ||
			state->next_audio_time.load() >= end_time;
		if (video_done && audio_done) {
			state->paused_ts.store(end_ts);
			state->last_start_ts.store(end_ts);
			state->state.store(k_state_stopped);
			break;
		}

		QThread::msleep(4);
	}

	state->thread_done.store(true);
}

void stop_and_join(OakEnginePlaybackState *state)
{
	state->state.store(k_state_stopped);
	if (state->renderer) {
		oakengine_renderer_cancel(state->renderer);
	}
	if (state->pull_thread.joinable()) {
		if (state->pull_thread.get_id() == std::this_thread::get_id()) {
			// Called from inside a callback on the pull thread: joining
			// ourselves would deadlock. Detach instead; the loop exits on
			// the stopped state and free() waits on thread_done.
			state->pull_thread.detach();
		} else {
			state->pull_thread.join();
		}
	}
}

} // namespace

extern "C"
{

OakEnginePlayback *oakengine_playback_create(OakEngineSequence *seq, int width,
											 int height, int fps_num,
											 int fps_den)
{
	olive::Sequence *sequence = reinterpret_cast<olive::Sequence *>(seq);
	if (!sequence || width <= 0 || height <= 0 || fps_num <= 0 ||
		fps_den <= 0) {
		return nullptr;
	}

	auto *state = new OakEnginePlaybackState();
	state->sequence = sequence;
	state->time_base = olive::Rational(fps_den, fps_num);
	state->fps = double(fps_num) / double(fps_den);
	// Frames: RGBA float16 like the renderer family's default CPU frames;
	// the colorspace is the sequence's reference space (no transform).
	state->renderer = oakengine_renderer_create(seq, width, height, 3 /* f16 */,
												fps_num, fps_den, nullptr);
	if (!state->renderer) {
		delete state;
		return nullptr;
	}
	state->wall_timer.start();
	return reinterpret_cast<OakEnginePlayback *>(state);
}

void oakengine_playback_free(OakEnginePlayback *self)
{
	if (!self) {
		return;
	}
	OakEnginePlaybackState *state = impl(self);
	stop_and_join(state);
	// A stop from inside a callback detaches the pull thread; wait for it
	// to actually exit before the state goes away. (Calling free() from a
	// callback itself is forbidden, see playback.h.)
	while (!state->thread_done.load()) {
		QThread::msleep(1);
	}
	if (state->renderer) {
		oakengine_renderer_free(state->renderer);
	}
	delete state;
}

int oakengine_playback_set_frame_callback(
	OakEnginePlayback *self,
	void (*on_frame)(const oak_playback_frame *frame, void *userdata),
	void *userdata)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	impl(self)->frame_cb = on_frame;
	impl(self)->frame_cb_data = userdata;
	return OAKENGINE_OK;
}

int oakengine_playback_set_audio_callback(
	OakEnginePlayback *self,
	void (*on_audio)(const oak_playback_audio *audio, void *userdata),
	void *userdata)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	impl(self)->audio_cb = on_audio;
	impl(self)->audio_cb_data = userdata;
	return OAKENGINE_OK;
}

int oakengine_playback_start(OakEnginePlayback *self, int64_t start_ts,
							 double speed)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	OakEnginePlaybackState *state = impl(self);
	set_error(state, QString());
	if (start_ts < 0 || !(speed > 0.0)) {
		set_error(state, QStringLiteral("invalid start time or speed"));
		return OAKENGINE_E_INVALID;
	}
	if (!olive::RenderManager::instance()) {
		set_error(state, QStringLiteral("engine not initialized with "
										"OAKENGINE_INIT_RENDER"));
		return OAKENGINE_E_STATE;
	}

	// A stopped thread is (re)spawned; a paused/playing one is re-anchored.
	if (state->state.load() == k_state_stopped) {
		if (state->pull_thread.joinable()) {
			state->pull_thread.join();
		}
		// A thread detached by a stop-from-callback may still be exiting;
		// wait it out so its final thread_done store cannot clobber the
		// new run. (Only in the stopped case: a paused thread is alive by
		// design and never sets thread_done.)
		while (!state->thread_done.load()) {
			QThread::msleep(1);
		}
	}

	state->speed.store(speed);
	state->anchor_ts.store(start_ts);
	state->last_start_ts.store(start_ts);
	state->next_frame_ts.store(start_ts);
	state->next_audio_time.store(start_ts * state->time_base.to_double());
	state->wall_timer.restart();
	if (olive::AudioManager::instance()) {
		// Restart the master clock at zero for this run (the viewer does
		// the same in finish_play_preprocess()).
		olive::AudioManager::instance()->reset_output_clock();
	}
	state->anchor_clock.store(master_clock_seconds(state));

	if (state->state.load() == k_state_stopped) {
		state->state.store(k_state_playing);
		state->thread_done.store(false);
		state->pull_thread = std::thread(pull_loop, state);
	} else {
		state->state.store(k_state_playing);
	}
	return OAKENGINE_OK;
}

int oakengine_playback_pause(OakEnginePlayback *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	OakEnginePlaybackState *state = impl(self);
	if (state->state.load() == k_state_playing) {
		state->paused_ts.store(position_ts_of(state,
											  master_clock_seconds(state)));
		state->state.store(k_state_paused);
		if (state->renderer) {
			oakengine_renderer_cancel(state->renderer);
		}
	}
	return OAKENGINE_OK;
}

int oakengine_playback_stop(OakEnginePlayback *self)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	stop_and_join(impl(self));
	return OAKENGINE_OK;
}

int oakengine_playback_get_position(const OakEnginePlayback *self, int64_t *ts)
{
	if (!self || !ts) {
		return OAKENGINE_E_INVALID;
	}
	const OakEnginePlaybackState *state = impl(self);
	switch (state->state.load()) {
	case k_state_playing:
		*ts = position_ts_of(state, master_clock_seconds(state));
		break;
	case k_state_paused:
		*ts = state->paused_ts.load();
		break;
	default:
		*ts = state->last_start_ts.load();
		break;
	}
	return OAKENGINE_OK;
}

int oakengine_playback_set_speed(OakEnginePlayback *self, double speed)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	OakEnginePlaybackState *state = impl(self);
	set_error(state, QString());
	if (!(speed > 0.0)) {
		set_error(state, QStringLiteral("invalid speed %1").arg(speed));
		return OAKENGINE_E_INVALID;
	}
	if (state->state.load() == k_state_playing) {
		// Re-anchor at the current position so delivery stays monotonic.
		const double clock_s = master_clock_seconds(state);
		state->anchor_ts.store(position_ts_of(state, clock_s));
		state->anchor_clock.store(clock_s);
	}
	state->speed.store(speed);
	return OAKENGINE_OK;
}

int oakengine_playback_is_playing(const OakEnginePlayback *self)
{
	if (!self) {
		return 0;
	}
	return impl(self)->state.load() == k_state_playing ? 1 : 0;
}

int oakengine_playback_last_error(const OakEnginePlayback *self, char *buf,
								  int buf_size)
{
	if (!self) {
		return OAKENGINE_E_INVALID;
	}
	return string_to_buf(impl(self)->last_error, buf, buf_size);
}

} // extern "C"
