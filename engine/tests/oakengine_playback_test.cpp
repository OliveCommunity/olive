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

// Pure C ABI test for the liboakengine playback facade. The validation
// part (argument checking, not-initialized errors, idempotent
// pause/stop, NULL safety) requires no GL and must always pass. The
// playback part needs a working render backend (frames are pulled
// through the renderer family in oak-render-worker child processes);
// when no backend is available it prints a SKIP notice and exits 0,
// mirroring oakengine_renderer_test.
//
// Being an engine-internal test, the GL-gated part builds sequence
// content through the engine C++ API: a solid red generator feeds the
// sequence's texture input and tests/demo.mp4 feeds its samples input.
// Callbacks fire on the engine's pull thread, so the test records them
// under a mutex (the documented marshalling duty of the consumer).

#include <assert.h>
#include <gtest/gtest.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QThread>

#include "config/config.h"
#include "node/generator/solid/solid.h"
#include "node/node.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"
#include "oakengine/init.h"
#include "oakengine/playback.h"
#include "oakengine/project.h"
#include "oakengine/renderer.h"
#include "oakengine/timeline.h"
#include "olive/core/util/timecodefunctions.h"
#include "render/backend/dynamicrenderer.h"

#ifndef OAK_TEST_SOURCE_DIR
#define OAK_TEST_SOURCE_DIR "."
#endif

static char g_tmpdir[4096];

static void make_tmpdir(void)
{
#if defined(_WIN32)
	char base[MAX_PATH];
	const DWORD len = GetTempPathA(MAX_PATH, base);
	EXPECT_TRUE(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_playback_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_playback_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

// Same probe as tests/gtest/render_worker_footage_test.cpp.
static bool is_render_backend_available(const QString &backend)
{
#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
	olive::DynamicRenderer renderer(backend);
	if (!renderer.load()) {
		return false;
	}

	OakRenderBackendInfo info = {};
	if (!renderer.get_backend_info(&info)) {
		return false;
	}

	if (backend == QStringLiteral("opengl") &&
		info.kind != oak_render_backend_opengl) {
		return false;
	}

	return renderer.init();
#else
	Q_UNUSED(backend)
	return false;
#endif
}

static bool worker_binary_exists()
{
	QDir dir(QCoreApplication::applicationDirPath());
	dir.cd(QStringLiteral("../worker"));
#if defined(_WIN32)
	return QFileInfo::exists(dir.filePath(QStringLiteral("oak-render-worker.exe")));
#else
	return QFileInfo::exists(dir.filePath(QStringLiteral("oak-render-worker")));
#endif
}

// Callback recorders (fired on the engine pull thread).
struct FrameLog {
	std::mutex mutex;
	std::vector<oak_playback_frame> frames;

	void add(const oak_playback_frame &f)
	{
		std::lock_guard<std::mutex> lock(mutex);
		frames.push_back(f);
	}

	size_t size()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return frames.size();
	}
};

struct AudioLog {
	std::mutex mutex;
	std::vector<oak_playback_audio> blocks;

	void add(const oak_playback_audio &a)
	{
		std::lock_guard<std::mutex> lock(mutex);
		blocks.push_back(a);
	}

	size_t size()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return blocks.size();
	}
};

static void on_frame(const oak_playback_frame *frame, void *userdata)
{
	static_cast<FrameLog *>(userdata)->add(*frame);
}

static void on_audio(const oak_playback_audio *audio, void *userdata)
{
	static_cast<AudioLog *>(userdata)->add(*audio);
}

// Stops playback from inside the callback (i.e. on the pull thread); the
// playback.h contract allows this for stop (but not for free).
static void on_frame_stop(const oak_playback_frame *frame, void *userdata)
{
	Q_UNUSED(frame)
	oakengine_playback_stop(static_cast<OakEnginePlayback *>(userdata));
}

