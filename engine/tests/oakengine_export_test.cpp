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

// Pure C ABI test for the liboakengine export facade. Codec probing and the
// argument/error paths require no GL; the actual export is GL-gated the same
// way as oakengine_renderer_test (dynamic backend probe + worker binary,
// SKIP with exit 0 when unavailable). The GL part builds a solid-color
// sequence through the engine C++ API (engine-internal test), exports one
// second of H.264 MP4 and validates it with ffprobe, and checks the
// progress callback.

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include "config/config.h"
#include "node/generator/solid/solid.h"
#include "node/node.h"
#include "node/output/viewer/viewer.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"

#ifdef OAK_ENABLE_DYNAMIC_RENDER_BACKEND
#include "render/backend/dynamicrenderer.h"
#include "render/backend/renderbackend_c.h"
#endif

#include "oakengine/exporter.h"
#include "oakengine/init.h"
#include "oakengine/project.h"
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
	assert(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_export_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	assert(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_export_test_XXXXXX");
	assert(mkdtemp(g_tmpdir) != NULL);
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

// ---- Progress callback state ----------------------------------------------
static int g_progress_calls = 0;
static double g_progress_last = -1.0;
static int g_progress_monotonic = 1;

static void progress_cb(double fraction, void *userdata)
{
	(void)userdata;
	g_progress_calls++;
	if (fraction < g_progress_last - 1e-9) {
		g_progress_monotonic = 0;
	}
	g_progress_last = fraction;
}

// ---- No-GL part -------------------------------------------------------------
static void test_codecs_and_validation(OakEngineSequence *seq)
{
	// Codec probing needs no RENDER bit and no GL.
	assert(oakengine_export_has_video_codec(OAKENGINE_EXPORT_VIDEO_H264) ==
		   1);
	assert(oakengine_export_has_video_codec(OAKENGINE_EXPORT_VIDEO_H265) ==
		   1);
	assert(oakengine_export_has_video_codec(
			   OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE) == 1);
	assert(oakengine_export_has_video_codec(-2) == 0);
	assert(oakengine_export_has_video_codec(99) == 0);
	assert(oakengine_export_has_audio_codec(OAKENGINE_EXPORT_AUDIO_AAC) == 1);
	assert(oakengine_export_has_audio_codec(OAKENGINE_EXPORT_AUDIO_PCM) == 1);
	assert(oakengine_export_has_audio_codec(
			   OAKENGINE_EXPORT_AUDIO_NONE) == 0);
	assert(oakengine_export_has_audio_codec(99) == 0);

	// Argument validation (engine has no RENDER bit yet either).
	char path[4096];
	snprintf(path, sizeof(path), "%s/out.mp4", g_tmpdir);
	assert(oakengine_export_render(NULL, path, 0, 30, 320, 180, NULL) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_export_render(seq, NULL, 0, 30, 320, 180, NULL) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_export_render(seq, path, -1, 30, 320, 180, NULL) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_export_render(seq, path, 30, 30, 320, 180, NULL) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_export_render(seq, path, 40, 30, 320, 180, NULL) ==
		   OAKENGINE_E_INVALID);

	// Without OAKENGINE_INIT_RENDER the export is refused with E_STATE.
	assert(oakengine_export_render(seq, path, 0, 30, 320, 180, NULL) ==
		   OAKENGINE_E_STATE);
	char err[256];
	assert(oakengine_export_last_error(err, sizeof(err)) > 0);
}

int main(void)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (see oakengine_init_test).
#if !defined(_WIN32)
	assert(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	assert(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
#endif

	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Export");
	assert(seq != NULL);

	test_codecs_and_validation(seq);

	// ---- GL-gated part ------------------------------------------------------
	if (!is_render_backend_available(QStringLiteral("opengl"))) {
		printf("oakengine_export_test: SKIP: OpenGL render backend not "
			   "available, export assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return 0;
	}
	if (!worker_binary_exists()) {
		printf("oakengine_export_test: SKIP: oak-render-worker binary not "
			   "found, export assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return 0;
	}

	olive::Config::current()[QStringLiteral("GraphicsBackend")] =
		QStringLiteral("opengl");
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) ==
		   OAKENGINE_OK);

	// Solid red generator -> texture input (engine C++ API, internal test).
	auto *proj = reinterpret_cast<olive::Project *>(project);
	auto *sequence = reinterpret_cast<olive::Sequence *>(seq);
	auto *solid = new olive::SolidGenerator();
	solid->setParent(proj);
	olive::Node::connect_edge(
		solid, olive::NodeInput(sequence, olive::ViewerOutput::k_texture_input));

	// Export one second (30 frames at the default 30000/1001) of H.264.
	char out[4096];
	snprintf(out, sizeof(out), "%s/export.mp4", g_tmpdir);
	oak_export_options opts;
	memset(&opts, 0, sizeof(opts));
	opts.video_codec = OAKENGINE_EXPORT_VIDEO_H264;
	opts.audio_codec = OAKENGINE_EXPORT_AUDIO_NONE; // no audio content here

	oakengine_export_set_progress_callback(progress_cb, NULL);
	char err[512];
	int rc = oakengine_export_render(seq, out, 0, 30, 320, 180, &opts);
	if (rc != OAKENGINE_OK) {
		fprintf(stderr, "export failed (%d): %s\n", rc,
				oakengine_export_last_error(err, sizeof(err)) > 0 ?
					err :
					"(no error)");
	}
	assert(rc == OAKENGINE_OK);
	oakengine_export_set_progress_callback(NULL, NULL);

	// Progress was reported, monotonically, and completed.
	assert(g_progress_calls > 0);
	assert(g_progress_monotonic == 1);
	assert(fabs(g_progress_last - 1.0) < 1e-6);

	// Validate the MP4 with ffprobe: h264 video, 320x180, ~1 second.
	assert(access(out, F_OK) == 0);
	char cmd[4608];
	snprintf(cmd, sizeof(cmd),
			 "ffprobe -v error -select_streams v:0 -show_entries "
			 "stream=codec_name,width,height,duration -of csv=p=0 \"%s\"",
			 out);
	FILE *probe = popen(cmd, "r");
	assert(probe != NULL);
	char probe_out[512] = { 0 };
	const size_t probe_len = fread(probe_out, 1, sizeof(probe_out) - 1, probe);
	(void)probe_len;
	assert(pclose(probe) == 0);
	assert(strstr(probe_out, "h264") != NULL);
	assert(strstr(probe_out, "320,180") != NULL);
	// Duration is the last csv field; 30 frames at 30000/1001 ~= 1.001 s.
	const char *comma = strrchr(probe_out, ',');
	assert(comma != NULL);
	const double duration = atof(comma + 1);
	assert(fabs(duration - 1.001) < 0.15);

	oakengine_project_free(project);
	assert(oakengine_shutdown() == OAKENGINE_OK);

	printf("oakengine_export_test: all assertions passed\n");
	return 0;
}
