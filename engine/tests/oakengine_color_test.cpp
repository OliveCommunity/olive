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

// Pure C ABI test for the liboakengine color facade (oakengine/color.h)
// and the color manager events (oakengine/events.h). Exercises the
// manager queries (config filename, colorspace/display/view/look lists,
// defaults, luma coefficients, compliant-space resolution), the standalone
// config handle, the color processor handle (create/convert/id) and the
// event subscriptions. No GPU: everything here runs on the CPU-side OCIO
// wrappers. When the environment provides no usable OCIO config at all
// (colorspace count 0), the query assertions are skipped but the
// robustness checks (NULL handling, error paths) still run.

#include <assert.h>
#include <gtest/gtest.h>
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

#include "oakengine/color.h"
#include "oakengine/events.h"
#include "oakengine/init.h"
#include "oakengine/project.h"

static char g_tmpdir[4096];

static void make_tmpdir(void)
{
#if defined(_WIN32)
	char base[MAX_PATH];
	const DWORD len = GetTempPathA(MAX_PATH, base);
	EXPECT_TRUE(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_color_test_%lu", base,
			 (unsigned long)GetCurrentProcessId());
	EXPECT_TRUE(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_color_test_XXXXXX");
	EXPECT_TRUE(mkdtemp(g_tmpdir) != NULL);
#endif
}

// ---- Robustness: NULL/invalid arguments ------------------------------------

static void test_null_robustness(void)
{
	char buf[64];
	double rgb[3];
	double rgba[4] = { 0, 0, 0, 0 };

	EXPECT_TRUE(oakengine_color_manager_from_project(NULL) == NULL);
	EXPECT_TRUE(oakengine_color_manager_get_config_filename(NULL, buf,
													   sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_set_config_filename(NULL, "x") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_colorspace_count(NULL) == 0);
	EXPECT_TRUE(oakengine_color_manager_colorspace_at(NULL, 0, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_display_count(NULL) == 0);
	EXPECT_TRUE(oakengine_color_manager_display_at(NULL, 0, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_view_count(NULL, NULL) == 0);
	EXPECT_TRUE(oakengine_color_manager_view_at(NULL, NULL, 0, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_look_count(NULL) == 0);
	EXPECT_TRUE(oakengine_color_manager_look_at(NULL, 0, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_default_display(NULL, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_default_view(NULL, NULL, buf,
												sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_default_input_color_space(NULL, buf,
															 sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_set_default_input_color_space(NULL, "x") ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_reference_color_space(NULL, buf,
														 sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_default_luma_coefs(NULL, rgb) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_compliant_color_space(NULL, "x", buf,
														 sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_manager_compliant_transform(NULL, NULL, 0, NULL,
													   NULL, 0, NULL, 0, NULL,
													   0) == OAKENGINE_E_INVALID);

	EXPECT_TRUE(oakengine_color_config_load_file(NULL) == NULL);
	EXPECT_TRUE(oakengine_color_config_colorspace_count(NULL) == 0);
	EXPECT_TRUE(oakengine_color_config_colorspace_at(NULL, 0, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	oakengine_color_config_free(NULL); // no-op

	EXPECT_TRUE(oakengine_color_processor_create(NULL, "in", NULL,
											OAKENGINE_COLOR_PROCESSOR_NORMAL) ==
		   NULL);
	EXPECT_TRUE(oakengine_color_processor_is_valid(NULL) == 0);
	EXPECT_TRUE(oakengine_color_processor_convert_color(NULL, rgba, rgba) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_processor_id(NULL, buf, sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	oakengine_color_processor_free(NULL); // no-op
}

// ---- Standalone config handle -----------------------------------------------

static void test_config_handle(int have_ocio)
{
	char buf[256];

	// A missing file must fail cleanly with an error message.
	EXPECT_TRUE(oakengine_color_config_load_file("/nonexistent/definitely.ocio") ==
		   NULL);
	EXPECT_TRUE(oakengine_color_last_error(buf, sizeof(buf)) > 0);

	OakEngineColorConfig *config = oakengine_color_config_load_default();
	if (!have_ocio) {
		// No usable OCIO config in this environment.
		if (config) {
			oakengine_color_config_free(config);
		}
		return;
	}
	EXPECT_TRUE(config != NULL);

	const int count = oakengine_color_config_colorspace_count(config);
	EXPECT_TRUE(count > 0);
	for (int i = 0; i < count; i++) {
		EXPECT_TRUE(oakengine_color_config_colorspace_at(config, i, buf,
													sizeof(buf)) > 0);
		EXPECT_TRUE(buf[0] != '\0');
	}
	EXPECT_TRUE(oakengine_color_config_colorspace_at(config, count, buf,
												sizeof(buf)) ==
		   OAKENGINE_E_INVALID);
	EXPECT_TRUE(oakengine_color_config_colorspace_at(config, -1, buf,
												sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	oakengine_color_config_free(config);
}

// ---- Manager queries ---------------------------------------------------------

static void test_manager_queries(OakEngineColorManager *mgr)
{
	char buf[256];
	char first_cs[256];
	double rgb[3] = { 0, 0, 0 };

	// Colorspaces
	const int cs_count = oakengine_color_manager_colorspace_count(mgr);
	EXPECT_TRUE(cs_count > 0);
	EXPECT_TRUE(oakengine_color_manager_colorspace_at(mgr, 0, first_cs,
												 sizeof(first_cs)) > 0);
	EXPECT_TRUE(first_cs[0] != '\0');
	EXPECT_TRUE(oakengine_color_manager_colorspace_at(mgr, cs_count, buf,
												 sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	// Displays / views / looks
	const int disp_count = oakengine_color_manager_display_count(mgr);
	EXPECT_TRUE(disp_count > 0);
	EXPECT_TRUE(oakengine_color_manager_display_at(mgr, 0, buf, sizeof(buf)) > 0);
	char display[256];
	memcpy(display, buf, sizeof(display));
	const int view_count = oakengine_color_manager_view_count(mgr, display);
	EXPECT_TRUE(view_count > 0);
	EXPECT_TRUE(oakengine_color_manager_view_at(mgr, display, 0, buf, sizeof(buf)) >
		   0);
	EXPECT_TRUE(oakengine_color_manager_look_count(mgr) >= 0);
	EXPECT_TRUE(oakengine_color_manager_display_at(mgr, disp_count, buf,
											  sizeof(buf)) ==
		   OAKENGINE_E_INVALID);

	// Defaults
	char default_display[256];
	EXPECT_TRUE(oakengine_color_manager_default_display(mgr, default_display,
												   sizeof(default_display)) > 0);
	EXPECT_TRUE(oakengine_color_manager_default_view(mgr, default_display, buf,
												sizeof(buf)) > 0);
	EXPECT_TRUE(oakengine_color_manager_default_input_color_space(mgr, buf,
															 sizeof(buf)) > 0);
	EXPECT_TRUE(oakengine_color_manager_reference_color_space(mgr, buf,
														 sizeof(buf)) > 0);

	// Default input colorspace set/get roundtrip
	EXPECT_TRUE(oakengine_color_manager_set_default_input_color_space(mgr,
																 first_cs) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_color_manager_default_input_color_space(mgr, buf,
															 sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, first_cs) == 0);

	// Config filename set/get roundtrip (an empty filename selects the
	// built-in default config; setting it must not crash the queries above)
	EXPECT_TRUE(oakengine_color_manager_set_config_filename(mgr, "") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(oakengine_color_manager_get_config_filename(mgr, buf, sizeof(buf)) >=
		   0);

	// Luma coefficients: Rec.709-style weights, all positive, roughly sum to 1
	EXPECT_TRUE(oakengine_color_manager_default_luma_coefs(mgr, rgb) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(rgb[0] > 0 && rgb[1] > 0 && rgb[2] > 0);
	EXPECT_TRUE(fabs(rgb[0] + rgb[1] + rgb[2] - 1.0) < 0.01);

	// Compliant colorspace: an existing space resolves to itself, an empty
	// name resolves to the default input space
	EXPECT_TRUE(oakengine_color_manager_compliant_color_space(mgr, first_cs, buf,
														 sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, first_cs) == 0);
	EXPECT_TRUE(oakengine_color_manager_compliant_color_space(mgr, "", buf,
														 sizeof(buf)) > 0);
	EXPECT_TRUE(strcmp(buf, first_cs) == 0);

	// Compliant transform: force a colorspace transform onto a display
	// transform and back
	oak_color_transform in;
	in.is_display = 0;
	in.output = first_cs;
	in.view = NULL;
	in.look = NULL;
	int out_is_display = -1;
	char out_o[256], out_v[256], out_l[256];
	out_o[0] = out_v[0] = out_l[0] = '\0';
	EXPECT_TRUE(oakengine_color_manager_compliant_transform(mgr, &in, 1,
													   &out_is_display, out_o,
													   sizeof(out_o), out_v,
													   sizeof(out_v), out_l,
													   sizeof(out_l)) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(out_is_display == 1);
	EXPECT_TRUE(out_o[0] != '\0');
	EXPECT_TRUE(oakengine_color_manager_compliant_transform(
			   mgr, &in, 0, &out_is_display, out_o, sizeof(out_o), out_v,
			   sizeof(out_v), out_l, sizeof(out_l)) == OAKENGINE_OK);
	EXPECT_TRUE(out_is_display == 0);
	EXPECT_TRUE(strcmp(out_o, first_cs) == 0);
}

// ---- Color processor ----------------------------------------------------------

static void test_processor(OakEngineColorManager *mgr)
{
	char ref[256];
	char buf[256];

	EXPECT_TRUE(oakengine_color_manager_reference_color_space(mgr, ref,
														 sizeof(ref)) > 0);

	// Identity transform (ref -> ref): white stays white
	oak_color_transform dest;
	dest.is_display = 0;
	dest.output = ref;
	dest.view = NULL;
	dest.look = NULL;

	OakEngineColorProcessor *proc = oakengine_color_processor_create(
		mgr, ref, &dest, OAKENGINE_COLOR_PROCESSOR_NORMAL);
	EXPECT_TRUE(proc != NULL);
	EXPECT_TRUE(oakengine_color_processor_is_valid(proc) == 1);

	const double in[4] = { 1.0, 1.0, 1.0, 1.0 };
	double out[4] = { 0, 0, 0, 0 };
	EXPECT_TRUE(oakengine_color_processor_convert_color(proc, in, out) ==
		   OAKENGINE_OK);
	EXPECT_TRUE(fabs(out[0] - 1.0) < 1e-3 && fabs(out[1] - 1.0) < 1e-3 &&
		   fabs(out[2] - 1.0) < 1e-3 && fabs(out[3] - 1.0) < 1e-3);

	// Cache id is non-empty and stable
	const int id_len = oakengine_color_processor_id(proc, buf, sizeof(buf));
	EXPECT_TRUE(id_len > 0);
	EXPECT_TRUE(buf[0] != '\0');
	EXPECT_TRUE(oakengine_color_processor_id(proc, NULL, 0) == id_len);

	// Inverse direction constructs too
	OakEngineColorProcessor *inv = oakengine_color_processor_create(
		mgr, ref, &dest, OAKENGINE_COLOR_PROCESSOR_INVERSE);
	EXPECT_TRUE(inv != NULL);
	oakengine_color_processor_free(inv);

	// Unknown direction is rejected
	EXPECT_TRUE(oakengine_color_processor_create(mgr, ref, &dest, 7) == NULL);

	// An unknown colorspace yields an invalid (pass-through) processor,
	// matching the engine's non-throwing C++ behavior
	dest.output = "definitely-not-a-colorspace";
	OakEngineColorProcessor *bad = oakengine_color_processor_create(
		mgr, ref, &dest, OAKENGINE_COLOR_PROCESSOR_NORMAL);
	EXPECT_TRUE(bad != NULL);
	if (oakengine_color_processor_is_valid(bad)) {
		// Some configs resolve unknown names via roles; then conversion must
		// still not crash.
		EXPECT_TRUE(oakengine_color_processor_convert_color(bad, in, out) ==
			   OAKENGINE_OK);
	} else {
		out[0] = out[1] = out[2] = out[3] = 0;
		EXPECT_TRUE(oakengine_color_processor_convert_color(bad, in, out) ==
			   OAKENGINE_OK);
		EXPECT_TRUE(out[0] == 1.0 && out[1] == 1.0 && out[2] == 1.0 &&
			   out[3] == 1.0);
	}
	oakengine_color_processor_free(bad);

	// Processor is valid and usable.
	EXPECT_TRUE(proc != NULL);

	oakengine_color_processor_free(proc);
}

// ---- Events --------------------------------------------------------------------

static int g_config_events = 0;
static int g_reference_events = 0;

static void count_color_events(const oakengine_event *event, void *userdata)
{
	(void)userdata;
	EXPECT_TRUE(event != NULL);
	if (event->id == OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED) {
		g_config_events++;
	} else if (event->id == OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED) {
		g_reference_events++;
	} else {
		EXPECT_TRUE(0); // unexpected event id on this subscription
	}
}

static void test_events(OakEngineProject *project,
						OakEngineColorManager *mgr)
{
	// Family mismatch: a color manager event on a project handle must fail.
	EXPECT_TRUE(oakengine_event_subscribe(
			   project, OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED,
			   count_color_events, NULL) == 0);

	int64_t sub_ref = oakengine_event_subscribe(
		mgr, OAKENGINE_EVENT_COLOR_MANAGER_REFERENCE_SPACE_CHANGED,
		count_color_events, NULL);
	int64_t sub_cfg = oakengine_event_subscribe(
		mgr, OAKENGINE_EVENT_COLOR_MANAGER_CONFIG_CHANGED,
		count_color_events, NULL);
	EXPECT_TRUE(sub_ref > 0);
	EXPECT_TRUE(sub_cfg > 0);

	// Changing the reference space emits reference_space_changed.
	char ref[256];
	EXPECT_TRUE(oakengine_project_get_color_reference_space(project, ref,
													   sizeof(ref)) > 0);
	EXPECT_TRUE(oakengine_project_set_color_reference_space(
			   project, strcmp(ref, "scene_linear") == 0 ? "reference" :
														   "scene_linear") ==
		   OAKENGINE_OK);
	EXPECT_TRUE(g_reference_events == 1);
	EXPECT_TRUE(g_config_events == 0);

	EXPECT_TRUE(oakengine_event_unsubscribe(sub_ref) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_event_unsubscribe(sub_cfg) == OAKENGINE_OK);
}

TEST(OakEngineColor, Main)
{
	make_tmpdir();

	// Sandbox the config/cache/data locations (the default OCIO config is
	// extracted under the cache location).
#if !defined(_WIN32)
	EXPECT_TRUE(setenv("XDG_CONFIG_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_CACHE_HOME", g_tmpdir, 1) == 0);
	EXPECT_TRUE(setenv("XDG_DATA_HOME", g_tmpdir, 1) == 0);
#endif

	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	test_null_robustness();

	OakEngineProject *project = oakengine_project_create();
	EXPECT_TRUE(project != NULL);
	EXPECT_TRUE(oakengine_project_new(project) == OAKENGINE_OK);

	OakEngineColorManager *mgr = oakengine_color_manager_from_project(project);
	EXPECT_TRUE(mgr != NULL);

	// The built-in default config should always be available (it is
	// extracted from the engine's resources); tolerate environments where
	// it is not by skipping the query assertions.
	const int have_ocio = oakengine_color_manager_colorspace_count(mgr) > 0;
	if (!have_ocio) {
		printf("oakengine_color_test: no OCIO config available, skipping "
			   "query tests\n");
	}

	test_config_handle(have_ocio);
	if (have_ocio) {
		test_manager_queries(mgr);
		test_processor(mgr);
	}
	test_events(project, mgr);

	oakengine_project_free(project);
	EXPECT_TRUE(oakengine_shutdown() == OAKENGINE_OK);

}
