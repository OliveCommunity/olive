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

// Pure C ABI test for the liboakengine renderer facade. The validation part
// (argument checking, not-initialized errors, NULL safety, cancel
// idempotency) requires no GL and must always pass. The render part needs a
// working render backend (the engine renders video in oak-render-worker
// child processes); when no backend is available it prints a SKIP notice
// and exits 0, mirroring the is_render_backend_available()/GTEST_SKIP logic
// of tests/gtest/render_worker_footage_test.cpp.
//
// Being an engine-internal test, the GL-gated part builds sequence content
// through the engine C++ API (the facade has no node-graph editing API yet):
// a solid red generator feeds the sequence's texture input, and the real
// footage tests/demo.mp4 feeds its samples input.

#include <assert.h>
#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <thread>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include "node/generator/solid/solid.h"
#include "node/node.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/footage/footage.h"
#include "node/project/sequence/sequence.h"

#include "config/config.h"

#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#include "render/backend/renderbackend_c.h"
#endif

#include "oakengine/init.h"
#include "oakengine/project.h"
#include "oakengine/renderer.h"
#include "oakengine/timeline.h"

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
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_renderer_test_%lu",
			 base, (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_renderer_test_XXXXXX");
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

	if (backend == QStringLiteral("vulkan") &&
		info.kind != oak_render_backend_vulkan) {
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

// Samples up to 4096 positions of the frame and counts non-zero bytes.
static int frame_nonzero_bytes(const OakEngineFrame *frame)
{
	const unsigned char *data =
		(const unsigned char *)oakengine_frame_data(frame);
	if (!data) {
		return -1;
	}
	const int size = oakengine_frame_linesize_bytes(frame) *
					 oakengine_frame_height(frame);
	const int step = size / 4096 > 1 ? size / 4096 : 1;
	int nonzero = 0;
	for (int i = 0; i < size; i += step) {
		if (data[i] != 0) {
			nonzero++;
		}
	}
	return nonzero;
}

// Argument validation and behavior without OAKENGINE_INIT_RENDER. Requires
// no GL at all.
static void test_validation(OakEngineSequence *seq)
{
	// create() argument validation.
	EXPECT_TRUE(oakengine_renderer_create(NULL, 320, 180, 4, 30000, 1001, NULL) ==
		   NULL);
	EXPECT_TRUE(oakengine_renderer_create(seq, 0, 180, 4, 30000, 1001, NULL) ==
		   NULL);
	EXPECT_TRUE(oakengine_renderer_create(seq, 320, -1, 4, 30000, 1001, NULL) ==
		   NULL);
	EXPECT_TRUE(oakengine_renderer_create(seq, 320, 180, 4, 0, 1001, NULL) ==
		   NULL);
	EXPECT_TRUE(oakengine_renderer_create(seq, 320, 180, 4, 30000, -1001, NULL) ==
		   NULL);
	EXPECT_TRUE(oakengine_renderer_create(seq, 320, 180, -1, 30000, 1001, NULL) ==
		   NULL);
	EXPECT_TRUE(oakengine_renderer_create(seq, 320, 180, 5, 30000, 1001, NULL) ==
		   NULL);

	OakEngineRenderer *r =
		oakengine_renderer_create(seq, 320, 180, 4, 30000, 1001, NULL);
	EXPECT_TRUE(r != NULL);

	// Mode follows olive::RenderMode: 0/1 accepted, everything else rejected.
	EXPECT_TRUE(oakengine_renderer_set_mode(r, 0) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_renderer_set_mode(r, 1) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_renderer_set_mode(r, -1) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_renderer_set_mode(r, 2) == OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_renderer_set_mode(NULL, 0) == OAKENGINE_E_INVALID);

	// Renders fail while the engine lacks the RENDER bit, with a
	// human-readable reason.
	EXPECT_TRUE(oakengine_renderer_render_frame(r, 0) == NULL);
	char err[256];
	EXPECT_TRUE(oakengine_renderer_last_error(r, err, sizeof(err)) > 0);
	EXPECT_TRUE(strstr(err, "OAKENGINE_INIT_RENDER") != NULL);
	EXPECT_TRUE(oakengine_renderer_render_audio(r, 0, 30) == NULL);
	EXPECT_TRUE(oakengine_renderer_last_error(r, err, sizeof(err)) > 0);

	// cancel is a no-op when nothing is in flight and is idempotent.
	oakengine_renderer_cancel(r);
	oakengine_renderer_cancel(r);

	oakengine_renderer_free(r);

	// NULL safety across all three handle families.
	EXPECT_TRUE(oakengine_renderer_last_error(NULL, err, sizeof(err)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_renderer_render_frame(NULL, 0) == NULL);
	EXPECT_TRUE(oakengine_renderer_render_audio(NULL, 0, 30) == NULL);
	oakengine_renderer_cancel(NULL);
	oakengine_renderer_free(NULL);
	EXPECT_TRUE(oakengine_frame_width(NULL) == 0);
	EXPECT_TRUE(oakengine_frame_height(NULL) == 0);
	EXPECT_TRUE(oakengine_frame_format(NULL) == -1);
	EXPECT_TRUE(oakengine_frame_channel_count(NULL) == 0);
	EXPECT_TRUE(oakengine_frame_linesize_bytes(NULL) == 0);
	EXPECT_TRUE(oakengine_frame_data(NULL) == NULL);
	oakengine_frame_free(NULL);
	EXPECT_TRUE(oakengine_audio_sample_rate(NULL) == 0);
	EXPECT_TRUE(oakengine_audio_channel_count(NULL) == 0);
	EXPECT_TRUE(oakengine_audio_sample_count(NULL) == 0);
	EXPECT_TRUE(oakengine_audio_data(NULL, 0) == NULL);
	oakengine_audio_free(NULL);
}

static void test_render_cache_helpers(void)
{
	// Without an active RenderManager, these return OAKENGINE_E_STATE rather
	// than crashing.
	EXPECT_TRUE(oakengine_render_cache_set_display_color_processor(NULL) ==
		   OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_render_cache_set_multicam_node(NULL) ==
		   OAKENGINE_E_STATE);
}

TEST(OakEngineRenderer, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	EXPECT_TRUE(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
#endif

	// HEADLESS is enough for the validation part and creates the offscreen
	// application object the backend probe below depends on.
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Render");
	EXPECT_TRUE(seq != NULL);

	test_render_cache_helpers();
	test_validation(seq);

	// ---- GL-gated part ---------------------------------------------------
	if (!is_render_backend_available(QStringLiteral("opengl"))) {
		printf("oakengine_renderer_test: SKIP: OpenGL render backend not "
			   "available, render assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return;
	}
	if (!worker_binary_exists()) {
		printf("oakengine_renderer_test: SKIP: oak-render-worker binary not "
			   "found, render assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return;
	}

	// The engine renders through the backend requested in the config.
	olive::Config::current()[QStringLiteral("GraphicsBackend")] =
		QStringLiteral("opengl");

	// Upgrade HEADLESS -> HEADLESS|RENDER (idempotent flag add).
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_init_flags() ==
		   (OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER));

	// Build content through the engine C++ API (engine-internal test; the
	// facade has no node editing API yet). The handles are the engine
	// pointers by design.
	auto *proj = reinterpret_cast<olive::Project *>(project);
	auto *sequence = reinterpret_cast<olive::Sequence *>(seq);

	// Solid red generator -> texture input: every frame is solid red.
	auto *solid = new olive::SolidGenerator();
	solid->setParent(proj);
	olive::Node::connect_edge(
		solid, olive::NodeInput(sequence, olive::ViewerOutput::k_texture_input));

	// Real footage -> samples input: real audio for render_audio.
	const QString demo_path =
		QDir(QStringLiteral(OAK_TEST_SOURCE_DIR))
			.filePath(QStringLiteral("tests/demo.mp4"));
	EXPECT_TRUE(QFileInfo::exists(demo_path));
	auto *footage = new olive::Footage(demo_path);
	footage->setParent(proj);
	EXPECT_TRUE(footage->is_valid());
	olive::Node::connect_edge(
		footage,
		olive::NodeInput(sequence, olive::ViewerOutput::k_samples_input));

	// render_frame: geometry/format and non-black pixels.
	OakEngineRenderer *renderer =
		oakengine_renderer_create(seq, 320, 180, 4, 30000, 1001, NULL);
	EXPECT_TRUE(renderer != NULL);
	OakEngineFrame *frame = oakengine_renderer_render_frame(renderer, 0);
	char err[256];
	if (!frame) {
		fprintf(stderr, "render_frame failed: %s\n",
				oakengine_renderer_last_error(renderer, err, sizeof(err)) > 0 ?
					err :
					"(no error)");
	}
	EXPECT_TRUE(frame != NULL);
	EXPECT_TRUE(oakengine_frame_width(frame) == 320);
	EXPECT_TRUE(oakengine_frame_height(frame) == 180);
	EXPECT_TRUE(oakengine_frame_format(frame) == 4); // f32
	EXPECT_TRUE(oakengine_frame_channel_count(frame) == 4);
	EXPECT_TRUE(oakengine_frame_linesize_bytes(frame) >= 320 * 4 * 4);
	EXPECT_TRUE(oakengine_frame_data(frame) != NULL);
	EXPECT_TRUE(frame_nonzero_bytes(frame) > 0); // solid red, not black
	oakengine_frame_free(frame);

	// render_audio: 30 frames at 1001/30000 = 1.001s at 48 kHz stereo.
	OakEngineAudioBuffer *audio =
		oakengine_renderer_render_audio(renderer, 0, 30);
	if (!audio) {
		fprintf(stderr, "render_audio failed: %s\n",
				oakengine_renderer_last_error(renderer, err, sizeof(err)) > 0 ?
					err :
					"(no error)");
	}
	EXPECT_TRUE(audio != NULL);
	EXPECT_TRUE(oakengine_audio_sample_rate(audio) == 48000);
	EXPECT_TRUE(oakengine_audio_channel_count(audio) == 2);
	const int64_t samples = oakengine_audio_sample_count(audio);
	EXPECT_TRUE(samples > 48048 - 4800 && samples < 48048 + 4800);
	EXPECT_TRUE(oakengine_audio_data(audio, 0) != NULL);
	EXPECT_TRUE(oakengine_audio_data(audio, 1) != NULL);
	EXPECT_TRUE(oakengine_audio_data(audio, 2) == NULL); // out of range
	oakengine_audio_free(audio);

	// Cancelling mid-render from another thread must not crash, and the
	// renderer must stay usable afterwards.
	std::thread canceller([renderer]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		oakengine_renderer_cancel(renderer);
	});
	OakEngineFrame *maybe = oakengine_renderer_render_frame(renderer, 60);
	canceller.join();
	oakengine_frame_free(maybe); // may be NULL (cancelled) or a frame
	OakEngineFrame *after = oakengine_renderer_render_frame(renderer, 0);
	EXPECT_TRUE(after != NULL);
	oakengine_frame_free(after);
	oakengine_renderer_cancel(renderer); // idempotent no-op

	oakengine_renderer_free(renderer);
	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
