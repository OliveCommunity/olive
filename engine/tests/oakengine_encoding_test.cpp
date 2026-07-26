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

// Pure C ABI test for the liboakengine encoding facade
// (oakengine/encoding.h + oakengine/videoparams.h). Exercises the
// format/codec metadata queries, the image-sequence filename helpers, the
// scaling matrix, the OakEngineEncodingParams handle (getter/setter
// roundtrips, preset file load/save) and the VideoParams static data behind
// the standard combo boxes. No GPU: the export execution path itself is
// covered by oakengine_export_test; here only the error paths of
// render_with_params are touched (no sequence).

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

#include "oakengine/encoding.h"
#include "oakengine/init.h"
#include "oakengine/videoparams.h"

static char g_tmpdir[4096];

static void make_tmpdir(void)
{
#if defined(_WIN32)
	char base[MAX_PATH];
	const DWORD len = GetTempPathA(MAX_PATH, base);
	assert(len > 0 && len < MAX_PATH);
	snprintf(g_tmpdir, sizeof(g_tmpdir), "%soakengine_encoding_test_%lu",
			 base, (unsigned long)GetCurrentProcessId());
	assert(_mkdir(g_tmpdir) == 0);
#else
	strcpy(g_tmpdir, "/tmp/oakengine_encoding_test_XXXXXX");
	assert(mkdtemp(g_tmpdir) != NULL);
#endif
}

static void test_format_metadata(void)
{
	char buf[256];

	assert(oakengine_encoding_format_count() > 0);

	// Matroska
	assert(oakengine_encoding_format_name(OAKENGINE_ENCODING_FORMAT_MATROSKA,
										  buf, sizeof(buf)) > 0);
	assert(strstr(buf, "Matroska") != NULL);
	assert(oakengine_encoding_format_extension(
			   OAKENGINE_ENCODING_FORMAT_MATROSKA, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "mkv") == 0);

	// Invalid format
	assert(oakengine_encoding_format_name(-1, buf, sizeof(buf)) == -1);
	assert(oakengine_encoding_format_extension(9999, buf, sizeof(buf)) == -1);
	assert(oakengine_encoding_format_video_codec_count(-1) == -1);

	// MP4 carries H.264 video and AAC audio
	const int vcount =
		oakengine_encoding_format_video_codec_count(OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO);
	assert(vcount > 0);
	int found_h264 = 0;
	for (int i = 0; i < vcount; i++) {
		if (oakengine_encoding_format_video_codec_at(
				OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO, i) ==
			OAKENGINE_ENCODING_CODEC_H264) {
			found_h264 = 1;
		}
	}
	assert(found_h264);
	assert(oakengine_encoding_format_video_codec_at(
			   OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO, vcount) == -1);

	// WAV is audio-only and carries PCM
	assert(oakengine_encoding_format_video_codec_count(
			   OAKENGINE_ENCODING_FORMAT_WAV) == 0);
	const int acount = oakengine_encoding_format_audio_codec_count(
		OAKENGINE_ENCODING_FORMAT_WAV);
	assert(acount > 0);
	int found_pcm = 0;
	for (int i = 0; i < acount; i++) {
		if (oakengine_encoding_format_audio_codec_at(
				OAKENGINE_ENCODING_FORMAT_WAV, i) == OAKENGINE_ENCODING_CODEC_PCM) {
			found_pcm = 1;
		}
	}
	assert(found_pcm);

	// SRT is subtitles-only
	assert(oakengine_encoding_format_subtitle_codec_count(
			   OAKENGINE_ENCODING_FORMAT_SRT) > 0);
	assert(oakengine_encoding_format_subtitle_codec_at(
			   OAKENGINE_ENCODING_FORMAT_SRT, 0) >= 0);
	assert(oakengine_encoding_format_subtitle_codec_at(
			   OAKENGINE_ENCODING_FORMAT_SRT, -1) < 0);
	assert(oakengine_encoding_format_audio_codec_count(
			   OAKENGINE_ENCODING_FORMAT_SRT) == 0);
}

