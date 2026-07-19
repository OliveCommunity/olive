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

// oak-cli: headless command-line consumer of the liboakengine C ABI facade.
//
// This binary is a pure C ABI consumer: it includes ONLY the public
// oakengine/*.h C headers (no engine C++ headers, no Qt headers) and links
// only against liboakengine. Everything it does goes through the facade:
//
//   oak-cli info <project.ove>
//       Print the project name, its sequences (name, length, frame rate,
//       track counts, playhead) and its footage (filename, online/offline).
//
//   oak-cli render <project.ove> <start_seconds> <end_seconds> <out_dir>
//       Render the first sequence frame by frame at the sequence frame rate
//       to PPM (P6) images plus the whole audio range to a PCM s16 WAV.
//
// Exit codes:
//   0   success
//   1   general error (bad project file, no sequence, I/O failure)
//   2   rendering unavailable or failed (e.g. no GL render backend); ctest
//       treats this as SKIP via SKIP_RETURN_CODE
//   64  usage error (wrong arguments)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "oakengine/exporter.h"
#include "oakengine/footage.h"
#include "oakengine/init.h"
#include "oakengine/project.h"
#include "oakengine/renderer.h"
#include "oakengine/timeline.h"

namespace
{

constexpr int k_exit_ok = 0;
constexpr int k_exit_error = 1;
constexpr int k_exit_render_unavailable = 2;
constexpr int k_exit_usage = 64;

constexpr int k_pixel_format_f32 = 4; // olive::core::PixelFormat::f32

void print_usage(FILE *out)
{
	fprintf(out,
			"oak-cli - headless consumer of the liboakengine C ABI\n"
			"\n"
			"Usage:\n"
			"  oak-cli info <project.ove>\n"
			"      Print project name, sequences and footage.\n"
			"\n"
			"  oak-cli render <project.ove> <start_seconds> <end_seconds> <out_dir>\n"
			"      Render the first sequence to PPM frames (P6, 8-bit RGB) and the\n"
			"      audio range to a PCM s16 WAV file in <out_dir>.\n"
			"\n"
			"  oak-cli probe <mediafile>\n"
			"      Probe a media file: decoder, duration, video and audio streams.\n"
			"\n"
			"  oak-cli transcode <input_media> <out> [width] [--format ppm|mp4]\n"
			"      Transcode a media file end to end: import it into a temporary\n"
			"      project, place it as clips, and render the whole duration.\n"
			"      Default output is a single H.264/AAC MP4 file (encoder default\n"
			"      bit rate); --format ppm renders PPM frames + a WAV instead.\n"
			"      [width] defaults to the source width; the height follows the\n"
			"      source aspect ratio. <out> is the MP4 file path, or the\n"
			"      output directory with --format ppm.\n"
			"\n"
			"  oak-cli --help\n"
			"      Show this text.\n"
			"\n"
			"Exit codes:\n"
			"  0   success\n"
			"  1   general error (bad project/media file, no sequence, I/O failure)\n"
			"  2   rendering unavailable or failed (e.g. no GL render backend)\n"
			"  64  usage error\n");
}

// Read a facade string (buf/size convention) into a std::string.
std::string facade_string(int (*getter)(const void *, char *, int),
						  const void *handle)
{
	const int size = getter(handle, nullptr, 0);
	if (size < 0) {
		return std::string();
	}
	std::string s(size_t(size) + 1, '\0');
	getter(handle, s.data(), size + 1);
	s.resize(size_t(size));
	return s;
}

int project_name_adapter(const void *handle, char *buf, int size)
{
	return oakengine_project_name(
		reinterpret_cast<const OakEngineProject *>(handle), buf, size);
}

int sequence_name_adapter(const void *handle, char *buf, int size)
{
	return oakengine_sequence_name(
		reinterpret_cast<const OakEngineSequence *>(handle), buf, size);
}

void print_sequence(OakEngineSequence *seq, int index)
{
	const std::string name =
		facade_string(sequence_name_adapter, reinterpret_cast<void *>(seq));

	double seconds = 0.0;
	oakengine_sequence_get_length(seq, &seconds);
	int len_num = 0, len_den = 0;
	oakengine_sequence_get_length_rational(seq, &len_num, &len_den);
	int fr_num = 0, fr_den = 0;
	oakengine_sequence_get_frame_rate(seq, &fr_num, &fr_den);
	int video = 0, audio = 0, subtitle = 0;
	oakengine_sequence_track_count(seq, &video, &audio, &subtitle);
	int64_t playhead = 0;
	double playhead_seconds = 0.0;
	oakengine_sequence_get_playhead(seq, &playhead);
	oakengine_sequence_get_playhead_seconds(seq, &playhead_seconds);

	printf("  [%d] \"%s\"\n", index, name.c_str());
	printf("      length: %.6f s (%d/%d)\n", seconds, len_num, len_den);
	printf("      frame rate: %d/%d (%.3f fps)\n", fr_num, fr_den,
		   fr_den ? double(fr_num) / double(fr_den) : 0.0);
	printf("      tracks: video=%d audio=%d subtitle=%d\n", video, audio,
		   subtitle);
	printf("      playhead: %lld (%.6f s)\n", (long long)playhead,
		   playhead_seconds);
}

int cmd_info(const char *path)
{
	if (oakengine_init(OAKENGINE_INIT_HEADLESS) != OAKENGINE_OK) {
		fprintf(stderr, "error: failed to initialize the engine\n");
		return k_exit_error;
	}

	int rc = k_exit_ok;
	OakEngineProject *project = oakengine_project_create();
	char err[1024];
	if (oakengine_project_load(project, path, err, sizeof(err)) !=
		OAKENGINE_OK) {
		fprintf(stderr, "error: failed to load \"%s\": %s\n", path, err);
		rc = k_exit_error;
	} else {
		const std::string name =
			facade_string(project_name_adapter, reinterpret_cast<void *>(project));
		char filename[4096];
		oakengine_project_filename(project, filename, sizeof(filename));

		printf("Project: %s\n", name.c_str());
		printf("File: %s\n", filename);
		printf("Modified: %s\n",
			   oakengine_project_is_modified(project) ? "yes" : "no");

		const int sequences = oakengine_project_sequence_count(project);
		printf("Sequences: %d\n", sequences);
		for (int i = 0; i < sequences; i++) {
			print_sequence(oakengine_project_sequence_at(project, i), i);
		}

		const int footage = oakengine_project_footage_count(project);
		printf("Footage: %d\n", footage);
		for (int i = 0; i < footage; i++) {
			const int size =
				oakengine_project_footage_filename(project, i, nullptr, 0);
			std::string fn(size_t(size > 0 ? size : 0), '\0');
			if (size > 0) {
				oakengine_project_footage_filename(project, i, fn.data(),
												   size + 1);
			}
			const int online =
				oakengine_project_footage_is_online(project, i);
			printf("  [%d] \"%s\" %s\n", i, fn.c_str(),
				   online == 1 ? "online" : "offline");
		}
	}

	oakengine_project_free(project);
	oakengine_shutdown();
	return rc;
}

// f32 RGBA -> 8-bit RGB triple, clamped.
void write_ppm(const OakEngineFrame *frame, const std::string &path)
{
	const int width = oakengine_frame_width(frame);
	const int height = oakengine_frame_height(frame);
	const int format = oakengine_frame_format(frame);
	const int channels = oakengine_frame_channel_count(frame);
	const int linesize = oakengine_frame_linesize_bytes(frame);
	const char *data =
		reinterpret_cast<const char *>(oakengine_frame_data(frame));

	FILE *f = fopen(path.c_str(), "wb");
	if (!f) {
		throw std::string("cannot open \"" + path + "\" for writing");
	}
	fprintf(f, "P6\n%d %d\n255\n", width, height);

	std::vector<unsigned char> row(size_t(width) * 3);
	for (int y = 0; y < height; y++) {
		const char *line = data + ptrdiff_t(y) * linesize;
		for (int x = 0; x < width; x++) {
			for (int c = 0; c < 3; c++) {
				unsigned char v = 0;
				if (format == k_pixel_format_f32) {
					// f32: 4 bytes per channel
					const float *px = reinterpret_cast<const float *>(line) +
									  ptrdiff_t(x) * channels;
					const float clamped = px[c] < 0.0f ? 0.0f :
										  px[c] > 1.0f ? 1.0f :
														 px[c];
					v = static_cast<unsigned char>(clamped * 255.0f + 0.5f);
				} else if (format == 0) {
					// u8: 1 byte per channel
					v = static_cast<unsigned char>(
						line[ptrdiff_t(x) * channels + c]);
				} else {
					fclose(f);
					throw std::string("unsupported frame pixel format " +
									  std::to_string(format));
				}
				row[size_t(x) * 3 + c] = v;
			}
		}
		fwrite(row.data(), 1, row.size(), f);
	}
	fclose(f);
}

void write_u16_le(FILE *f, uint16_t v)
{
	fputc(v & 0xFF, f);
	fputc((v >> 8) & 0xFF, f);
}

void write_u32_le(FILE *f, uint32_t v)
{
	write_u16_le(f, uint16_t(v & 0xFFFF));
	write_u16_le(f, uint16_t(v >> 16));
}

// Planar float samples -> interleaved PCM s16 WAV.
void write_wav(const OakEngineAudioBuffer *audio, const std::string &path)
{
	const int rate = oakengine_audio_sample_rate(audio);
	const int channels = oakengine_audio_channel_count(audio);
	const int64_t samples = oakengine_audio_sample_count(audio);

	FILE *f = fopen(path.c_str(), "wb");
	if (!f) {
		throw std::string("cannot open \"" + path + "\" for writing");
	}

	const uint32_t data_size = uint32_t(samples) * uint32_t(channels) * 2;
	fwrite("RIFF", 1, 4, f);
	write_u32_le(f, 36 + data_size);
	fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f);
	write_u32_le(f, 16); // fmt chunk size
	write_u16_le(f, 1); // PCM
	write_u16_le(f, uint16_t(channels));
	write_u32_le(f, uint32_t(rate));
	write_u32_le(f, uint32_t(rate * channels * 2)); // byte rate
	write_u16_le(f, uint16_t(channels * 2)); // block align
	write_u16_le(f, 16); // bits per sample
	fwrite("data", 1, 4, f);
	write_u32_le(f, data_size);

