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

#ifndef OAKENGINE_PLAYBACK_H
#define OAKENGINE_PLAYBACK_H

#include <stdint.h>

#include "export.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file playback.h
 * @brief C ABI for asynchronous headless playback of a sequence
 *
 * An OakEnginePlayback drives a pull thread that renders upcoming frames
 * and 1/4-second audio blocks through the renderer family and delivers
 * them through user callbacks -- the execution engine a viewer can be
 * built on. The MVP deliberately covers forward playback at constant or
 * sped-up rates only: no negative speed, no variable shuttle, no
 * tape-style scrub audio.
 *
 * Threading: every callback fires ON THE PULL THREAD (one background
 * thread per instance). Consumers that need the main thread (widgets,
 * most GUI work) must marshal the data themselves. The payload pointers
 * inside oak_playback_frame/oak_playback_audio are valid only until the
 * callback returns (the engine owns and recycles the buffers); copy what
 * you keep.
 *
 * Audio output: when an olive::AudioManager instance exists (the
 * application creates it; bare facade processes usually have none), each
 * audio block is also pushed to it (packed float32) and the playback
 * position is read from its output clock (AudioManager::seconds(),
 * latency-compensated). Without an instance, only the callbacks fire and
 * the position falls back to the wall clock. Sequence position advances
 * by consumed-output seconds * speed from the start timestamp either
 * way.
 *
 * Event loop requirement: the process must pump a Qt event loop on its
 * main thread while playing (the engine's conform/decode completions
 * are posted there); GUI consumers get this for free, console consumers
 * should drive QCoreApplication::processEvents() periodically (the same
 * rule the synchronous facade waits follow).
 *
 * Frame pacing at speed != 1 mirrors the application's viewer: the
 * timestamp step between delivered frames is llround(speed) (minimum
 * 1), so > 1x skips frames while pacing stays clock-driven; audio is
 * rendered per wall 1/4 second covering interval*speed of sequence time
 * (no tempo/pitch correction at speed, an MVP limitation).
 *
 * Conventions match the other families: 0 (OAKENGINE_OK) / negative
 * OAKENGINE_E_* codes, NULL handles as no-ops or OAKENGINE_E_INVALID,
 * per-handle human-readable reason via oakengine_playback_last_error().
 * All timestamps are frame numbers in the sequence's frame-rate
 * timebase, like the rest of the timeline family.
 */

/**
 * @brief Opaque playback engine handle. Free with
 * oakengine_playback_free(); the sequence is borrowed (owned by its
 * project).
 */
typedef struct OakEnginePlayback OakEnginePlayback;

/**
 * @brief POD video frame delivered to the frame callback.
 *
 * `timestamp` is the frame number in the sequence's timebase; `format`
 * is an olive::core::PixelFormat::Format value; `linesize` is the
 * stride in bytes. `data` is owned by the engine and valid only until
 * the callback returns.
 */
typedef struct oak_playback_frame {
	int64_t timestamp;
	int width;
	int height;
	int format;
	int linesize;
	const void *data;
} oak_playback_frame;

/**
 * @brief POD audio block delivered to the audio callback.
 *
 * `start_ts` is the block's start in sequence timebase units,
 * `sample_count` the frames per channel, `channel_data` planar float
 * pointers (engine-owned, valid only until the callback returns).
 */
typedef struct oak_playback_audio {
	int64_t start_ts;
	int channels;
	int sample_rate;
	int64_t sample_count;
	const float *const *channel_data;
} oak_playback_audio;

/**
 * @brief Create a playback engine for `seq` producing `width`x`height`
 * frames at `fps_num`/`fps_den`.
 *
 * Rendering goes through the renderer family, so actual playback
 * requires the engine initialized with OAKENGINE_INIT_RENDER (starting
 * without it fails with OAKENGINE_E_STATE). Returns NULL on invalid
 * arguments (NULL sequence, non-positive size or frame rate).
 */
OAKENGINE_API OakEnginePlayback *oakengine_playback_create(
	OakEngineSequence *seq, int width, int height, int fps_num,
	int fps_den);

/**
 * @brief Stop playback, join the pull thread and free the instance.
 * NULL-safe. Must NOT be called from inside a frame/audio callback (the
 * pull thread cannot join itself; use oakengine_playback_stop() there
 * and free from another thread afterwards).
 */
OAKENGINE_API void oakengine_playback_free(OakEnginePlayback *self);

/**
 * @brief Install the frame callback (NULL to clear). Fires on the pull
 * thread; the payload is valid only during the call.
 */
OAKENGINE_API int oakengine_playback_set_frame_callback(
	OakEnginePlayback *self,
	void (*on_frame)(const oak_playback_frame *frame, void *userdata),
	void *userdata);

/**
 * @brief Install the audio callback (NULL to clear). Fires on the pull
 * thread; the payload is valid only during the call.
 */
OAKENGINE_API int oakengine_playback_set_audio_callback(
	OakEnginePlayback *self,
	void (*on_audio)(const oak_playback_audio *audio, void *userdata),
	void *userdata);

/**
 * @brief Start (or re-base) playback at `start_ts` with `speed`.
 *
 * `speed` must be > 0 (OAKENGINE_E_INVALID otherwise; negative speed is
 * outside the MVP). Starting while already playing re-anchors at
 * `start_ts`. Requires OAKENGINE_INIT_RENDER (OAKENGINE_E_STATE).
 */
OAKENGINE_API int oakengine_playback_start(OakEnginePlayback *self,
										   int64_t start_ts, double speed);

/**
 * @brief Pause playback (idempotent). The position freezes; resume by
 * calling oakengine_playback_start() at the frozen (or any) timestamp.
 */
OAKENGINE_API int oakengine_playback_pause(OakEnginePlayback *self);

/**
 * @brief Stop playback (idempotent): the pull thread exits and the
 * position resets to the last start timestamp (0 before the first
 * start). May be called from inside a callback (the pull thread then
 * detaches and exits on its own; oakengine_playback_free() waits it
 * out).
 */
OAKENGINE_API int oakengine_playback_stop(OakEnginePlayback *self);

/**
 * @brief Current playback position as a frame timestamp.
 *
 * Read from the audio output clock when an AudioManager instance is
 * pushing audio (the master clock), otherwise from the wall clock; a
 * frozen value while paused, the last start timestamp while stopped.
 */
OAKENGINE_API int oakengine_playback_get_position(
	const OakEnginePlayback *self, int64_t *ts);

/**
 * @brief Change the speed mid-playback (`speed` > 0). Re-anchors at the
 * current position so delivery stays monotonic. OAKENGINE_E_INVALID
 * for speed <= 0.
 */
OAKENGINE_API int oakengine_playback_set_speed(OakEnginePlayback *self,
											   double speed);

/**
 * @brief 1 while playing (0 when paused, stopped, or after the
 * end-of-stream auto-stop). 0 on a NULL handle.
 */
OAKENGINE_API int oakengine_playback_is_playing(
	const OakEnginePlayback *self);

/**
 * @brief Human-readable reason of the last failed call on this handle
 * (buf/size convention). Empty when the last call succeeded.
 */
OAKENGINE_API int oakengine_playback_last_error(
	const OakEnginePlayback *self, char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_PLAYBACK_H */
