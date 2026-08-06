/***

  Oak Video Editor - Non-Linear Video Editor
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

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "codec/conform.h"
#include "codec/proxy.h"
#include "codec/task.h"

namespace
{

struct SubmitLog {
	int calls = 0;
	OakCodecTaskRequest last = {};
	std::string input;
	std::string output;
};

int recording_submit(const OakCodecTaskRequest *req, void *userdata)
{
	auto *log = static_cast<SubmitLog *>(userdata);
	log->calls++;
	log->last = *req;
	log->input = req->input_filename ? req->input_filename : "";
	log->output = req->output_filename ? req->output_filename : "";
	// Accept the task but do no work (files never appear).
	return OAKCODEC_OK;
}

struct TaskRegistrarGuard {
	TaskRegistrarGuard() { oakcodec_set_task_submit_cb(nullptr, nullptr); }
	~TaskRegistrarGuard() { oakcodec_set_task_submit_cb(nullptr, nullptr); }
};

} // namespace

TEST(OakCodecTask, RegistryRoundTrip)
{
	TaskRegistrarGuard guard;

	EXPECT_EQ(oakcodec_task_submit_is_registered(), 0);

	SubmitLog log;
	oakcodec_set_task_submit_cb(&recording_submit, &log);
	EXPECT_EQ(oakcodec_task_submit_is_registered(), 1);

	oakcodec_set_task_submit_cb(nullptr, nullptr);
	EXPECT_EQ(oakcodec_task_submit_is_registered(), 0);
}

TEST(OakCodecConform, UnregisteredReportsUnavailable)
{
	TaskRegistrarGuard guard;

	ASSERT_EQ(oakcodec_conform_create_instance(), OAKCODEC_OK);

	int state = oakcodec_conform_get_state("/tmp/oakcodec_conform_test",
									   "some_video.mp4", 1, 48000, 0x3, 4,
									   1);
	EXPECT_EQ(state, OAKCODEC_CONFORM_UNAVAILABLE);

	// Filename computation is still deterministic without a registrar.
	int count = oakcodec_conform_filename_count(
		"/tmp/oakcodec_conform_test", "some_video.mp4", 1, 48000, 0x3, 4);
	EXPECT_GE(count, 1); // stereo layout -> 2 channels
	if (count >= 1) {
		char buf[1024] = {};
		EXPECT_GT(oakcodec_conform_filename_at(
					  "/tmp/oakcodec_conform_test", "some_video.mp4", 1,
					  48000, 0x3, 4, 0, buf, sizeof(buf)),
				  1);
		EXPECT_STRNE(buf, "");
		EXPECT_EQ(oakcodec_conform_filename_at(
					  "/tmp/oakcodec_conform_test", "some_video.mp4", 1,
					  48000, 0x3, 4, count, buf, sizeof(buf)),
				  OAKCODEC_E_NOT_FOUND);
	}

	oakcodec_conform_destroy_instance();
}

TEST(OakCodecConform, RegisteredSubmitIsInvoked)
{
	TaskRegistrarGuard guard;
	SubmitLog log;
	oakcodec_set_task_submit_cb(&recording_submit, &log);

	ASSERT_EQ(oakcodec_conform_create_instance(), OAKCODEC_OK);

	// wait=0: the (no-op) task was "queued" -> GENERATING.
	int state = oakcodec_conform_get_state("/tmp/oakcodec_conform_test",
									   "some_video.mp4", 1, 48000, 0x3, 4,
									   0);
	EXPECT_EQ(state, OAKCODEC_CONFORM_GENERATING);
	EXPECT_EQ(log.calls, 1);
	EXPECT_EQ(log.last.kind, OAKCODEC_TASK_CONFORM);
	EXPECT_EQ(log.last.stream_index, 1);
	EXPECT_EQ(log.last.sample_rate, 48000);

	// wait=1: post-submit miss -> UNAVAILABLE.
	state = oakcodec_conform_get_state("/tmp/oakcodec_conform_test",
								   "some_video.mp4", 1, 48000, 0x3, 4, 1);
	EXPECT_EQ(state, OAKCODEC_CONFORM_UNAVAILABLE);

	oakcodec_conform_destroy_instance();
}

TEST(OakCodecProxy, MissingAndStateStrings)
{
	TaskRegistrarGuard guard;

	ASSERT_EQ(oakcodec_proxy_create_instance(), OAKCODEC_OK);

	EXPECT_EQ(oakcodec_proxy_get_state(nullptr), OAKCODEC_PROXY_STATE_MISSING);
	EXPECT_EQ(oakcodec_proxy_get_state("/nonexistent/proxy.mp4"),
			  OAKCODEC_PROXY_STATE_MISSING);

	char buf[64] = {};
	EXPECT_GT(oakcodec_proxy_state_to_string(OAKCODEC_PROXY_STATE_READY, buf,
										 sizeof(buf)),
			  1);
	EXPECT_EQ(oakcodec_proxy_state_to_string(99, buf, sizeof(buf)),
			  OAKCODEC_E_INVALID);

	oakcodec_proxy_params params = {};
	ASSERT_EQ(oakcodec_proxy_params_default(&params), OAKCODEC_OK);
	EXPECT_GT(params.width, 0);
	EXPECT_STRNE(params.extension, "");

	oakcodec_proxy_result result = {};
	ASSERT_EQ(oakcodec_proxy_get_or_start("/tmp/oakcodec_proxy_test",
									  "some_video.mp4", 0, &params, &result),
			  OAKCODEC_OK);
	// No registrar: stays missing, filename is still computed.
	EXPECT_EQ(result.state, OAKCODEC_PROXY_STATE_MISSING);
	EXPECT_STRNE(result.filename, "");

	oakcodec_proxy_destroy_instance();
}

TEST(OakCodecProxy, RegisteredSubmitIsInvoked)
{
	TaskRegistrarGuard guard;
	SubmitLog log;
	oakcodec_set_task_submit_cb(&recording_submit, &log);

	ASSERT_EQ(oakcodec_proxy_create_instance(), OAKCODEC_OK);

	oakcodec_proxy_params params = {};
	ASSERT_EQ(oakcodec_proxy_params_default(&params), OAKCODEC_OK);

	oakcodec_proxy_result result = {};
	ASSERT_EQ(oakcodec_proxy_get_or_start("/tmp/oakcodec_proxy_test",
									  "some_video.mp4", 2, &params, &result),
			  OAKCODEC_OK);
	EXPECT_EQ(log.calls, 1);
	EXPECT_EQ(log.last.kind, OAKCODEC_TASK_PROXY);
	EXPECT_EQ(log.last.stream_index, 2);
	// Task accepted but produced nothing -> generating.
	EXPECT_EQ(result.state, OAKCODEC_PROXY_STATE_GENERATING);

	oakcodec_proxy_destroy_instance();
}