	for (int64_t i = 0; i < samples; i++) {
		for (int ch = 0; ch < channels; ch++) {
			const float *channel_data = oakengine_audio_data(audio, ch);
			const float v = channel_data[i];
			const float clamped = v < -1.0f ? -1.0f : v > 1.0f ? 1.0f : v;
			const int16_t s = static_cast<int16_t>(clamped * 32767.0f);
			write_u16_le(f, uint16_t(s));
		}
	}
	fclose(f);
}

int renderer_fail(OakEngineRenderer *renderer, const char *what)
{
	char err[1024];
	if (oakengine_renderer_last_error(renderer, err, sizeof(err)) > 0) {
		fprintf(stderr, "error: %s failed: %s\n", what, err);
	} else {
		fprintf(stderr, "error: %s failed\n", what);
	}
	return k_exit_render_unavailable;
}

// Render frames [start_ts, end_ts) to PPM files in out_dir, with per-frame
// progress on stderr. Shared by the render and transcode commands.
int render_frames_to_ppm(OakEngineRenderer *renderer, const char *out_dir,
						 int64_t start_ts, int64_t end_ts)
{
	const int64_t frame_count = end_ts - start_ts;
	int rc = k_exit_ok;
	try {
		for (int64_t ts = start_ts; ts < end_ts; ts++) {
			OakEngineFrame *frame =
				oakengine_renderer_render_frame(renderer, ts);
			if (!frame) {
				rc = renderer_fail(renderer, "render_frame");
				break;
			}
			fprintf(stderr, "frame %lld/%lld (ts=%lld)\n",
					(long long)(ts - start_ts + 1), (long long)frame_count,
					(long long)ts);

			char name[64];
			snprintf(name, sizeof(name), "frame-%04lld.ppm",
					 (long long)(ts - start_ts));
			const std::filesystem::path ppm_path =
				std::filesystem::path(out_dir) / name;
			write_ppm(frame, ppm_path.string());
			oakengine_frame_free(frame);
		}
	} catch (const std::string &e) {
		fprintf(stderr, "error: %s\n", e.c_str());
		if (rc == k_exit_ok) {
			rc = k_exit_error;
		}
	}
	return rc;
}