static void test_codec_metadata(void)
{
	char buf[256];

	assert(oakengine_encoding_codec_name(OAKENGINE_ENCODING_CODEC_H264, buf,
										 sizeof(buf)) > 0);
	assert(buf[0] != '\0');
	assert(oakengine_encoding_codec_name(-1, buf, sizeof(buf)) == -1);

	assert(oakengine_encoding_codec_is_still_image(5 /* PNG */) == 1);
	assert(oakengine_encoding_codec_is_still_image(
			   OAKENGINE_ENCODING_CODEC_H264) == 0);
	assert(oakengine_encoding_codec_is_lossless(OAKENGINE_ENCODING_CODEC_PCM) ==
		   1);
	assert(oakengine_encoding_codec_is_lossless(OAKENGINE_ENCODING_CODEC_AAC) ==
		   0);

	// Encoded pixel formats of H.264 in MP4: yuv420p is the preferred one
	const int pcount = oakengine_encoding_pix_fmt_count(
		OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO, OAKENGINE_ENCODING_CODEC_H264);
	assert(pcount > 0);
	assert(oakengine_encoding_pix_fmt_at(OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO,
										 OAKENGINE_ENCODING_CODEC_H264, 0, buf,
										 sizeof(buf)) > 0);
	assert(strcmp(buf, "yuv420p") == 0);
	assert(oakengine_encoding_pix_fmt_at(OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO,
										 OAKENGINE_ENCODING_CODEC_H264, pcount,
										 buf, sizeof(buf)) == -1);
	assert(oakengine_encoding_pix_fmt_index(OAKENGINE_ENCODING_CODEC_H264,
											"yuv420p") == 0);
	assert(oakengine_encoding_pix_fmt_index(OAKENGINE_ENCODING_CODEC_H264,
											"no-such-format") == 0);
	assert(oakengine_encoding_pix_fmt_index(OAKENGINE_ENCODING_CODEC_H264,
											NULL) == 0);

	// Sample formats of PCM in WAV
	const int scount = oakengine_encoding_sample_format_count(
		OAKENGINE_ENCODING_FORMAT_WAV, OAKENGINE_ENCODING_CODEC_PCM);
	assert(scount > 0);
	for (int i = 0; i < scount; i++) {
		assert(oakengine_encoding_sample_format_at(
				   OAKENGINE_ENCODING_FORMAT_WAV, OAKENGINE_ENCODING_CODEC_PCM,
				   i) >= 0);
	}
	assert(oakengine_encoding_sample_format_at(
			   OAKENGINE_ENCODING_FORMAT_WAV, OAKENGINE_ENCODING_CODEC_PCM,
			   scount) == -1);
}

static void test_filename_helpers(void)
{
	char buf[4096];

	assert(oakengine_encoding_filename_contains_digit_placeholder(
			   "/tmp/out_[#####].png") == 1);
	assert(oakengine_encoding_filename_contains_digit_placeholder(
			   "/tmp/out.png") == 0);
	assert(oakengine_encoding_filename_contains_digit_placeholder(NULL) == 0);

	assert(oakengine_encoding_image_sequence_digit_count(
			   "/tmp/out_[#####].png") == 5);
	assert(oakengine_encoding_image_sequence_digit_count("/tmp/out.png") == 0);

	assert(oakengine_encoding_filename_remove_digit_placeholder(
			   "/tmp/out_[#####].png", buf, sizeof(buf)) > 0);
	assert(strstr(buf, "[#####]") == NULL);
	assert(strstr(buf, ".png") != NULL);
}