// Poll `pred` until it holds or the timeout elapses, PUMPING THE MAIN
// THREAD's event queue: conform/decode completions are posted here, and
// starving it stalls audio rendering (the playback facade requires a
// pumped Qt event loop on the consumer's main thread).
static bool wait_until(bool (*pred)(void *), void *userdata, int timeout_ms)
{
	const int64_t deadline =
		QDateTime::currentMSecsSinceEpoch() + timeout_ms;
	while (QDateTime::currentMSecsSinceEpoch() < deadline) {
		if (pred(userdata)) {
			return true;
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(5);
	}
	return pred(userdata);
}

// Sleep `ms` while pumping the main thread (see wait_until).
static void pump_ms(int ms)
{
	const int64_t deadline = QDateTime::currentMSecsSinceEpoch() + ms;
	while (QDateTime::currentMSecsSinceEpoch() < deadline) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(5);
	}
}

static bool six_frames(void *userdata)
{
	return static_cast<FrameLog *>(userdata)->size() >= 6;
}

static bool good_audio_block(void *userdata)
{
	AudioLog *log = static_cast<AudioLog *>(userdata);
	std::lock_guard<std::mutex> lock(log->mutex);
	for (const oak_playback_audio &a : log->blocks) {
		if (a.channel_data && a.channel_data[0] && a.channel_data[1]) {
			return true;
		}
	}
	return false;
}

static bool not_playing(void *userdata)
{
	return oakengine_playback_is_playing(
			   static_cast<OakEnginePlayback *>(userdata)) == 0;
}