// Render audio [start_ts, start_ts+length_ts) to audio.wav in out_dir.
// Shared by the render and transcode commands.
int render_audio_to_wav(OakEngineRenderer *renderer, const char *out_dir,
						int64_t start_ts, int64_t length_ts)
{
	OakEngineAudioBuffer *audio =
		oakengine_renderer_render_audio(renderer, start_ts, length_ts);
	if (!audio) {
		return renderer_fail(renderer, "render_audio");
	}
	int rc = k_exit_ok;
	try {
		write_wav(audio,
				  (std::filesystem::path(out_dir) / "audio.wav").string());
	} catch (const std::string &e) {
		fprintf(stderr, "error: %s\n", e.c_str());
		rc = k_exit_error;
	}
	oakengine_audio_free(audio);
	return rc;
}

int cmd_render(const char *path, const char *start_str, const char *end_str,
			   const char *out_dir)
{
	char *end = nullptr;
	const double start_seconds = std::strtod(start_str, &end);
	if (end == start_str || *end != '\0') {
		fprintf(stderr, "error: invalid start seconds \"%s\"\n", start_str);
		return k_exit_usage;
	}
	const double end_seconds = std::strtod(end_str, &end);
	if (end == end_str || *end != '\0' || end_seconds <= start_seconds) {
		fprintf(stderr, "error: invalid end seconds \"%s\"\n", end_str);
		return k_exit_usage;
	}

	if (oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) !=
		OAKENGINE_OK) {
		fprintf(stderr, "error: failed to initialize the engine\n");
		return k_exit_error;
	}

	int rc = k_exit_ok;
	OakEngineProject *project = nullptr;
	OakEngineRenderer *renderer = nullptr;
	do {
		// Footage paths are stored relative to the project file, and the
		// engine probes/decodes them against the process cwd (both when the
		// project loads and when audio renders in-process), so resolve from
		// the project's directory -- the same rule as
		// oakengine_project_footage_is_online(). The project path itself is
		// made absolute first so it survives the chdir.
		std::error_code ec;
		const std::filesystem::path abs_project =
			std::filesystem::absolute(std::filesystem::path(path), ec);
		if (ec) {
			fprintf(stderr, "error: cannot resolve \"%s\": %s\n", path,
					ec.message().c_str());
			rc = k_exit_error;
			break;
		}
		const std::filesystem::path project_dir = abs_project.parent_path();
		if (!project_dir.empty()) {
			std::filesystem::current_path(project_dir, ec);
		}

		project = oakengine_project_create();
		char err[1024];
		if (oakengine_project_load(project, abs_project.string().c_str(), err,
								   sizeof(err)) != OAKENGINE_OK) {
			fprintf(stderr, "error: failed to load \"%s\": %s\n", path, err);
			rc = k_exit_error;
			break;
		}

		if (oakengine_project_sequence_count(project) < 1) {
			fprintf(stderr, "error: project has no sequence to render\n");
			rc = k_exit_error;
			break;
		}
		OakEngineSequence *seq = oakengine_project_sequence_at(project, 0);

		int fr_num = 0, fr_den = 0;
		if (oakengine_sequence_get_frame_rate(seq, &fr_num, &fr_den) !=
				OAKENGINE_OK ||
			fr_num <= 0 || fr_den <= 0) {
			fprintf(stderr, "error: sequence has no valid frame rate\n");
			rc = k_exit_error;
			break;
		}
		const double fps = double(fr_num) / double(fr_den);
		const int64_t start_ts = std::llround(start_seconds * fps);
		const int64_t end_ts = std::llround(end_seconds * fps);
		const int64_t frame_count = end_ts - start_ts;

		std::filesystem::create_directories(out_dir, ec);
		if (ec) {
			fprintf(stderr, "error: cannot create output directory \"%s\": %s\n",
					out_dir, ec.message().c_str());
			rc = k_exit_error;
			break;
		}

		int width = 0, height = 0;
		if (oakengine_sequence_get_video_params(seq, &width, &height, nullptr,
												nullptr) != OAKENGINE_OK ||
			width <= 0 || height <= 0) {
			fprintf(stderr, "error: sequence has no valid video dimensions\n");
			rc = k_exit_error;
			break;
		}

		renderer = oakengine_renderer_create(seq, width, height,
											 k_pixel_format_f32, fr_num, fr_den,
											 nullptr);
		if (!renderer) {
			fprintf(stderr, "error: failed to create renderer\n");
			rc = k_exit_error;
			break;
		}

		rc = render_frames_to_ppm(renderer, out_dir, start_ts, end_ts);

		if (rc == k_exit_ok) {
			rc = render_audio_to_wav(renderer, out_dir, start_ts, frame_count);
		}

		if (rc == k_exit_ok) {
			printf("wrote %lld PPM frame(s) and audio.wav to \"%s\"\n",
				   (long long)frame_count, out_dir);
		}
	} while (false);

	oakengine_renderer_free(renderer);
	oakengine_project_free(project);
	oakengine_shutdown();
	return rc;
}