static void test_generate_matrix(void)
{
	float m[16];

	// Fit with matching dimensions is the identity
	assert(oakengine_encoding_generate_matrix(OAKENGINE_ENCODING_SCALING_FIT,
											  1920, 1080, 1920, 1080,
											  m) == OAKENGINE_OK);
	const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0,
								 0, 0, 1, 0, 0, 0, 0, 1 };
	for (int i = 0; i < 16; i++) {
		assert(fabsf(m[i] - identity[i]) < 1e-6f);
	}

	// Stretch is the identity transform (the preview is normalized device
	// coordinates; stretching needs no matrix)
	assert(oakengine_encoding_generate_matrix(OAKENGINE_ENCODING_SCALING_STRETCH,
											  960, 540, 1920, 1080,
											  m) == OAKENGINE_OK);
	for (int i = 0; i < 16; i++) {
		assert(fabsf(m[i] - identity[i]) < 1e-6f);
	}

	// Fit into a wider-than-source frame pillarboxes: x scale shrinks to
	// source_ar/export_ar
	assert(oakengine_encoding_generate_matrix(OAKENGINE_ENCODING_SCALING_FIT,
											  1920, 1080, 1920, 540,
											  m) == OAKENGINE_OK);
	const float expected_x = (1920.0f / 1080.0f) / (1920.0f / 540.0f);
	assert(fabsf(m[0] - expected_x) < 1e-5f);
	assert(fabsf(m[5] - 1.0f) < 1e-6f);

	// Invalid arguments
	assert(oakengine_encoding_generate_matrix(-1, 1, 1, 1, 1,
											  m) == OAKENGINE_E_INVALID);
	assert(oakengine_encoding_generate_matrix(OAKENGINE_ENCODING_SCALING_FIT, 0,
											  1, 1, 1,
											  m) == OAKENGINE_E_INVALID);
}

