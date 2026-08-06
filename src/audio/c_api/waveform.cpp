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

#include "audio/waveform.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "audiovisualwaveform.h"
#include "codec/decoder.h"
#include "ffmpeg_bridge/ffmpeg_bridge.h"
#include "olive/core/render/samplebuffer.h"
#include "refcounted.h"

using olive::AudioVisualWaveform;
using olive::core::AudioParams;
using olive::core::Rational;
using olive::core::SampleBuffer;
using olive::core::SampleFormat;

namespace
{

AudioVisualWaveform::SamplePerChannel *as_pairs(oakaudio_min_max *p)
{
	static_assert(sizeof(oakaudio_min_max) ==
					  sizeof(AudioVisualWaveform::SamplePerChannel),
				  "POD layout mismatch");
	return reinterpret_cast<AudioVisualWaveform::SamplePerChannel *>(p);
}

const AudioVisualWaveform::SamplePerChannel *
as_pairs_const(const oakaudio_min_max *p)
{
	return reinterpret_cast<const AudioVisualWaveform::SamplePerChannel *>(p);
}

bool make_rational(int64_t num, int64_t den, Rational *out)
{
	if (den == 0) {
		return false;
	}
	*out = Rational(int(num), int(den));
	return true;
}

/* ---- oakaudio_waveform_extract() helpers --------------------------------- */

#define OAKAUDIO_EXTRACT_MAX_CHANNELS 64

using PendingPlanes = std::vector<std::vector<float>>;

void append_pending(PendingPlanes &pending, FBFrame *frame, int channels,
				int nb)
{
	if (pending.empty()) {
		pending.resize(size_t(channels));
	}
	for (int ch = 0; ch < channels; ch++) {
		const float *data =
			reinterpret_cast<const float *>(fb_frame_get_data(frame, ch));
		std::vector<float> &plane = pending[size_t(ch)];
		plane.insert(plane.end(), data, data + nb);
	}
}

// Emit one point per samples_per_point pending samples. With `flush`, a
// trailing partial point is emitted too.
void emit_points(int channels, int samples_per_point, PendingPlanes &pending,
			 std::vector<oakaudio_min_max> &points, bool flush)
{
	if (pending.empty()) {
		return;
	}
	while (true) {
		const size_t available = pending[0].size();
		if (available == 0 ||
			(!flush && available < size_t(samples_per_point))) {
			return;
		}
		const size_t n = std::min(available, size_t(samples_per_point));

		const size_t point = points.size() / size_t(channels);
		points.resize(points.size() + size_t(channels));
		for (int ch = 0; ch < channels; ch++) {
			std::vector<float> &plane = pending[size_t(ch)];
			float mn = plane[0];
			float mx = mn;
			for (size_t i = 1; i < n; i++) {
				mn = std::min(mn, plane[i]);
				mx = std::max(mx, plane[i]);
			}
			oakaudio_min_max &dst =
				points[point * size_t(channels) + size_t(ch)];
			dst.min = mn;
			dst.max = mx;
			plane.erase(plane.begin(), plane.begin() + ptrdiff_t(n));
		}
	}
}

int drain_graph(FBAudioGraph *graph, FBFrame *converted, int channels,
			int samples_per_point, PendingPlanes &pending,
			std::vector<oakaudio_min_max> &points)
{
	while (true) {
		const int pull = fb_audio_graph_pull(graph, converted);
		if (pull < 0) {
			return OAKAUDIO_E_FAILED;
		}
		if (pull == 0) {
			return OAKAUDIO_OK;
		}
		append_pending(pending, converted, channels,
					   fb_frame_get_nb_samples(converted));
		emit_points(channels, samples_per_point, pending, points, false);
	}
}

void flush_points(int channels, int samples_per_point, PendingPlanes &pending,
			  std::vector<oakaudio_min_max> &points)
{
	emit_points(channels, samples_per_point, pending, points, true);
}

} // namespace

extern "C" OakAudioWaveform oakaudio_waveform_init(void)
{
	return oakaudio::make_handle_in_place<OakAudioWaveform,
									   AudioVisualWaveform>();
}