int cmd_probe(const char *path)
{
	if (oakengine_init(OAKENGINE_INIT_HEADLESS) != OAKENGINE_OK) {
		fprintf(stderr, "error: failed to initialize the engine\n");
		return k_exit_error;
	}

	int rc = k_exit_ok;
	OakEngineFootage *f = oakengine_footage_probe(path);
	if (!f) {
		char err[1024];
		fprintf(stderr, "error: %s\n",
				oakengine_footage_last_error(err, sizeof(err)) > 0 ?
					err :
					"probe failed");
		rc = k_exit_error;
	} else {
		char decoder[64];
		oakengine_footage_get_decoder_name(f, decoder, sizeof(decoder));
		double duration = 0.0;
		oakengine_footage_get_duration(f, &duration);
		printf("Decoder: %s\n", decoder);
		printf("Duration: %.6f s\n", duration);

		const int videos = oakengine_footage_get_video_stream_count(f);
		printf("Video streams: %d\n", videos);
		for (int i = 0; i < videos; i++) {
			oak_footage_video_info vi;
			if (oakengine_footage_get_video_stream_info(f, i, &vi) !=
				OAKENGINE_OK) {
				continue;
			}
			const double secs = vi.time_base_den ?
				double(vi.duration_ts) * vi.time_base_num / vi.time_base_den :
				0.0;
			const double fps = vi.frame_rate_den ?
				double(vi.frame_rate_num) / vi.frame_rate_den :
				0.0;
			printf("  [%d] stream %d: %dx%d, %d/%d fps (%.3f), duration "
				   "%lld/%d (%f s), primaries=%d trc=%d, %s\n",
				   i, vi.stream_index, vi.width, vi.height,
				   vi.frame_rate_num, vi.frame_rate_den, fps,
				   (long long)vi.duration_ts, vi.time_base_den, secs,
				   vi.color_primaries, vi.color_trc,
				   vi.interlaced ? "interlaced" : "progressive");
		}

		const int audios = oakengine_footage_get_audio_stream_count(f);
		printf("Audio streams: %d\n", audios);
		for (int i = 0; i < audios; i++) {
			oak_footage_audio_info ai;
			if (oakengine_footage_get_audio_stream_info(f, i, &ai) !=
				OAKENGINE_OK) {
				continue;
			}
			const double secs = ai.time_base_den ?
				double(ai.duration_ts) * ai.time_base_num / ai.time_base_den :
				0.0;
			printf("  [%d] stream %d: %d Hz, %d channels, duration %lld/%d "
				   "(%f s)\n",
				   i, ai.stream_index, ai.sample_rate, ai.channel_count,
				   (long long)ai.duration_ts, ai.time_base_den, secs);
		}

		printf("Subtitle streams: %d\n",
			   oakengine_footage_get_subtitle_stream_count(f));
		oakengine_footage_free(f);
	}

	oakengine_shutdown();
	return rc;
}