// Argument validation, idempotent pause/stop, initial position. No GL.
static void test_validation(OakEngineSequence *seq)
{
	EXPECT_TRUE(oakengine_playback_create(NULL, 320, 180, 24, 1) == NULL);
	EXPECT_TRUE(oakengine_playback_create(seq, 0, 180, 24, 1) == NULL);
	EXPECT_TRUE(oakengine_playback_create(seq, 320, -1, 24, 1) == NULL);
	EXPECT_TRUE(oakengine_playback_create(seq, 320, 180, 0, 1) == NULL);
	EXPECT_TRUE(oakengine_playback_create(seq, 320, 180, 24, 0) == NULL);

	OakEnginePlayback *p = oakengine_playback_create(seq, 320, 180, 24, 1);
	EXPECT_TRUE(p != NULL);

	// Callback setters: NULL-safe, idempotent.
	EXPECT_TRUE(oakengine_playback_set_frame_callback(NULL, on_frame, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_set_frame_callback(p, NULL, NULL) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_set_audio_callback(NULL, on_audio, NULL) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_set_audio_callback(p, NULL, NULL) ==
		   OAKENGINE_OK);

	// Position is 0 before the first start; pause/stop are idempotent.
	int64_t pos = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &pos) == OAKENGINE_OK);
	EXPECT_TRUE(pos == 0);
	EXPECT_TRUE(oakengine_playback_get_position(NULL, &pos) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_get_position(p, NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_is_playing(p) == 0);
	EXPECT_TRUE(oakengine_playback_is_playing(NULL) == 0);
	EXPECT_TRUE(oakengine_playback_pause(p) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_stop(p) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_is_playing(p) == 0);

	// Bad speeds are rejected with a readable reason; start without the
	// RENDER bit reports OAKENGINE_E_STATE.
	EXPECT_TRUE(oakengine_playback_set_speed(p, 0.0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_set_speed(p, -1.0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_set_speed(NULL, 1.0) == OAKENGINE_E_INVALID);
	char err[256];
	EXPECT_TRUE(oakengine_playback_last_error(p, err, sizeof(err)) > 0);
	EXPECT_TRUE(oakengine_playback_start(p, 0, 0.0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_start(p, 0, -2.0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_start(p, -1, 1.0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_start(p, 0, 1.0) == OAKENGINE_E_STATE);
	EXPECT_TRUE(strstr(err, "OAKENGINE_INIT_RENDER") != NULL ||
		   oakengine_playback_last_error(p, err, sizeof(err)) > 0);
	EXPECT_TRUE(oakengine_playback_start(NULL, 0, 1.0) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_pause(NULL) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_playback_stop(NULL) == OAKENGINE_E_INVALID);

	oakengine_playback_free(p);
	oakengine_playback_free(NULL);
}

TEST(OakEnginePlayback, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	EXPECT_TRUE(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("OAK_CONFIG_DIR", g_tmpdir, 1) == 0);
#endif

	// HEADLESS is enough for the validation part and creates the
	// application object the backend probe below depends on.
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Playback");
	EXPECT_TRUE(seq != NULL);

	test_validation(seq);

	// ---- GL-gated part ---------------------------------------------------
	if (!is_render_backend_available(QStringLiteral("opengl"))) {
		printf("oakengine_playback_test: SKIP: OpenGL render backend not "
			   "available, playback assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return;
	}
	if (!worker_binary_exists()) {
		printf("oakengine_playback_test: SKIP: oak-render-worker binary not "
			   "found, playback assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return;
	}

	// The engine renders through the backend requested in the config.
	olive::Config::current()[QStringLiteral("GraphicsBackend")] =
		QStringLiteral("opengl");
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) ==
		   OAKENGINE_OK);

	// Build content through the facade (a real clip gives the sequence a
	// bounded length for the end-of-stream test): one video track with
	// tests/demo.mp4 placed from 0.
	const QString demo_path = QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
								  .filePath(QStringLiteral("tests/demo.mp4"));
	EXPECT_TRUE(QFileInfo::exists(demo_path));
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) ==
		   0);
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, demo_path.toUtf8().constData());
	EXPECT_TRUE(footage != NULL);
	OakEngineClip *clip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, 480, 0);
	EXPECT_TRUE(clip != NULL);
	// Audio comes from an audio track: without one the sequence's samples
	// output is empty (the video track alone carries no samples).
	EXPECT_TRUE(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) ==
		   0);
	OakEngineClip *aclip = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_AUDIO, 0, 0, 480, 0);
	EXPECT_TRUE(aclip != NULL);
	oakengine_footage_free(footage);

	FrameLog frames;
	AudioLog audio;
	OakEnginePlayback *p =
		oakengine_playback_create(seq, 320, 180, 30000, 1001);
	EXPECT_TRUE(p != NULL);
	EXPECT_TRUE(oakengine_playback_set_frame_callback(p, on_frame, &frames) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_set_audio_callback(p, on_audio, &audio) ==
		   OAKENGINE_OK);

	// Start at 1x: frames arrive monotonically with the right geometry,
	// audio blocks with the right layout.
	EXPECT_TRUE(oakengine_playback_start(p, 0, 1.0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_is_playing(p) == 1);
	EXPECT_TRUE(wait_until(six_frames, &frames, 10000));
	// The first block(s) can land while the decoder is still warming up
	// (empty channels); require at least one well-formed block and a
	// consistent layout across all of them (generous for CI stability).
	EXPECT_TRUE(wait_until(good_audio_block, &audio, 10000));
	{
		frames.mutex.lock();
		const size_t n = frames.frames.size();
		for (size_t i = 0; i < n; i++) {
			const oak_playback_frame &f = frames.frames.at(i);
			EXPECT_TRUE(f.width == 320 && f.height == 180);
			EXPECT_TRUE(f.format == 3); // f16
			EXPECT_TRUE(f.linesize >= 320 * 4 * 2);
			EXPECT_TRUE(f.data != NULL);
			if (i > 0) {
				EXPECT_TRUE(f.timestamp > frames.frames.at(i - 1).timestamp);
			}
		}
		frames.mutex.unlock();
	}
	{
		audio.mutex.lock();
		bool found_good = false;
		for (const oak_playback_audio &a : audio.blocks) {
			EXPECT_TRUE(a.channels == 2);
			EXPECT_TRUE(a.sample_rate == 48000);
			EXPECT_TRUE(a.sample_count >= 0);
			if (a.channel_data && a.channel_data[0] && a.channel_data[1] &&
				a.sample_count > 0) {
				found_good = true;
			}
		}
		EXPECT_TRUE(found_good);
		audio.mutex.unlock();
	}

	// Position advances from 0 (wall clock master here: no AudioManager
	// instance exists in this process).
	pump_ms(400);
	int64_t pos_a = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &pos_a) == OAKENGINE_OK);
	EXPECT_TRUE(pos_a > 0);

	// Pause freezes delivery and the position.
	EXPECT_TRUE(oakengine_playback_pause(p) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_is_playing(p) == 0);
	const size_t frozen_frames = frames.size();
	int64_t frozen_pos = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &frozen_pos) == OAKENGINE_OK);
	pump_ms(250);
	EXPECT_TRUE(frames.size() <= frozen_frames + 1); // one in-flight may land
	int64_t still_frozen = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &still_frozen) == OAKENGINE_OK);
	EXPECT_TRUE(still_frozen == frozen_pos);

	// Resume at the frozen position: delivery continues.
	EXPECT_TRUE(oakengine_playback_start(p, frozen_pos, 1.0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_is_playing(p) == 1);
	pump_ms(300);
	EXPECT_TRUE(frames.size() > frozen_frames);

	// 2x advances the position (much) faster than 1x over the same wall
	// window (generous margins to stay CI-stable).
	EXPECT_TRUE(oakengine_playback_get_position(p, &pos_a) == OAKENGINE_OK);
	pump_ms(600);
	int64_t pos_b = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &pos_b) == OAKENGINE_OK);
	const int64_t advance_1x = pos_b - pos_a;
	EXPECT_TRUE(oakengine_playback_set_speed(p, 2.0) == OAKENGINE_OK);
	pump_ms(600);
	int64_t pos_c = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &pos_c) == OAKENGINE_OK);
	const int64_t advance_2x = pos_c - pos_b;
	EXPECT_TRUE(advance_1x > 0);
	EXPECT_TRUE(advance_2x > advance_1x + advance_1x / 2);

	// Stop resets to the last start timestamp; restarting from 0 replays.
	EXPECT_TRUE(oakengine_playback_stop(p) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_is_playing(p) == 0);
	int64_t stopped_pos = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &stopped_pos) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(stopped_pos == frozen_pos);
	{
		frames.mutex.lock();
		frames.frames.clear();
		frames.mutex.unlock();
	}
	EXPECT_TRUE(oakengine_playback_start(p, 0, 1.0) == OAKENGINE_OK);
	EXPECT_TRUE(wait_until(six_frames, &frames, 10000));
	{
		frames.mutex.lock();
		EXPECT_TRUE(frames.frames.front().timestamp < 30);
		frames.mutex.unlock();
	}
	EXPECT_TRUE(oakengine_playback_stop(p) == OAKENGINE_OK);

	// End of stream: start near the end at 4x, the engine auto-stops at
	// the sequence length and reports that as the position.
	int len_num = 0, len_den = 1;
	EXPECT_TRUE(oakengine_sequence_get_length_rational(seq, &len_num, &len_den) ==
		   OAKENGINE_OK);
	const int64_t end_ts = int64_t(
		olive::core::Timecode::time_to_timestamp(
			olive::Rational(len_num, len_den),
			olive::Rational(1001, 30000), olive::core::Timecode::k_round));
	const int64_t near_end = end_ts > 48 ? end_ts - 48 : 0;
	EXPECT_TRUE(oakengine_playback_start(p, near_end, 4.0) == OAKENGINE_OK);
	EXPECT_TRUE(wait_until(not_playing, p, 10000));
	int64_t end_pos = -1;
	EXPECT_TRUE(oakengine_playback_get_position(p, &end_pos) == OAKENGINE_OK);
	EXPECT_TRUE(end_pos == end_ts);

	// Freeing during playback must not crash.
	OakEnginePlayback *p2 =
		oakengine_playback_create(seq, 320, 180, 30000, 1001);
	EXPECT_TRUE(p2 != NULL);
	EXPECT_TRUE(oakengine_playback_set_frame_callback(p2, on_frame,
												 &frames) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_start(p2, 0, 1.0) == OAKENGINE_OK);
	pump_ms(50);
	oakengine_playback_free(p2);

	// Stopping from inside a callback must not deadlock: the pull thread
	// detaches, and free() from this thread waits its exit out.
	OakEnginePlayback *p3 =
		oakengine_playback_create(seq, 320, 180, 30000, 1001);
	EXPECT_TRUE(p3 != NULL);
	EXPECT_TRUE(oakengine_playback_set_frame_callback(p3, on_frame_stop, p3) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_playback_start(p3, 0, 1.0) == OAKENGINE_OK);
	EXPECT_TRUE(wait_until(not_playing, p3, 10000));
	oakengine_playback_free(p3);

	oakengine_playback_free(p);
	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