extern "C" void oakaudio_waveform_free(OakAudioWaveform *self)
{
	oakaudio::free_handle(self);
}

extern "C" int oakaudio_waveform_get_channel_count(OakAudioWaveform self)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	return w->channel_count();
}

extern "C" int oakaudio_waveform_set_channel_count(OakAudioWaveform self,
		int channels)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	if (channels < 0) {
		return OAKAUDIO_E_INVALID;
	}
	w->set_channel_count(channels);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_length(OakAudioWaveform self,
		int64_t *num, int64_t *den)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	if (!num || !den) {
		return OAKAUDIO_E_INVALID;
	}
	*num = w->length().numerator();
	*den = w->length().denominator();
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_overwrite_samples(OakAudioWaveform self,
		const float *const *planar, int frame_count, int sample_rate,
		int64_t start_num, int64_t start_den)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	Rational start;
	if (!planar || frame_count <= 0 || sample_rate <= 0 ||
		!make_rational(start_num, start_den, &start)) {
		return OAKAUDIO_E_INVALID;
	}

	const int channels = w->channel_count();
	if (channels <= 0) {
		return OAKAUDIO_E_STATE;
	}

	// Repack the caller's planes into a SampleBuffer (planar f32).
	AudioParams params(sample_rate, fb_channel_layout_default(channels),
					SampleFormat(SampleFormat::f32_p));
	SampleBuffer buffer(params, Rational(frame_count, sample_rate));
	for (int ch = 0; ch < channels; ch++) {
		if (!planar[ch]) {
			return OAKAUDIO_E_INVALID;
		}
	}
	for (int ch = 0; ch < channels; ch++) {
		memcpy(buffer.data(ch), planar[ch],
			   size_t(frame_count) * sizeof(float));
	}

	w->overwrite_samples(buffer, sample_rate, start);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_overwrite_sums(OakAudioWaveform self,
		OakAudioWaveform src,
		int64_t dest_num, int64_t dest_den,
		int64_t offset_num, int64_t offset_den,
		int64_t length_num, int64_t length_den)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	AudioVisualWaveform *other =
		oakaudio::handle_impl<AudioVisualWaveform>(src.ctx);
	if (!w || !other) {
		return OAKAUDIO_E_INVALID;
	}
	Rational dest, offset, length;
	if (!make_rational(dest_num, dest_den, &dest) ||
		!make_rational(offset_num, offset_den, &offset) ||
		!make_rational(length_num, length_den, &length)) {
		return OAKAUDIO_E_INVALID;
	}
	w->overwrite_sums(*other, dest, offset, length);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_overwrite_silence(OakAudioWaveform self,
		int64_t start_num, int64_t start_den,
		int64_t length_num, int64_t length_den)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	Rational start, length;
	if (!make_rational(start_num, start_den, &start) ||
		!make_rational(length_num, length_den, &length)) {
		return OAKAUDIO_E_INVALID;
	}
	w->overwrite_silence(start, length);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_trim_in(OakAudioWaveform self,
		int64_t length_num, int64_t length_den)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	Rational length;
	if (!make_rational(length_num, length_den, &length)) {
		return OAKAUDIO_E_INVALID;
	}
	w->trim_in(length);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_resize(OakAudioWaveform self,
		int64_t length_num, int64_t length_den)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	Rational length;
	if (!make_rational(length_num, length_den, &length) || length < 0) {
		return OAKAUDIO_E_INVALID;
	}
	w->resize(length);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_trim_range(OakAudioWaveform self,
		int64_t in_num, int64_t in_den,
		int64_t length_num, int64_t length_den)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	Rational in, length;
	if (!make_rational(in_num, in_den, &in) ||
		!make_rational(length_num, length_den, &length)) {
		return OAKAUDIO_E_INVALID;
	}
	w->trim_range(in, length);
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_get_summary(OakAudioWaveform self,
		int64_t start_num, int64_t start_den,
		int64_t length_num, int64_t length_den,
		oakaudio_min_max *out_pairs, int capacity_points)
{
	AudioVisualWaveform *w =
		oakaudio::handle_impl<AudioVisualWaveform>(self.ctx);
	if (!w) {
		return OAKAUDIO_E_INVALID;
	}
	Rational start, length;
	if (!make_rational(start_num, start_den, &start) ||
		!make_rational(length_num, length_den, &length) || length <= 0 ||
		capacity_points < 0) {
		return OAKAUDIO_E_INVALID;
	}

	// Points are produced at the length scale: one point per channel per
	// `length`-sized window covering [start, start+length) — i.e. exactly
	// one point, matching AudioVisualWaveform::get_summary_from_time().
	AudioVisualWaveform::Sample summary =
		w->get_summary_from_time(start, length);
	const int points = int(summary.size()) /
					   std::max(1, w->channel_count());

	if (!out_pairs || capacity_points < points) {
		return points;
	}
	memcpy(out_pairs, summary.data(),
		   summary.size() * sizeof(oakaudio_min_max));
	return points;
}