static void test_params_handle(void)
{
	char buf[1024];

	OakEngineEncodingParams *p = oakengine_encoding_params_create();
	assert(p != NULL);

	// Fresh handle: nothing enabled, format unset
	assert(oakengine_encoding_params_is_valid(p) == 0);
	assert(oakengine_encoding_params_format(p) == -1);
	assert(oakengine_encoding_params_video_enabled(p) == 0);
	assert(oakengine_encoding_params_audio_enabled(p) == 0);
	assert(oakengine_encoding_params_subtitles_enabled(p) == 0);
	assert(oakengine_encoding_params_has_custom_range(p) == 0);

	// Filename / format roundtrip
	assert(oakengine_encoding_params_set_filename(p, "/tmp/out.mp4") ==
		   OAKENGINE_OK);
	assert(oakengine_encoding_params_filename(p, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "/tmp/out.mp4") == 0);
	assert(oakengine_encoding_params_set_format(
			   p, OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO) == OAKENGINE_OK);
	assert(oakengine_encoding_params_format(p) ==
		   OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO);
	assert(oakengine_encoding_params_set_format(p, 9999) ==
		   OAKENGINE_E_INVALID);

	// Video roundtrip
	oak_video_params v = {};
	v.width = 1920;
	v.height = 1080;
	v.time_base_num = 1001;
	v.time_base_den = 30000;
	v.format = 8; /* a PixelFormat::Format value */
	v.pixel_aspect_num = 1;
	v.pixel_aspect_den = 1;
	v.interlacing = OAKENGINE_ENCODING_INTERLACE_BOTTOM_FIRST;
	v.color_range = OAKENGINE_ENCODING_COLOR_RANGE_FULL;
	v.divider = 1;
	assert(oakengine_encoding_params_enable_video(
			   p, &v, OAKENGINE_ENCODING_CODEC_H264) == OAKENGINE_OK);
	assert(oakengine_encoding_params_is_valid(p) == 1);
	assert(oakengine_encoding_params_video_enabled(p) == 1);
	assert(oakengine_encoding_params_video_codec(p) ==
		   OAKENGINE_ENCODING_CODEC_H264);
	assert(oakengine_encoding_params_enable_video(p, NULL, 1) ==
		   OAKENGINE_E_INVALID);
	assert(oakengine_encoding_params_enable_video(p, &v, 9999) ==
		   OAKENGINE_E_INVALID);

	oak_video_params back = {};
	assert(oakengine_encoding_params_get_video_params(p, &back) ==
		   OAKENGINE_OK);
	assert(back.width == 1920 && back.height == 1080);
	assert(back.time_base_num == 1001 && back.time_base_den == 30000);
	assert(back.pixel_aspect_num == 1 && back.pixel_aspect_den == 1);
	assert(back.interlacing == OAKENGINE_ENCODING_INTERLACE_BOTTOM_FIRST);
	assert(back.color_range == OAKENGINE_ENCODING_COLOR_RANGE_FULL);

	// Audio roundtrip
	assert(oakengine_encoding_params_enable_audio(p, 48000, 0x3, 4,
												  OAKENGINE_ENCODING_CODEC_AAC) ==
		   OAKENGINE_OK);
	assert(oakengine_encoding_params_audio_enabled(p) == 1);
	assert(oakengine_encoding_params_audio_codec(p) ==
		   OAKENGINE_ENCODING_CODEC_AAC);
	int sample_rate = 0, sample_format = 0;
	uint64_t layout = 0;
	assert(oakengine_encoding_params_get_audio_params(p, &sample_rate, &layout,
													  &sample_format) ==
		   OAKENGINE_OK);
	assert(sample_rate == 48000 && layout == 0x3 && sample_format == 4);
	assert(oakengine_encoding_params_enable_audio(p, 0, 0x3, 4, 0) ==
		   OAKENGINE_E_INVALID);

	// Subtitles (embedded, then sidecar)
	assert(oakengine_encoding_params_enable_subtitles(
			   p, OAKENGINE_ENCODING_CODEC_SRT) == OAKENGINE_OK);
	assert(oakengine_encoding_params_subtitles_enabled(p) == 1);
	assert(oakengine_encoding_params_subtitles_are_sidecar(p) == 0);
	assert(oakengine_encoding_params_subtitles_codec(p) ==
		   OAKENGINE_ENCODING_CODEC_SRT);
	assert(oakengine_encoding_params_enable_sidecar_subtitles(
			   p, OAKENGINE_ENCODING_FORMAT_SRT,
			   OAKENGINE_ENCODING_CODEC_SRT) == OAKENGINE_OK);
	assert(oakengine_encoding_params_subtitles_are_sidecar(p) == 1);
	assert(oakengine_encoding_params_subtitles_sidecar_format(p) ==
		   OAKENGINE_ENCODING_FORMAT_SRT);

	// Scalar setters/getters
	oakengine_encoding_params_set_video_bit_rate(p, 8000000);
	assert(oakengine_encoding_params_video_bit_rate(p) == 8000000);
	oakengine_encoding_params_set_video_min_bit_rate(p, 1000);
	assert(oakengine_encoding_params_video_min_bit_rate(p) == 1000);
	oakengine_encoding_params_set_video_max_bit_rate(p, 16000000);
	assert(oakengine_encoding_params_video_max_bit_rate(p) == 16000000);
	oakengine_encoding_params_set_video_buffer_size(p, 2000000);
	assert(oakengine_encoding_params_video_buffer_size(p) == 2000000);
	oakengine_encoding_params_set_video_threads(p, 4);
	assert(oakengine_encoding_params_video_threads(p) == 4);
	oakengine_encoding_params_set_audio_bit_rate(p, 320000);
	assert(oakengine_encoding_params_audio_bit_rate(p) == 320000);

	assert(oakengine_encoding_params_set_video_pix_fmt(p, "yuv420p") ==
		   OAKENGINE_OK);
	assert(oakengine_encoding_params_video_pix_fmt(p, buf, sizeof(buf)) > 0);
	assert(strcmp(buf, "yuv420p") == 0);

	oakengine_encoding_params_set_video_is_image_sequence(p, 1);
	assert(oakengine_encoding_params_video_is_image_sequence(p) == 1);
	oakengine_encoding_params_set_video_is_image_sequence(p, 0);
	assert(oakengine_encoding_params_video_is_image_sequence(p) == 0);

	assert(oakengine_encoding_params_set_color_transform(p, "sRGB OETF") ==
		   OAKENGINE_OK);
	assert(oakengine_encoding_params_color_transform_output(p, buf,
															sizeof(buf)) > 0);
	assert(strcmp(buf, "sRGB OETF") == 0);

	oakengine_encoding_params_set_export_length(p, 10, 1);
	int num = 0, den = 0;
	assert(oakengine_encoding_params_get_export_length(p, &num, &den) ==
		   OAKENGINE_OK);
	assert(num == 10 && den == 1);

	// Custom range
	oakengine_encoding_params_set_custom_range(p, 1, 1, 5, 1);
	assert(oakengine_encoding_params_has_custom_range(p) == 1);
	int64_t in_num = 0, in_den = 0, out_num = 0, out_den = 0;
	assert(oakengine_encoding_params_get_custom_range(p, &in_num, &in_den,
													  &out_num,
													  &out_den) == OAKENGINE_OK);
	assert(in_num == 1 && in_den == 1 && out_num == 5 && out_den == 1);

	// Scaling method
	assert(oakengine_encoding_params_set_video_scaling_method(
			   p, OAKENGINE_ENCODING_SCALING_CROP) == OAKENGINE_OK);
	assert(oakengine_encoding_params_video_scaling_method(p) ==
		   OAKENGINE_ENCODING_SCALING_CROP);
	assert(oakengine_encoding_params_set_video_scaling_method(p, 42) ==
		   OAKENGINE_E_INVALID);

	// Video options
	assert(oakengine_encoding_params_video_option(p, "crf", buf,
												  sizeof(buf)) ==
		   OAKENGINE_E_NOT_FOUND);
	assert(oakengine_encoding_params_set_video_option(p, "crf", "18") ==
		   OAKENGINE_OK);
	assert(oakengine_encoding_params_video_option(p, "crf", buf,
												  sizeof(buf)) > 0);
	assert(strcmp(buf, "18") == 0);

	// Disables
	oakengine_encoding_params_disable_subtitles(p);
	assert(oakengine_encoding_params_subtitles_enabled(p) == 0);
	oakengine_encoding_params_disable_video(p);
	assert(oakengine_encoding_params_video_enabled(p) == 0);
	assert(oakengine_encoding_params_get_video_params(p, &back) ==
		   OAKENGINE_E_STATE);
	oakengine_encoding_params_disable_audio(p);
	assert(oakengine_encoding_params_audio_enabled(p) == 0);

	// NULL safety
	oakengine_encoding_params_destroy(NULL);
	assert(oakengine_encoding_params_is_valid(NULL) == 0);

	oakengine_encoding_params_destroy(p);
}

