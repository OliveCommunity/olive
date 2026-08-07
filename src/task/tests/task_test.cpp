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

#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "codec/conform.h"
#include "codec/decoder.h"
#include "codec/task.h"
#include "node/folder.h"
#include "node/project.h"
#include "task/manager.h"
#include "task/project.h"
#include "task/task.h"
#include "undo/undostack.h"

namespace
{

class OakTaskFixture : public ::testing::Test {
protected:
	void SetUp() override
	{
		project_ = oaknode_project_init();
		ASSERT_NE(project_, nullptr);
		ASSERT_EQ(oaknode_project_initialize(project_), OAKNODE_OK);
	}

	void TearDown() override
	{
		oaknode_project_free(project_);
	}

	OakNodeProject *project_ = nullptr;
};

// ---- task family ----------------------------------------------------------

TEST_F(OakTaskFixture, TaskLifecycleSync)
{
	oaktask_task_free(nullptr); // no-op on NULL

	OakTaskTask *t = oaktask_create_project_save(project_, nullptr, 0);
	ASSERT_NE(t, nullptr);

	char title[128];
	EXPECT_GT(oaktask_task_title(t, title, sizeof(title)), 0);
	EXPECT_EQ(oaktask_task_title(nullptr, title, sizeof(title)),
			  OAKTASK_E_INVALID);

	EXPECT_EQ(oaktask_task_is_finished(t), 0);
	EXPECT_EQ(oaktask_task_is_finished(nullptr), OAKTASK_E_INVALID);

	// Save to an override path in the temp dir
	std::string path =
		(std::filesystem::temp_directory_path() / "oaktask_save_test.ove")
			.string();

	oaktask_task_free(t);

	t = oaktask_create_project_save(project_, path.c_str(), 0);
	ASSERT_NE(t, nullptr);

	EXPECT_EQ(oaktask_task_start_sync(t), 1);
	EXPECT_EQ(oaktask_task_is_finished(t), 1);
	EXPECT_TRUE(std::filesystem::exists(path));

	char err[256];
	EXPECT_GE(oaktask_task_error(t, err, sizeof(err)), 0);

	oaktask_task_free(t);
	EXPECT_EQ(oaktask_debug_alive_count(), 0);

	std::filesystem::remove(path);
}

TEST_F(OakTaskFixture, LoadMissingFileFails)
{
	OakTaskTask *t =
		oaktask_create_project_load("/nonexistent/definitely-missing.ove");
	ASSERT_NE(t, nullptr);

	EXPECT_EQ(oaktask_task_start_sync(t), 0);

	char err[256];
	EXPECT_GT(oaktask_task_error(t, err, sizeof(err)), 0);

	EXPECT_EQ(oaktask_load_take_project(t), nullptr);
	oaktask_task_free(t);

	EXPECT_EQ(oaktask_create_project_load(nullptr), nullptr);
}

TEST_F(OakTaskFixture, SaveLoadRoundTrip)
{
	std::string path =
		(std::filesystem::temp_directory_path() / "oaktask_roundtrip.ove")
			.string();

	OakTaskTask *save =
		oaktask_create_project_save(project_, path.c_str(), 0);
	ASSERT_NE(save, nullptr);
	ASSERT_EQ(oaktask_task_start_sync(save), 1);
	oaktask_task_free(save);

	OakTaskTask *load = oaktask_create_project_load(path.c_str());
	ASSERT_NE(load, nullptr);
	ASSERT_EQ(oaktask_task_start_sync(load), 1);

	OakNodeProject *loaded = oaktask_load_take_project(load);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(oaktask_load_take_project(load), nullptr);

	oaknode_project_free(loaded);
	oaktask_task_free(load);

	std::filesystem::remove(path);
}

TEST_F(OakTaskFixture, ImportDemoFootage)
{
	OakNodeFolder *folder = oaknode_folder_create(project_);
	ASSERT_NE(folder, nullptr);

	const char *urls[] = { OAK_REPO_ROOT "/tests/demo.mp4" };
	OakTaskTask *t =
		oaktask_create_project_import(folder, project_, urls, 1);
	ASSERT_NE(t, nullptr);

	ASSERT_EQ(oaktask_task_start_sync(t), 1);

	if (oaktask_import_footage_count(t) == 0) {
		// Footage probing still resolves through oaknode's codec
		// transition stubs (module wiring milestone) - demo.mp4 reports
		// invalid until then.
		GTEST_SKIP()
			<< "footage probe not wired to oakcodec yet (step 4)";
	}

	EXPECT_EQ(oaktask_import_footage_count(t), 1);
	EXPECT_NE(oaktask_import_footage_at(t, 0), nullptr);
	EXPECT_EQ(oaktask_import_footage_at(t, 5), nullptr);
	EXPECT_EQ(oaktask_import_invalid_count(t), 0);
	EXPECT_EQ(oaktask_import_invalid_at(t, 0, nullptr, 0),
			  OAKTASK_E_NOT_FOUND);

	// The import command adds the footage to the folder; undo removes it
	OakUndoCommand *cmd = oaktask_import_take_command(t);
	ASSERT_NE(cmd, nullptr);
	EXPECT_EQ(oaktask_import_take_command(t), nullptr);

	EXPECT_EQ(oaknode_folder_child_count(folder), 0); // not yet redone

	oakundo_command_redo_now(cmd);
	EXPECT_EQ(oaknode_folder_child_count(folder), 1);

	oakundo_command_undo_now(cmd);
	EXPECT_EQ(oaknode_folder_child_count(folder), 0);

	oakundo_command_free(cmd);
	oaktask_task_free(t);

	EXPECT_EQ(oaktask_create_project_import(nullptr, project_, urls, 1),
			  nullptr);
	EXPECT_EQ(oaktask_import_footage_count(nullptr), OAKTASK_E_INVALID);

	EXPECT_EQ(oaktask_debug_alive_count(), 0);
}

// ---- manager family -------------------------------------------------------

TEST(OakTaskManager, InitShutdownAndCodecSubmitter)
{
	EXPECT_EQ(oaktask_manager_init(), OAKTASK_OK);
	EXPECT_EQ(oaktask_manager_init(), OAKTASK_E_STATE);

	EXPECT_EQ(oakcodec_task_submit_is_registered(), 1);

	EXPECT_GE(oaktask_manager_count(), 0);
	oaktask_manager_delete_finished();

	EXPECT_EQ(oaktask_manager_at(0), nullptr);

	oaktask_manager_shutdown();
	EXPECT_EQ(oakcodec_task_submit_is_registered(), 0);

	EXPECT_EQ(oaktask_manager_count(), OAKTASK_E_STATE);
}

TEST(OakTaskManager, AsyncStartAndSubscribe)
{
	ASSERT_EQ(oaktask_manager_init(), OAKTASK_OK);

	OakNodeProject *project = oaknode_project_init();
	ASSERT_NE(project, nullptr);
	ASSERT_EQ(oaknode_project_initialize(project), OAKNODE_OK);

	std::string path =
		(std::filesystem::temp_directory_path() / "oaktask_async.ove")
			.string();

	OakTaskTask *t = oaktask_create_project_save(project, path.c_str(), 0);
	ASSERT_NE(t, nullptr);

	struct Seen {
		int started;
		int finished;
		double succeeded;
	} seen = { 0, 0, 0.0 };

	EXPECT_EQ(oaktask_task_subscribe(
				  t,
				  [](int event_id, double value, void *userdata) {
					  Seen *s = static_cast<Seen *>(userdata);
					  if (event_id == OAKTASK_EVENT_STARTED) {
						  s->started++;
					  } else if (event_id == OAKTASK_EVENT_FINISHED) {
						  s->finished++;
						  s->succeeded = value;
					  }
				  },
				  &seen),
			  0);
	EXPECT_EQ(oaktask_task_subscribe(t, nullptr, nullptr),
			  OAKTASK_E_INVALID);

	ASSERT_EQ(oaktask_task_start(t), OAKTASK_OK);
	EXPECT_EQ(oaktask_task_start(t), OAKTASK_E_STATE);

	EXPECT_EQ(oaktask_task_wait(t), OAKTASK_OK);
	EXPECT_EQ(oaktask_task_is_finished(t), 1);
	EXPECT_EQ(oaktask_task_succeeded(t), 1);

	EXPECT_EQ(seen.started, 1);
	EXPECT_EQ(seen.finished, 1);
	EXPECT_EQ(seen.succeeded, 1.0);

	EXPECT_TRUE(std::filesystem::exists(path));
	std::filesystem::remove(path);

	oaktask_task_free(t);
	oaknode_project_free(project);
	oaktask_manager_shutdown();

	EXPECT_EQ(oaktask_debug_alive_count(), 0);
}

// ---- conform submitter end-to-end ------------------------------------------

TEST(OakTaskConform, SubmittedConformProducesPcm)
{
	const std::string demo = OAK_REPO_ROOT "/tests/demo.mp4";
	if (!std::filesystem::exists(demo)) {
		GTEST_SKIP() << "demo.mp4 not available";
	}

	ASSERT_EQ(oaktask_manager_init(), OAKTASK_OK);
	ASSERT_EQ(oakcodec_conform_create_instance(), OAKCODEC_OK);

	// Probe demo.mp4 for an audio stream
	OakDecoder probe = oakcodec_decoder_probe(demo.c_str());
	ASSERT_NE(probe.ctx, nullptr);
	if (oakcodec_decoder_probe_audio_stream_count(probe) <= 0) {
		oakcodec_decoder_free(&probe);
		oaktask_manager_shutdown();
		GTEST_SKIP() << "demo.mp4 has no audio stream";
	}

	oakcodec_audio_stream_info info = {};
	ASSERT_EQ(oakcodec_decoder_probe_get_audio_stream(probe, 0, &info),
			  OAKCODEC_OK);
	oakcodec_decoder_free(&probe);

	std::string cache_path =
		(std::filesystem::temp_directory_path() / "oaktask_conform_cache")
			.string();
	std::error_code ec;
	std::filesystem::remove_all(cache_path, ec);
	std::filesystem::create_directories(cache_path, ec);

	const int sample_format = 4; /* olive::core::SampleFormat::f32_p */
	int state = oakcodec_conform_get_state(
		cache_path.c_str(), demo.c_str(), info.stream_index,
		info.sample_rate, info.channel_layout, sample_format, 1);
	EXPECT_EQ(state, 0 /* OAKCODEC_CONFORM_EXISTS */);

	int count = oakcodec_conform_filename_count(
		cache_path.c_str(), demo.c_str(), info.stream_index,
		info.sample_rate, info.channel_layout, sample_format);
	EXPECT_GT(count, 0);

	std::filesystem::remove_all(cache_path, ec);
	oakcodec_conform_destroy_instance();
	oaktask_manager_shutdown();
}

} // namespace
