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

// Pure C ABI test for the liboakengine sync facade. The validation part
// (handle checking, not-initialized errors) requires no GL and must
// always pass. The estimation part renders the clips' audio, so it is
// GL-gated like oakengine_playback_test (dynamic backend probe +
// worker binary, SKIP with exit 0 when unavailable).

#include <assert.h>
#include <math.h>
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

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include "config/config.h"
#include "oakengine/footage.h"
#include "oakengine/init.h"
#include "oakengine/project.h"
#include "oakengine/sync.h"
#include "oakengine/timeline.h"
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
	assert(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_sync_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	assert(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_sync_test_XXXXXX");
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

// demo.mp4's audio is near-silent and useless for correlation, and
// stationary noise or a constant chirp both have a flat RMS envelope
// (no lag peak). Write noise with a deterministic per-window random
// gain: a textured, unique envelope for both the lag and rate search.
static void write_textured_wav(const QString &path, int seconds)
{
	const int rate = 48000;
	const int channels = 2;
	const int frames = rate * seconds;
	const int data_size = frames * channels * int(sizeof(int16_t));
	const int block = rate / 20; // one gain value per envelope window

	QFile f(path);
	assert(f.open(QFile::WriteOnly));
	auto write_u32 = [&f](uint32_t v) {
		f.write(reinterpret_cast<const char *>(&v), 4);
	};
	auto write_u16 = [&f](uint16_t v) {
		f.write(reinterpret_cast<const char *>(&v), 2);
	};

	f.write("RIFF", 4);
	write_u32(uint32_t(36 + data_size));
	f.write("WAVE", 4);
	f.write("fmt ", 4);
	write_u32(16);
	write_u16(1); // PCM
	write_u16(uint16_t(channels));
	write_u32(uint32_t(rate));
	write_u32(uint32_t(rate * channels * int(sizeof(int16_t))));
	write_u16(uint16_t(channels * int(sizeof(int16_t))));
	write_u16(16);
	f.write("data", 4);
	write_u32(uint32_t(data_size));

	uint32_t state = 0x12345678u;
	auto next_u32 = [&state]() {
		state = state * 1664525u + 1013904223u;
		return state;
	};

	const int blocks = frames / block + 2;
	std::vector<double> block_gains(static_cast<size_t>(blocks));
	for (int b = 0; b < blocks; b++) {
		block_gains[size_t(b)] =
			0.1 + 0.9 * double(next_u32() % 1000) / 1000.0;
	}

	for (int i = 0; i < frames; i++) {
		// Constant gain within a block: the envelope window equals the
		// block, so envelope[b] == block_gains[b] (sharp and unique).
		const double gain = block_gains[size_t(i / block)];
		const int16_t sample = int16_t(
			(int(next_u32() >> 16) % 32768 - 16384) * gain);
		for (int ch = 0; ch < channels; ch++) {
			f.write(reinterpret_cast<const char *>(&sample), 2);
		}
	}
	f.close();
}

// The shared fixture: one sequence with an audio track and two clips of
// the noise footage; the target's content starts k_offset_frames later
// in the source (the application's real sync scenario: two recordings
// of one event, one started late). The sequence runs at 20 fps so one
// frame is exactly one envelope window (1/20 s).
static const int64_t k_offset_frames = 8;

static OakEngineClip *make_pair(OakEngineProject *project,
								OakEngineSequence *seq,
								const char *media_path,
								OakEngineClip **target_out)
{
	assert(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) ==
		   0);
	// A second audio track: placing the target on the SAME track would
	// overwrite (trim) the reference clip.
	assert(oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) ==
		   1);
	OakEngineFootage *footage =
		oakengine_project_import_footage(project, media_path);
	assert(footage != NULL);
	OakEngineClip *reference = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_AUDIO, 0, 0, 160, 0);
	assert(reference != NULL);
	OakEngineClip *target = oakengine_sequence_add_footage_clip(
		seq, footage, OAKENGINE_TRACK_TYPE_AUDIO, 1, k_offset_frames,
		160 + k_offset_frames, k_offset_frames);
	assert(target != NULL);
	oakengine_footage_free(footage);
	*target_out = target;
	return reference;
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

	// HEADLESS is enough for the validation part.
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	OakEngineProject *project = oakengine_project_create();
	assert(project != NULL);
	assert(oakengine_project_new(project) == OAKENGINE_OK);
	OakEngineSequence *seq = oakengine_sequence_new(project, "Sync");
	assert(seq != NULL);
	// 20 fps: one frame == one envelope window (1/20 s) exactly.
	assert(oakengine_sequence_set_video_params(seq, -1, -1, 20, 1, -1, -1,
											   -1, -1, 1) == OAKENGINE_OK);

	const QString noise_path = QDir(QString::fromUtf8(g_tmpdir))
								   .filePath(QStringLiteral("sync-noise.wav"));
	write_textured_wav(noise_path, 8);
	OakEngineClip *target = NULL;
	OakEngineClip *reference =
		make_pair(project, seq, noise_path.toUtf8().constData(), &target);

	// ---- Validation (no GL) -------------------------------------------
	double offset_s = -1, confidence = -1, stretch = -1;
	assert(oakengine_sync_estimate_offset(NULL, reference, target,
										  &offset_s,
										  &confidence) == OAKENGINE_E_INVALID);
	assert(oakengine_sync_estimate_offset(seq, NULL, target, &offset_s,
										  &confidence) == OAKENGINE_E_INVALID);
	assert(oakengine_sync_estimate_offset(seq, reference, NULL, &offset_s,
										  &confidence) == OAKENGINE_E_INVALID);
	assert(oakengine_sync_estimate_stretch_offset(NULL, reference, target,
												  &stretch, &offset_s,
												  &confidence) ==
		   OAKENGINE_E_INVALID);
	char err[256];
	assert(oakengine_sync_last_error(err, sizeof(err)) > 0);

	// Valid handles but the engine lacks the RENDER bit: OAKENGINE_E_STATE
	// with a readable reason, nothing else changed.
	assert(oakengine_sync_estimate_offset(seq, reference, target, &offset_s,
										  &confidence) == OAKENGINE_E_STATE);
	assert(oakengine_sync_last_error(err, sizeof(err)) > 0);
	assert(strstr(err, "OAKENGINE_INIT_RENDER") != NULL);

	// ---- GL-gated estimation ------------------------------------------
	if (!is_render_backend_available(QStringLiteral("opengl"))) {
		printf("oakengine_sync_test: SKIP: OpenGL render backend not "
			   "available, estimation assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return 0;
	}
	if (!worker_binary_exists()) {
		printf("oakengine_sync_test: SKIP: oak-render-worker binary not "
			   "found, estimation assertions skipped\n");
		oakengine_project_free(project);
		oakengine_shutdown();
		return 0;
	}

	olive::Config::current()[QStringLiteral("GraphicsBackend")] =
		QStringLiteral("opengl");
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) ==
		   OAKENGINE_OK);

	// The target's content starts k_offset_frames later in the source:
	// the estimator must report that offset back (negative = move the
	// target earlier). At 20 fps one frame is one envelope window, so
	// the expected value is exact; the tolerance is one window (the
	// method's quantization).
	const double expected_s = double(k_offset_frames) / 20.0;
	const double tolerance_s = 1.0 / 20.0;

	const int est_rc = oakengine_sync_estimate_offset(
		seq, reference, target, &offset_s, &confidence);
	if (est_rc != OAKENGINE_OK) {
		char est_err[512];
		est_err[0] = '\0';
		oakengine_sync_last_error(est_err, sizeof(est_err));
		fprintf(stderr, "DEBUG est_rc=%d off=%f conf=%f err='%s'\n", est_rc,
				offset_s, confidence, est_err);
	}
	assert(est_rc == OAKENGINE_OK);
	assert(fabs(fabs(offset_s) - expected_s) < tolerance_s);
	assert(offset_s < 0.0); // the target is delayed: it must move earlier
	assert(confidence > 0.0 && confidence <= 1.0);

	// Same-speed content: the stretch estimator reports rate ~1 and the
	// same offset.
	const int str_rc = oakengine_sync_estimate_stretch_offset(
		seq, reference, target, &stretch, &offset_s, &confidence);
	assert(str_rc == OAKENGINE_OK);
	assert(fabs(stretch - 1.0) < 0.01);
	assert(fabs(fabs(offset_s) - expected_s) < tolerance_s);

	oakengine_project_free(project);
	assert(oakengine_shutdown() == OAKENGINE_OK);

	printf("oakengine_sync_test: all assertions passed\n");
	return 0;
}