static void test_preset_files(void)
{
	char buf[1024];

	// Preset directory listing is readable (may be empty in the sandbox)
	assert(oakengine_encoding_preset_path(buf, sizeof(buf)) > 0);
	const int count = oakengine_encoding_preset_count();
	assert(count >= 0);
	for (int i = 0; i < count; i++) {
		assert(oakengine_encoding_preset_name(i, buf, sizeof(buf)) > 0);
	}
	assert(oakengine_encoding_preset_name(count, buf, sizeof(buf)) == -1);

	// Save/load roundtrip through a temp file
	char path[4096];
	snprintf(path, sizeof(path), "%s/preset.xml", g_tmpdir);

	OakEngineEncodingParams *p = oakengine_encoding_params_create();
	assert(oakengine_encoding_params_set_filename(p, "/tmp/out.mp4") ==
		   OAKENGINE_OK);
	assert(oakengine_encoding_params_set_format(
			   p, OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO) == OAKENGINE_OK);
	oak_video_params v = {};
	v.width = 1280;
	v.height = 720;
	v.time_base_num = 1;
	v.time_base_den = 25;
	v.format = 8;
	v.pixel_aspect_num = 1;
	v.pixel_aspect_den = 1;
	v.interlacing = OAKENGINE_ENCODING_INTERLACE_NONE;
	v.color_range = OAKENGINE_ENCODING_COLOR_RANGE_LIMITED;
	v.divider = 1;
	assert(oakengine_encoding_params_enable_video(
			   p, &v, OAKENGINE_ENCODING_CODEC_H264) == OAKENGINE_OK);
	assert(oakengine_encoding_params_set_video_option(p, "crf", "20") ==
		   OAKENGINE_OK);
	assert(oakengine_encoding_params_save_file(p, path) == OAKENGINE_OK);
	oakengine_encoding_params_destroy(p);

	OakEngineEncodingParams *q = oakengine_encoding_params_create();
	assert(oakengine_encoding_params_load_file(q, path) == OAKENGINE_OK);
	assert(oakengine_encoding_params_video_enabled(q) == 1);
	assert(oakengine_encoding_params_video_codec(q) ==
		   OAKENGINE_ENCODING_CODEC_H264);
	oak_video_params back = {};
	assert(oakengine_encoding_params_get_video_params(q, &back) ==
		   OAKENGINE_OK);
	assert(back.width == 1280 && back.height == 720);
	assert(back.time_base_num == 1 && back.time_base_den == 25);
	assert(oakengine_encoding_params_video_option(q, "crf", buf,
												  sizeof(buf)) > 0);
	assert(strcmp(buf, "20") == 0);
	oakengine_encoding_params_destroy(q);

	// Loading a nonexistent file fails
	OakEngineEncodingParams *r = oakengine_encoding_params_create();
	assert(oakengine_encoding_params_load_file(r, "/no/such/file.xml") ==
		   OAKENGINE_E_FAILED);
	oakengine_encoding_params_destroy(r);

	// Bad arguments
	assert(oakengine_encoding_params_save_file(NULL, path) ==
		   OAKENGINE_E_INVALID);
}