// Progress printer for mp4 transcodes: percentage steps of 5% on stderr.
static int g_transcode_progress_shown = -1;

void transcode_progress(double fraction, void *userdata)
{
	(void)userdata;
	const int percent = int(fraction * 100.0);
	if (percent / 5 > g_transcode_progress_shown) {
		g_transcode_progress_shown = percent / 5;
		fprintf(stderr, "export progress: %d%%\n", percent);
	}
}

// "Media in, renders out" round trip: probe the source, build a temporary
// project with the whole media placed as clips, and render it out -- as a
// single encoded MP4 by default, or as PPM frames + WAV with --format ppm.
int cmd_transcode(const char *input, const char *out, int argc,
				  char *argv[], int first_opt)
{
	int width = 0;
	std::string format = "mp4";
	for (int i = first_opt; i < argc; i++) {
		if (std::strcmp(argv[i], "--format") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "error: --format needs a value (ppm|mp4)\n");
				return k_exit_usage;
			}
			format = argv[++i];
			if (format != "ppm" && format != "mp4") {
				fprintf(stderr, "error: unknown --format \"%s\" (ppm|mp4)\n",
						format.c_str());
				return k_exit_usage;
			}
		} else if (width == 0) {
			char *end = nullptr;
			const long parsed = std::strtol(argv[i], &end, 10);
			if (end == argv[i] || *end != '\0' || parsed <= 0) {
				fprintf(stderr, "error: invalid width \"%s\"\n", argv[i]);
				return k_exit_usage;
			}
			width = int(parsed);
		} else {
			fprintf(stderr, "error: unexpected argument \"%s\"\n", argv[i]);
			return k_exit_usage;
		}
	}

	if (oakengine_init(OAKENGINE_INIT_HEADLESS | OAKENGINE_INIT_RENDER) !=
		OAKENGINE_OK) {
		fprintf(stderr, "error: failed to initialize the engine\n");
		return k_exit_error;
	}

	int rc = k_exit_ok;
	OakEngineProject *project = nullptr;
	OakEngineFootage *footage = nullptr;
	OakEngineRenderer *renderer = nullptr;
	do {
		// Probe the source for its native geometry, frame rate and duration.
		OakEngineFootage *probed = oakengine_footage_probe(input);
		if (!probed) {
			char err[1024];
			fprintf(stderr, "error: %s\n",
					oakengine_footage_last_error(err, sizeof(err)) > 0 ?
						err :
						"probe failed");
			rc = k_exit_error;
			break;
		}
		if (oakengine_footage_get_video_stream_count(probed) < 1) {
			fprintf(stderr, "error: \"%s\" has no video stream\n", input);
			oakengine_footage_free(probed);
			rc = k_exit_error;
			break;
		}
		oak_footage_video_info vi;
		if (oakengine_footage_get_video_stream_info(probed, 0, &vi) !=
				OAKENGINE_OK ||
			vi.frame_rate_num <= 0 || vi.frame_rate_den <= 0) {
			fprintf(stderr, "error: \"%s\" has no valid video stream\n",
					input);
			oakengine_footage_free(probed);
			rc = k_exit_error;
			break;
		}
		const double fps = double(vi.frame_rate_num) / vi.frame_rate_den;
		double seconds = 0.0;
		oakengine_footage_get_duration(probed, &seconds);
		const int64_t total_ts = std::llround(seconds * fps);
		if (width <= 0) {
			width = vi.width;
		}
		const int height = std::max(2l, std::lround(
										  double(width) * vi.height / vi.width));
		oakengine_footage_free(probed);

		// Build the temporary project: import, sequence, one video + one
		// audio track, and clips spanning the whole media.
		project = oakengine_project_create();
		if (oakengine_project_new(project) != OAKENGINE_OK) {
			fprintf(stderr, "error: failed to create project\n");
			rc = k_exit_error;
			break;
		}
		std::error_code ec;
		const std::string abs_input =
			std::filesystem::absolute(std::filesystem::path(input), ec)
				.string();
		footage = oakengine_project_import_footage(
			project, ec ? input : abs_input.c_str());
		if (!footage) {
			char err[1024];
			fprintf(stderr, "error: %s\n",
					oakengine_footage_last_error(err, sizeof(err)) > 0 ?
						err :
						"import failed");
			rc = k_exit_error;
			break;
		}

		OakEngineSequence *seq = oakengine_sequence_new(project, "Transcode");
		if (!seq) {
			fprintf(stderr, "error: failed to create sequence\n");
			rc = k_exit_error;
			break;
		}
		// Clip ranges are timestamps in the SEQUENCE's frame-rate timebase
		// (which may differ from the source frame rate); the render loop
		// below runs at the source frame rate.
		int seq_fr_num = 0, seq_fr_den = 0;
		if (oakengine_sequence_get_frame_rate(seq, &seq_fr_num,
											  &seq_fr_den) != OAKENGINE_OK ||
			seq_fr_num <= 0 || seq_fr_den <= 0) {
			fprintf(stderr, "error: sequence has no valid frame rate\n");
			rc = k_exit_error;
			break;
		}
		const double seq_fps = double(seq_fr_num) / seq_fr_den;
		const int64_t clip_end_ts = std::llround(seconds * seq_fps);
		char edit_err[512];
		if (oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_VIDEO) < 0 ||
			oakengine_sequence_add_track(seq, OAKENGINE_TRACK_TYPE_AUDIO) < 0) {
			fprintf(stderr, "error: failed to add tracks: %s\n",
					oakengine_sequence_last_error(edit_err,
												  sizeof(edit_err)) > 0 ?
						edit_err :
						"(no error)");
			rc = k_exit_error;
			break;
		}
		if (!oakengine_sequence_add_footage_clip(
				seq, footage, OAKENGINE_TRACK_TYPE_VIDEO, 0, 0, clip_end_ts,
				0) ||
		!oakengine_sequence_add_footage_clip(
				seq, footage, OAKENGINE_TRACK_TYPE_AUDIO, 0, 0, clip_end_ts,
				0)) {
			fprintf(stderr, "error: failed to place clips: %s\n",
					oakengine_sequence_last_error(edit_err,
												  sizeof(edit_err)) > 0 ?
						edit_err :
						"(no error)");
			rc = k_exit_error;
			break;
		}

		if (format == "ppm") {
			std::filesystem::create_directories(out, ec);
			if (ec) {
				fprintf(stderr,
						"error: cannot create output directory \"%s\": %s\n",
						out, ec.message().c_str());
				rc = k_exit_error;
				break;
			}

			renderer = oakengine_renderer_create(seq, width, height,
												 k_pixel_format_f32,
												 vi.frame_rate_num,
												 vi.frame_rate_den, nullptr);
			if (!renderer) {
				fprintf(stderr, "error: failed to create renderer\n");
				rc = k_exit_error;
				break;
			}

			rc = render_frames_to_ppm(renderer, out, 0, total_ts);
			if (rc == k_exit_ok) {
				rc = render_audio_to_wav(renderer, out, 0, total_ts);
			}
			if (rc == k_exit_ok) {
				printf("wrote %lld PPM frame(s) (%dx%d) and audio.wav to "
					   "\"%s\"\n",
					   (long long)total_ts, width, height, out);
			}
		} else {
			// mp4: synchronous render + encode through the export facade.
			// Bit rate is left to the encoder default (bit_rate 0), H.264
			// video + AAC audio at 48 kHz stereo.
			const std::filesystem::path out_parent =
				std::filesystem::path(out).parent_path();
			if (!out_parent.empty()) {
				std::filesystem::create_directories(out_parent, ec);
			}
			if (ec) {
				fprintf(stderr,
						"error: cannot create output directory for \"%s\": %s\n",
						out, ec.message().c_str());
				rc = k_exit_error;
				break;
			}

			oak_export_options opts;
			memset(&opts, 0, sizeof(opts));
			opts.video_codec = OAKENGINE_EXPORT_VIDEO_H264;
			opts.audio_codec = OAKENGINE_EXPORT_AUDIO_AAC;
			opts.audio_sample_rate = 48000;
			opts.audio_channel_count = 2;
			oakengine_export_set_progress_callback(transcode_progress, NULL);
			const int export_rc = oakengine_export_render(
				seq, out, 0, clip_end_ts, width, height, &opts);
			oakengine_export_set_progress_callback(NULL, NULL);
			if (export_rc != OAKENGINE_OK) {
				char err[1024];
				fprintf(stderr, "error: export failed: %s\n",
						oakengine_export_last_error(err, sizeof(err)) > 0 ?
							err :
							"(no error)");
				rc = k_exit_render_unavailable;
				break;
			}
			fprintf(stderr, "\n");
			printf("wrote %dx%d H.264/AAC MP4 to \"%s\"\n", width, height,
				   out);
		}
	} while (false);

	oakengine_renderer_free(renderer);
	oakengine_footage_free(footage);
	oakengine_project_free(project);
	oakengine_shutdown();
	return rc;
}

} // namespace

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage(stderr);
		return k_exit_usage;
	}

	const std::string command = argv[1];
	if (command == "--help" || command == "-h") {
		print_usage(stdout);
		return k_exit_ok;
	}
	if (command == "info") {
		if (argc != 3) {
			print_usage(stderr);
			return k_exit_usage;
		}
		return cmd_info(argv[2]);
	}
	if (command == "render") {
		if (argc != 6) {
			print_usage(stderr);
			return k_exit_usage;
		}
		return cmd_render(argv[2], argv[3], argv[4], argv[5]);
	}
	if (command == "probe") {
		if (argc != 3) {
			print_usage(stderr);
			return k_exit_usage;
		}
		return cmd_probe(argv[2]);
	}
	if (command == "transcode") {
		if (argc < 4 || argc > 7) {
			print_usage(stderr);
			return k_exit_usage;
		}
		return cmd_transcode(argv[2], argv[3], argc, argv, 4);
	}

	fprintf(stderr, "error: unknown command \"%s\"\n", argv[1]);
	print_usage(stderr);
	return k_exit_usage;
}