extern "C" int oakaudio_waveform_sum_samples_s(const float *const *planar,
		int channel_count, int start_index, int length,
		oakaudio_min_max *out)
{
	if (!planar || !out || channel_count <= 0 || start_index < 0 ||
		length <= 0) {
		return OAKAUDIO_E_INVALID;
	}

	AudioParams params(48000, fb_channel_layout_default(channel_count),
					SampleFormat(SampleFormat::f32_p));
	SampleBuffer buffer(params, Rational(length + start_index, 48000));
	for (int ch = 0; ch < channel_count; ch++) {
		if (!planar[ch]) {
			return OAKAUDIO_E_INVALID;
		}
		memcpy(buffer.data(ch) + start_index, planar[ch],
			   size_t(length) * sizeof(float));
	}

	AudioVisualWaveform::Sample summary = AudioVisualWaveform::sum_samples(
		buffer, size_t(start_index), size_t(length));
	if (int(summary.size()) < channel_count) {
		return OAKAUDIO_E_FAILED;
	}
	memcpy(out, summary.data(),
		   size_t(channel_count) * sizeof(oakaudio_min_max));
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_re_sum_s(const oakaudio_min_max *in,
		int nb_entries, int nb_channels, oakaudio_min_max *out)
{
	if (!in || !out || nb_entries <= 0 || nb_channels <= 0) {
		return OAKAUDIO_E_INVALID;
	}

	AudioVisualWaveform::Sample summary = AudioVisualWaveform::re_sum_samples(
		as_pairs_const(in), size_t(nb_entries), nb_channels);
	memcpy(out, summary.data(),
		   size_t(nb_channels) * sizeof(oakaudio_min_max));
	return OAKAUDIO_OK;
}

extern "C" int oakaudio_waveform_extract(const char *filename,
		int stream_index, int samples_per_point,
		oakaudio_min_max *out_pairs, int capacity_points,
		int *out_channel_count)
{
	if (!filename || stream_index < 0 || samples_per_point <= 0 ||
		capacity_points < 0) {
		return OAKAUDIO_E_INVALID;
	}

	// Probe for the stream's native rate/layout (oakcodec probe is
	// stateless and does not need a conform)
	OakDecoder probe = oakcodec_decoder_probe(filename);
	if (!probe.ctx) {
		return OAKAUDIO_E_NOT_FOUND;
	}
	oakcodec_audio_stream_info info;
	int r = oakcodec_decoder_probe_get_audio_stream(probe, stream_index,
												  &info);
	oakcodec_decoder_free(&probe);
	if (r != OAKCODEC_OK) {
		return OAKAUDIO_E_NOT_FOUND;
	}
	if (info.sample_rate <= 0 || info.channel_count <= 0) {
		return OAKAUDIO_E_FAILED;
	}

	// Decode the whole stream through ffmpeg_bridge (fb_decoder +
	// fb_audio_graph) rather than oakcodec_decoder_decode_audio: the
	// oakcodec decode path is conform-cache based and cannot decode media
	// without an existing pcm conform until the task system lands (M8).
	// The stream is reduced to channel-interleaved min/max points at the
	// native rate/layout.
	FBDecoder *decoder = fb_decoder_create();
	if (!decoder) {
		return OAKAUDIO_E_NOMEM;
	}
	r = fb_decoder_open(decoder, filename, info.stream_index);
	if (r < 0) {
		fb_decoder_free(&decoder);
		return OAKAUDIO_E_FAILED;
	}

	const int channels = info.channel_count;
	std::vector<oakaudio_min_max> points;
	PendingPlanes pending; // per-channel planar backlog

	// The graph converts the stream's native format to planar float; the
	// stream info carries the validated sample format/rate/layout (audio
	// frames do not report a sample format through fb_frame_get_format).
	FBStreamInfo sinfo;
	if (fb_decoder_get_stream_info(decoder, &sinfo) < 0 ||
		sinfo.sample_rate <= 0) {
		fb_decoder_close(decoder);
		fb_decoder_free(&decoder);
		return OAKAUDIO_E_FAILED;
	}

	FBAudioGraphConfig config;
	memset(&config, 0, sizeof(config));
	config.in_sample_rate = sinfo.sample_rate;
	config.in_channel_layout_mask = sinfo.channel_layout_mask;
	config.in_sample_format = sinfo.sample_format;
	config.in_channels = channels;
	config.out_sample_rate = config.in_sample_rate;
	config.out_channel_layout_mask = config.in_channel_layout_mask;
	config.out_sample_format = fb_sample_fmt_fltp;
	config.out_channels = channels;
	config.out_is_planar = 1;
	config.tempo = 1.0;

	FBPacket *packet = fb_packet_alloc();
	FBFrame *frame = fb_frame_alloc();
	FBFrame *converted = fb_frame_alloc();
	FBAudioGraph *graph = fb_audio_graph_create(&config);
	int result = OAKAUDIO_OK;

	if (!packet || !frame || !converted) {
		result = OAKAUDIO_E_NOMEM;
		goto done;
	}
	if (!graph) {
		result = OAKAUDIO_E_FAILED;
		goto done;
	}

	while (true) {
		if (fb_decoder_get_frame(decoder, packet, frame) < 0) {
			break; // EOF or error: stop decoding
		}

		// Push the decoded frame (planar pointer array; a packed source is
		// read from plane 0 by the buffersrc)
		const uint8_t *planes[OAKAUDIO_EXTRACT_MAX_CHANNELS];
		for (int ch = 0; ch < channels; ch++) {
			planes[ch] = fb_frame_get_data(frame, ch);
		}
		if (fb_audio_graph_push(graph, planes,
							fb_frame_get_nb_samples(frame)) < 0) {
			result = OAKAUDIO_E_FAILED;
			goto done;
		}

		if (drain_graph(graph, converted, channels, samples_per_point,
					pending, points) != OAKAUDIO_OK) {
			result = OAKAUDIO_E_FAILED;
			goto done;
		}
	}

	// Flush the resampler delay
	if (graph) {
		fb_audio_graph_push(graph, nullptr, 0);
		while (fb_audio_graph_pull(graph, converted) == 1) {
			append_pending(pending, converted, channels,
						   fb_frame_get_nb_samples(converted));
		}
		flush_points(channels, samples_per_point, pending, points);
	}

done:
	if (graph) {
		fb_audio_graph_free(&graph);
	}
	if (converted) {
		fb_frame_free(&converted);
	}
	if (frame) {
		fb_frame_free(&frame);
	}
	if (packet) {
		fb_packet_free(&packet);
	}
	fb_decoder_close(decoder);
	fb_decoder_free(&decoder);
	if (result != OAKAUDIO_OK) {
		return result;
	}

	if (out_channel_count) {
		*out_channel_count = channels;
	}

	const int point_count = int(points.size()) / channels;
	if (!out_pairs || capacity_points < point_count) {
		return point_count;
	}
	memcpy(out_pairs, points.data(),
		   points.size() * sizeof(oakaudio_min_max));
	return point_count;
}