static void test_last_used_and_render_errors(void)
{
	// NULL sequence: no last-used params, no-op setter
	assert(oakengine_encoding_params_get_last_used(NULL) == NULL);
	oakengine_encoding_params_set_last_used(NULL, NULL);

	// render_with_params without a valid sequence/params fails cleanly
	assert(oakengine_export_render_with_params(NULL, NULL) ==
		   OAKENGINE_E_INVALID);
	OakEngineEncodingParams *p = oakengine_encoding_params_create();
	assert(oakengine_export_render_with_params(NULL, p) ==
		   OAKENGINE_E_INVALID);
	// Nothing enabled on the handle
	assert(oakengine_export_render_with_params(
			   reinterpret_cast<OakEngineSequence *>(p), p) ==
		   OAKENGINE_E_INVALID);
	oakengine_encoding_params_destroy(p);

	// Audio recording requires an enabled audio track on the handle
	assert(oakengine_encoding_start_audio_recording(NULL, NULL, 0) ==
		   OAKENGINE_E_INVALID);
}

static void test_video_params_statics(void)
{
	char buf[256];
	int num = 0, den = 0;

	// Standard frame rates
	const int fr_count = oakengine_video_params_supported_frame_rate_count();
	assert(fr_count > 0);
	for (int i = 0; i < fr_count; i++) {
		assert(oakengine_video_params_supported_frame_rate_at(i, &num, &den) ==
			   OAKENGINE_OK);
		assert(num > 0 && den > 0);
	}
	assert(oakengine_video_params_supported_frame_rate_at(fr_count, &num,
														  &den) ==
		   OAKENGINE_E_INVALID);

	// 24000/1001 prints as 23.976...
	assert(oakengine_video_params_frame_rate_to_string(24000, 1001, buf,
													   sizeof(buf)) > 0);
	assert(strstr(buf, "23.97") != NULL);

	// Standard pixel aspects: the first one is square (1:1)
	const int pa_count = oakengine_video_params_standard_pixel_aspect_count();
	assert(pa_count > 0);
	assert(oakengine_video_params_standard_pixel_aspect_at(0, &num, &den) ==
		   OAKENGINE_OK);
	assert(num == 1 && den == 1);
	assert(oakengine_video_params_standard_pixel_aspect_name(0, buf,
															 sizeof(buf)) > 0);
	assert(buf[0] != '\0');
	assert(oakengine_video_params_standard_pixel_aspect_at(pa_count, &num,
														   &den) ==
		   OAKENGINE_E_INVALID);

	// Custom PAR label template
	assert(oakengine_video_params_format_pixel_aspect_ratio_string(
			   "Custom (%1)", 32, 27, buf, sizeof(buf)) > 0);
	assert(strstr(buf, "Custom") != NULL);

	// Dividers
	const int div_count = oakengine_video_params_supported_divider_count();
	assert(div_count > 0);
	for (int i = 0; i < div_count; i++) {
		const int d = oakengine_video_params_supported_divider_at(i);
		assert(d > 0);
		assert(oakengine_video_params_divider_name(d, buf, sizeof(buf)) > 0);
	}
	assert(oakengine_video_params_supported_divider_at(div_count) == -1);

	// Pixel format names: some entry must be non-empty
	assert(oakengine_video_params_pixel_format_name(8, buf, sizeof(buf)) > 0);

	// Float detection (8-bit integer formats are not float)
	assert(oakengine_video_params_format_is_float(0) == 0);

	// Effective (divider-scaled) size
	int w = 0, h = 0;
	assert(oakengine_video_params_effective_size(1920, 1080, 2, &w, &h) ==
		   OAKENGINE_OK);
	assert(w == 960 && h == 540);
	assert(oakengine_video_params_effective_size(0, 1080, 2, &w, &h) ==
		   OAKENGINE_E_INVALID);
}

