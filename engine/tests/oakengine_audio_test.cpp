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

// Pure C ABI test for the liboakengine audio I/O family
// (oakengine/audio.h). Exercises the AudioManager instance lifecycle,
// input/output device get/set round-trips, output push error paths and the
// output_params_changed event subscription. No GL or QApplication required.

#include <assert.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <string.h>

#include "oakengine/audio.h"
#include "oakengine/events.h"
#include "oakengine/init.h"

static int g_output_params_changed_count;
static void *g_output_params_changed_source;

static void on_output_params_changed(const oakengine_event *event, void *)
{
	EXPECT_TRUE(event != NULL);
	EXPECT_TRUE(event->id == OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED);
	g_output_params_changed_count++;
	g_output_params_changed_source = event->source;
}

static void test_instance_lifecycle(void)
{
	// No instance before create.
	EXPECT_TRUE(oakengine_audio_manager_handle() == NULL);
	EXPECT_TRUE(oakengine_audio_get_output_device() == -1);
	EXPECT_TRUE(oakengine_audio_get_input_device() == -1);
	EXPECT_TRUE(oakengine_audio_clear_buffered_output() == OAKENGINE_E_STATE);
	EXPECT_TRUE(oakengine_audio_stop_recording() == OAKENGINE_E_STATE);

	EXPECT_TRUE(oakengine_audio_create_instance() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_manager_handle() != NULL);

	// Idempotent create is allowed.
	EXPECT_TRUE(oakengine_audio_create_instance() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_manager_handle() != NULL);

	oakengine_audio_destroy_instance();
	EXPECT_TRUE(oakengine_audio_manager_handle() == NULL);

	// Idempotent destroy is allowed.
	EXPECT_TRUE(oakengine_audio_destroy_instance() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_manager_handle() == NULL);
}

static void test_device_round_trip(void)
{
	EXPECT_TRUE(oakengine_audio_create_instance() == OAKENGINE_OK);
	void *handle = oakengine_audio_manager_handle();
	EXPECT_TRUE(handle != NULL);

	// Default is usually paNoDevice (-1) in headless environments.
	const int64_t original_output = oakengine_audio_get_output_device();
	const int64_t original_input = oakengine_audio_get_input_device();

	// Setting a value should change the returned value.
	EXPECT_TRUE(oakengine_audio_set_output_device(42) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_get_output_device() == 42);

	EXPECT_TRUE(oakengine_audio_set_input_device(43) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_get_input_device() == 43);

	// hard_reset re-initializes PortAudio and should not crash.
	EXPECT_TRUE(oakengine_audio_hard_reset() == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_manager_handle() == handle);

	// Restore original values.
	EXPECT_TRUE(oakengine_audio_set_output_device(original_output) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_set_input_device(original_input) == OAKENGINE_OK);
	EXPECT_TRUE(oakengine_audio_get_output_device() == original_output);
	EXPECT_TRUE(oakengine_audio_get_input_device() == original_input);

	oakengine_audio_destroy_instance();
}

static void test_push_to_output_errors(void)
{
	EXPECT_TRUE(oakengine_audio_create_instance() == OAKENGINE_OK);

	char error_buf[256];
	memset(error_buf, 0, sizeof(error_buf));

	// NULL params is rejected without crashing.
	EXPECT_TRUE(oakengine_audio_push_to_output(NULL, "x", 1, error_buf,
									  sizeof(error_buf)) ==
		   OAKENGINE_E_INVALID);

	// NULL samples is rejected.
	EXPECT_TRUE(oakengine_audio_push_to_output((const OakAudioParams *)1, NULL, 1,
									  error_buf, sizeof(error_buf)) ==
		   OAKENGINE_E_INVALID);

	oakengine_audio_destroy_instance();
}

