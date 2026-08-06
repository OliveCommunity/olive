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

#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "audio/manager.h"

namespace
{

constexpr int kSampleFmtF32 = 10; // olive::core::SampleFormat::f32
constexpr uint64_t kLayoutStereo = 0x3;

// Creates the singleton for the duration of the test; skips when no
// audio device environment is available.
struct ManagerFixture {
	ManagerFixture()
	{
		created = (oakaudio_manager_create_instance() == OAKAUDIO_OK) &&
				  oakaudio_manager_instance().ctx != nullptr;
	}
	~ManagerFixture()
	{
		if (created) {
			oakaudio_manager_destroy_instance();
		}
	}
	bool created = false;
};

} // namespace

TEST(OakAudioManager, InstanceLifecycle)
{
	// Without an instance all calls report E_STATE and instance() is empty
	OakAudioManager none = oakaudio_manager_instance();
	EXPECT_EQ(none.ctx, nullptr);
	EXPECT_EQ(none.abi_version, OAKAUDIO_ABI_VERSION);

	double secs;
	EXPECT_EQ(oakaudio_manager_seconds(none, &secs), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_get_output_device(none), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_get_input_device(none), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_set_output_device(none, 0), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_set_input_device(none, 0), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_set_output_notify_interval(none, 1024),
			  OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_clear_buffered_output(none), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_stop_output(none), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_reset_output_clock(none), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_hard_reset(none), OAKAUDIO_E_STATE);
	EXPECT_EQ(oakaudio_manager_stop_recording(none), OAKAUDIO_E_STATE);

	float samples[2] = { 0.0f, 0.0f };
	EXPECT_EQ(oakaudio_manager_push_to_output(none, 48000, kLayoutStereo,
											kSampleFmtF32,
											reinterpret_cast<char *>(samples),
											sizeof(samples), nullptr, 0),
			  OAKAUDIO_E_STATE);

	oakcodec_encoding_params params;
	std::memset(&params, 0, sizeof(params));
	params.audio_enabled = 1;
	EXPECT_EQ(oakaudio_manager_start_recording(none, &params, nullptr, 0),
			  OAKAUDIO_E_STATE);

	// free is a no-op and safe on NULL/empty
	oakaudio_manager_free(nullptr);
	oakaudio_manager_free(&none);
	EXPECT_EQ(none.ctx, nullptr);
}

TEST(OakAudioManager, DeviceRoundTrip)
{
	ManagerFixture fx;
	if (!fx.created) {
		GTEST_SKIP() << "no PortAudio device environment";
	}

	OakAudioManager m = oakaudio_manager_instance();
	ASSERT_NE(m.ctx, nullptr);
	// Singleton: addref/release never destroy
	m.addref(m.ctx);
	m.release(m.ctx);
	EXPECT_EQ(oakaudio_manager_instance().ctx, m.ctx);

	// Devices: whatever was detected, get/set round-trips
	const int out_device = oakaudio_manager_get_output_device(m);
	EXPECT_EQ(oakaudio_manager_set_output_device(m, out_device), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_manager_get_output_device(m), out_device);

	const int in_device = oakaudio_manager_get_input_device(m);
	EXPECT_EQ(oakaudio_manager_set_input_device(m, in_device), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_manager_get_input_device(m), in_device);

	// Notify interval set/get-free command
	EXPECT_EQ(oakaudio_manager_set_output_notify_interval(m, 4096),
			  OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_manager_set_output_notify_interval(m, -1),
			  OAKAUDIO_E_INVALID);

	// No stream running: seconds() is negative, clock commands are valid
	double secs = 1.0;
	EXPECT_EQ(oakaudio_manager_seconds(m, &secs), OAKAUDIO_OK);
	EXPECT_LT(secs, 0.0);
	EXPECT_EQ(oakaudio_manager_seconds(m, nullptr), OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_manager_reset_output_clock(m), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_manager_clear_buffered_output(m), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_manager_stop_output(m), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_manager_hard_reset(m), OAKAUDIO_OK);
	EXPECT_EQ(oakaudio_manager_stop_recording(m), OAKAUDIO_OK);
}

TEST(OakAudioManager, PushToOutput)
{
	ManagerFixture fx;
	if (!fx.created) {
		GTEST_SKIP() << "no PortAudio device environment";
	}

	OakAudioManager m = oakaudio_manager_instance();
	if (oakaudio_manager_get_output_device(m) < 0) {
		GTEST_SKIP() << "no output device";
	}

	// One second of silence, packed f32 stereo
	std::vector<float> silence(48000 * 2, 0.0f);
	char error[256];
	EXPECT_EQ(oakaudio_manager_push_to_output(
				  m, 48000, kLayoutStereo, kSampleFmtF32,
				  reinterpret_cast<char *>(silence.data()),
				  int64_t(silence.size() * sizeof(float)), error,
				  int(sizeof(error))),
			  OAKAUDIO_OK);

	// Error paths
	EXPECT_EQ(oakaudio_manager_push_to_output(
				  m, 0, kLayoutStereo, kSampleFmtF32,
				  reinterpret_cast<char *>(silence.data()), 16, nullptr, 0),
			  OAKAUDIO_E_INVALID);
	EXPECT_EQ(oakaudio_manager_push_to_output(m, 48000, kLayoutStereo,
											kSampleFmtF32, nullptr, 16,
											nullptr, 0),
			  OAKAUDIO_E_INVALID);

	oakaudio_manager_stop_output(m);
}

TEST(OakAudioManager, StartRecordingErrorPaths)
{
	ManagerFixture fx;
	if (!fx.created) {
		GTEST_SKIP() << "no PortAudio device environment";
	}

	OakAudioManager m = oakaudio_manager_instance();

	// NULL params / audio disabled
	EXPECT_EQ(oakaudio_manager_start_recording(m, nullptr, nullptr, 0),
			  OAKAUDIO_E_INVALID);
	oakcodec_encoding_params params;
	std::memset(&params, 0, sizeof(params));
	EXPECT_EQ(oakaudio_manager_start_recording(m, &params, nullptr, 0),
			  OAKAUDIO_E_INVALID);
}

TEST(OakAudioManager, FindDeviceByName)
{
	ManagerFixture fx;
	if (!fx.created) {
		GTEST_SKIP() << "no PortAudio device environment";
	}

	// A name that matches nothing falls back to the default device (or
	// paNoDevice on device-less systems); either way no crash and a valid
	// index or -1.
	const int out = oakaudio_manager_find_device_by_name_s(
		"definitely-not-a-real-device-name-oakaudio-test", 1);
	EXPECT_GE(out, -1);

	const int cfg = oakaudio_manager_find_config_device_by_name_s(1);
	EXPECT_GE(cfg, -1);

	// Error path: NULL name
	EXPECT_EQ(oakaudio_manager_find_device_by_name_s(nullptr, 1),
			  OAKAUDIO_E_INVALID);
}