// POD mirror of the display-path VideoParams (B7): make/equal/is_valid,
// bytes-per-pixel and the internal channel count.
static void test_video_params_pod(void)
{
	oak_video_params p, q;

	// make fills every field
	assert(oakengine_video_params_make(&p, 1920, 1080, 1001, 30000, 0, 1, 1,
									   0, 0, 2) == OAKENGINE_OK);
	assert(p.width == 1920 && p.height == 1080);
	assert(p.time_base_num == 1001 && p.time_base_den == 30000);
	assert(p.pixel_aspect_num == 1 && p.pixel_aspect_den == 1);
	assert(p.divider == 2);
	assert(oakengine_video_params_make(NULL, 1, 1, 1, 1, 0, 1, 1, 0, 0, 1) ==
		   OAKENGINE_E_INVALID);

	// equal: identical PODs match, any single-field difference does not
	q = p;
	assert(oakengine_video_params_equal(&p, &q) == 1);
	assert(oakengine_video_params_equal(&p, NULL) == 0);
	assert(oakengine_video_params_equal(NULL, &q) == 0);
	q.divider = 1;
	assert(oakengine_video_params_equal(&p, &q) == 0);
	q = p;
	q.interlacing = 1;
	assert(oakengine_video_params_equal(&p, &q) == 0);

	// is_valid: positive dimensions + in-range format passes; zero
	// dimensions or an out-of-range format fail
	assert(oakengine_video_params_is_valid(&p) == 1);
	assert(oakengine_video_params_is_valid(NULL) == 0);
	q = p;
	q.width = 0;
	assert(oakengine_video_params_is_valid(&q) == 0);
	q = p;
	q.format = -1; // olive::PixelFormat::invalid
	assert(oakengine_video_params_is_valid(&q) == 0);

	// bytes per pixel: u8 RGBA = 4, f32 RGBA = 16 (format values follow
	// olive::PixelFormat::Format: 0 = u8, 4 = f32)
	const int channels = oakengine_video_params_internal_channel_count();
	assert(channels == 4);
	assert(oakengine_video_params_bytes_per_pixel(0, channels) == 4);
	assert(oakengine_video_params_bytes_per_pixel(4, channels) == 16);
}

// Engine-side VideoParams construction used by the app during R6 to avoid
// pulling C++ constructors into oak-editor.
static void test_video_params_create_free(void)
{
	// NULL pod -> NULL handle
	assert(oakengine_video_params_create(NULL) == NULL);

	// Empty POD -> default-constructed VideoParams handle
	oak_video_params empty = {};
	void *vp_empty = oakengine_video_params_create(&empty);
	assert(vp_empty != NULL);
	oakengine_video_params_free(vp_empty);

	// Display-path POD with explicit timebase
	oak_video_params pod;
	assert(oakengine_video_params_make(&pod, 1920, 1080, 1001, 30000, 0, 1, 1,
									   0, 0, 1) == OAKENGINE_OK);
	void *vp = oakengine_video_params_create(&pod);
	assert(vp != NULL);
	oakengine_video_params_free(vp);

	// Display-path POD without timebase (uses constructor without timebase)
	oak_video_params pod2 = {};
	pod2.width = 640;
	pod2.height = 480;
	pod2.format = 0; // u8
	void *vp2 = oakengine_video_params_create(&pod2);
	assert(vp2 != NULL);
	oakengine_video_params_free(vp2);

	// free(NULL) is a no-op
	oakengine_video_params_free(NULL);
}

int main(void)
{
	make_tmpdir();

	// HEADLESS: no GL, but a QCoreApplication (needed by the FFmpeg encoder
	// probes and QStandardPaths behind the metadata queries) comes up.
	assert(oakengine_init(OAKENGINE_INIT_HEADLESS) == OAKENGINE_OK);

	test_format_metadata();
	test_codec_metadata();
	test_filename_helpers();
	test_generate_matrix();
	test_params_handle();
	test_preset_files();
	test_last_used_and_render_errors();
	test_video_params_statics();
	test_video_params_create_free();

	oakengine_shutdown();

	printf("oakengine_encoding_test: OK\n");
	return 0;
}