static void test_output_params_changed_event(void)
{
	EXPECT_TRUE(oakengine_audio_create_instance() == OAKENGINE_OK);
	void *handle = oakengine_audio_manager_handle();
	EXPECT_TRUE(handle != NULL);

	g_output_params_changed_count = 0;
	g_output_params_changed_source = NULL;

	const int64_t sub = oakengine_event_subscribe(
		handle, OAKENGINE_EVENT_AUDIO_MANAGER_OUTPUT_PARAMS_CHANGED,
		on_output_params_changed, NULL);
	EXPECT_TRUE(sub > 0);

	const int64_t original_output = oakengine_audio_get_output_device();
	EXPECT_TRUE(oakengine_audio_set_output_device(84) == OAKENGINE_OK);
	EXPECT_TRUE(g_output_params_changed_count >= 1);
	EXPECT_TRUE(g_output_params_changed_source == handle);

	EXPECT_TRUE(oakengine_event_unsubscribe(sub) == OAKENGINE_OK);

	// No further deliveries after unsubscribe.
	const int count_after_unsub = g_output_params_changed_count;
	EXPECT_TRUE(oakengine_audio_set_output_device(85) == OAKENGINE_OK);
	EXPECT_TRUE(g_output_params_changed_count == count_after_unsub);

	// Restore original value.
	EXPECT_TRUE(oakengine_audio_set_output_device(original_output) == OAKENGINE_OK);

	oakengine_audio_destroy_instance();
}

static void test_audio_sync_algorithms(void)
{
	// place_by_waveform_offset: a 1-second positive offset at 48 kHz
	oak_audio_sync_placement placement;
	EXPECT_TRUE(oakengine_audio_sync_place_by_waveform_offset(
				0, 1, 48000, 48000, &placement) == OAKENGINE_OK);
	EXPECT_TRUE(placement.valid);
	EXPECT_TRUE(placement.timeline_in_num == 1 && placement.timeline_in_den == 1);

	// place_by_source_time: matching source/media in points -> same timeline in
	oak_audio_sync_source_clip ref = { 0, 1, 0, 1, 1 };
	oak_audio_sync_source_clip cand = { 0, 1, 0, 1, 1 };
	EXPECT_TRUE(oakengine_audio_sync_place_by_source_time(
				&ref, &cand, 5, 1, &placement) == OAKENGINE_OK);
	EXPECT_TRUE(placement.valid);
	EXPECT_TRUE(placement.timeline_in_num == 5 && placement.timeline_in_den == 1);

	// estimate_envelope_offset: identical envelopes -> zero offset, high
	// confidence
	double envelope[10] = { 0, 1, 2, 3, 4, 5, 4, 3, 2, 1 };
	oak_audio_waveform_offset offset;
	EXPECT_TRUE(oakengine_audio_estimate_envelope_offset(
				envelope, 10, envelope, 10, NULL, 0, NULL, 0, 1, 5,
				&offset) == OAKENGINE_OK);
	EXPECT_TRUE(offset.valid);
	EXPECT_TRUE(offset.offset_samples == 0);
	EXPECT_TRUE(offset.confidence > 0.99);

	// estimate_stretch_and_offset: identical envelopes at rate 1 -> zero offset
	oak_audio_waveform_stretch_offset stretch;
	EXPECT_TRUE(oakengine_audio_estimate_stretch_and_offset(
				envelope, 10, envelope, 10, NULL, 0, NULL, 0, 1, 5, 0.9, 1.1,
				0.05, &stretch) == OAKENGINE_OK);
	EXPECT_TRUE(stretch.valid);
	EXPECT_TRUE(stretch.offset_samples == 0);
	EXPECT_TRUE(stretch.rate > 0.99 && stretch.rate < 1.01);
}

TEST(OakEngineAudio, Main)
{
	EXPECT_TRUE(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	test_instance_lifecycle();
	test_device_round_trip();
	test_push_to_output_errors();
	test_output_params_changed_event();
	test_audio_sync_algorithms();

	oakengine_shutdown();

	printf("oakengine_audio_test: OK\n");
}
