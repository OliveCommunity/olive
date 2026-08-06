/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2022 Olive Team
  Modifications Copyright (C) 2025 mikesolar

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

#include "audioprocessor.h"

#include <cstdio>
#include <cstring>

#include "common/ffmpegutils.h"

namespace olive
{

/**
 * @brief Bridge sample format for a native format via the oakcommon C ABI
 */
static int to_bridge_sample_format(core::SampleFormat fmt)
{
	int out = -1; /* fb_sample_fmt_none */
	oakcommon_ffmpegutils_get_ffmpeg_sample_format(int(fmt), &out);
	return out;
}

/**
 * @brief Ensure an AudioParams has a usable channel layout mask.
 *
 * The bridge's abuffer/aformat filters reject a channel layout mask of 0
 * (e.g. when the user config or a source stream reports a mask of 0).
 * If the mask is zero, fall back to a default layout derived from the
 * channel count (stereo when unknown).
 */
static core::AudioParams fix_channel_layout(const core::AudioParams &params)
{
	core::AudioParams result = params;

	if (params.channel_layout() == 0) {
		int channels = params.channel_count();
		if (channels <= 0) {
			channels = 2;
		}

		fprintf(stderr,
				"AudioProcessor: fixing unspecified channel layout "
				"(channels=%d) -> default %d channel layout\n",
				params.channel_count(), channels);

		result.set_channel_layout(fb_channel_layout_default(channels));
	}

	return result;
}

AudioProcessor::AudioProcessor()
{
	graph_ = nullptr;
	out_frame_ = nullptr;
}

AudioProcessor::~AudioProcessor()
{
	close();
}

bool AudioProcessor::open(const core::AudioParams &from,
						  const core::AudioParams &to, double tempo)
{
	if (graph_) {
		fprintf(stderr,
				"AudioProcessor: tried to open a processor that was "
				"already open\n");
		return false;
	}

	core::AudioParams from_fixed = fix_channel_layout(from);
	core::AudioParams to_fixed = fix_channel_layout(to);

	FBAudioGraphConfig config;
	memset(&config, 0, sizeof(config));
	config.in_sample_rate = from_fixed.sample_rate();
	config.in_channel_layout_mask = from_fixed.channel_layout();
	config.in_sample_format = to_bridge_sample_format(from_fixed.format());
	config.in_channels = from_fixed.channel_count();

	config.out_sample_rate = to_fixed.sample_rate();
	config.out_channel_layout_mask = to_fixed.channel_layout();
	config.out_sample_format = to_bridge_sample_format(to_fixed.format());
	config.out_channels = to_fixed.channel_count();
	config.out_is_planar = to_fixed.format().is_planar() ? 1 : 0;

	config.tempo = tempo;

	graph_ = fb_audio_graph_create(&config);
	if (!graph_) {
		fprintf(stderr, "AudioProcessor: failed to create audio filter "
						"graph\n");
		return false;
	}

	out_frame_ = fb_frame_alloc();
	if (!out_frame_) {
		fprintf(stderr, "AudioProcessor: failed to allocate output frame\n");
		close();
		return false;
	}

	from_ = from_fixed;
	to_ = to_fixed;

	return true;
}

void AudioProcessor::close()
{
	if (graph_) {
		fb_audio_graph_free(&graph_);
	}

	if (out_frame_) {
		fb_frame_free(&out_frame_);
	}
}

int AudioProcessor::convert(float **in, int nb_in_samples,
							AudioProcessor::Buffer *output)
{
	if (!is_open()) {
		fprintf(stderr,
				"AudioProcessor: tried to convert on closed processor\n");
		return -1;
	}

	int r = 0;

	if (in && nb_in_samples) {
		r = fb_audio_graph_push(
			graph_, reinterpret_cast<const uint8_t *const *>(in),
			nb_in_samples);
		if (r < 0) {
			fprintf(stderr,
					"AudioProcessor: failed to add frame to buffersrc: %d\n",
					r);
			return r;
		}
	}

	if (output) {
		int nb_channels = to_.channel_count();

		if (to_.format().is_packed()) {
			nb_channels = 1;
		}

		AudioProcessor::Buffer &result = *output;
		result.resize(size_t(nb_channels));

		int byte_offset = 0;

		while (true) {
			r = fb_audio_graph_pull(graph_, out_frame_);
			if (r <= 0) {
				if (r == 0) {
					// No more output available right now
					r = 0;
				} else {
					// Handle unexpected error
					fprintf(stderr,
							"AudioProcessor: failed to pull from "
							"buffersink: %d\n",
							r);
				}
				break;
			}

			int nb_bytes = fb_frame_get_nb_samples(out_frame_) *
						   to_.bytes_per_sample_per_channel();
			if (to_.format().is_packed()) {
				nb_bytes *= to_.channel_count();
			}

			for (int i = 0; i < nb_channels; i++) {
				result[size_t(i)].resize(size_t(byte_offset + nb_bytes));
				memcpy(result[size_t(i)].data() + byte_offset,
					   fb_frame_get_data(out_frame_, i), size_t(nb_bytes));
			}
			byte_offset += nb_bytes;
		}
	}

	return r;
}

void AudioProcessor::flush()
{
	int r = fb_audio_graph_push(graph_, nullptr, 0);
	if (r < 0) {
		fprintf(stderr, "AudioProcessor: failed to flush: %d\n", r);
	}
}

}
