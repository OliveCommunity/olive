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

#include "codec/decoder.h"

#include <algorithm>
#include <cstring>
#include <string>

#include <sys/stat.h>

#include "common/loopmode.h"
#include "decoder.h"
#include "footagedescription.h"
#include "frame.h"
#include "refcounted.h"

namespace
{

struct ProbeBox {
	std::string decoder_name;
	olive::FootageDescription desc;
};

struct DecoderBox {
	olive::DecoderPtr decoder;
	std::string last_error;
	std::string open_filename;
	int open_stream = -1;
	bool open = false;
};

ProbeBox *probe_box(void *ctx)
{
	return oakcodec::handle_impl<ProbeBox>(ctx);
}

DecoderBox *decoder_box(void *ctx)
{
	return oakcodec::handle_impl<DecoderBox>(ctx);
}

thread_local std::string g_probe_error;

int string_out(const std::string &s, char *buf, int buf_size)
{
	int need = static_cast<int>(s.size()) + 1;
	if (buf && buf_size > 0) {
		int n = std::min(static_cast<int>(s.size()), buf_size - 1);
		memcpy(buf, s.data(), n);
		buf[n] = '\0';
	}
	return need;
}

bool file_exists(const char *filename)
{
	struct stat st;
	return filename && stat(filename, &st) == 0;
}

/**
 * @brief Probe with every available decoder, returning the first valid
 *        description (and filling `name`).
 */
bool probe_with_any_decoder(const char *filename, std::string *name,
							olive::FootageDescription *out)
{
	for (const olive::DecoderPtr &d :
		 olive::Decoder::receive_list_of_all_decoders()) {
		olive::FootageDescription desc = d->probe(filename, nullptr);
		if (desc.is_valid()) {
			*name = desc.decoder();
			*out = desc;
			return true;
		}
	}
	return false;
}

void fill_video_info(const OakVideoParams &vp,
					 oakcodec_video_stream_info *out)
{
	*out = {};

	oakcommon_videoparams_get_stream_index(vp, &out->stream_index);
	oakcommon_videoparams_get_width(vp, &out->width);
	oakcommon_videoparams_get_height(vp, &out->height);

	int fr_num = 0, fr_den = 0;
	oakcommon_videoparams_get_frame_rate(vp, &fr_num, &fr_den);
	out->frame_rate_num = fr_num;
	out->frame_rate_den = fr_den;

	int tb_num = 0, tb_den = 0;
	oakcommon_videoparams_get_time_base(vp, &tb_num, &tb_den);
	out->time_base_num = tb_num;
	out->time_base_den = tb_den;

	oakcommon_videoparams_get_duration(vp, &out->duration_ts);
	oakcommon_videoparams_get_format(vp, &out->format);
	oakcommon_videoparams_get_channel_count(vp, &out->channel_count);
	oakcommon_videoparams_get_color_primaries(vp, &out->color_primaries);
	oakcommon_videoparams_get_color_transfer(vp, &out->color_trc);

	int interlacing = OAKCOMMON_VIDEO_INTERLACE_NONE;
	oakcommon_videoparams_get_interlacing(vp, &interlacing);
	out->interlaced = interlacing != OAKCOMMON_VIDEO_INTERLACE_NONE;
}

void fill_audio_info(const olive::AudioParams &ap,
					 oakcodec_audio_stream_info *out)
{
	*out = {};
	out->stream_index = ap.stream_index();
	out->sample_rate = ap.sample_rate();
	out->channel_layout = ap.channel_layout();
	out->channel_count = ap.channel_count();

	olive::Rational tb = ap.time_base();
	out->time_base_num = tb.numerator();
	out->time_base_den = tb.denominator();
	// AudioParams carries no duration; duration_ts stays 0 (unknown).
}

} // namespace

/* ---- Probe ---------------------------------------------------------------- */

OakDecoder oakcodec_decoder_probe(const char *filename)
{
	if (!filename || !*filename) {
		g_probe_error = "no filename given";
		return OakDecoder{};
	}
	if (!file_exists(filename)) {
		g_probe_error = std::string("file not found: ") + filename;
		return OakDecoder{};
	}

	OakDecoder h = oakcodec::make_handle_in_place<OakDecoder, ProbeBox>();
	ProbeBox *b = probe_box(h.ctx);
	if (!b) {
		g_probe_error = "out of memory";
		return OakDecoder{};
	}

	if (!probe_with_any_decoder(filename, &b->decoder_name, &b->desc)) {
		g_probe_error =
			std::string("no decoder recognizes this file: ") + filename;
		oakcodec_decoder_free(&h);
		return OakDecoder{};
	}

	g_probe_error.clear();
	return h;
}

int oakcodec_probe_last_error(char *buf, int buf_size)
{
	return string_out(g_probe_error, buf, buf_size);
}

int oakcodec_decoder_probe_decoder_name(OakDecoder probe, char *buf,
									int buf_size)
{
	ProbeBox *b = probe_box(probe.ctx);
	if (!b)
		return OAKCODEC_E_INVALID;
	return string_out(b->decoder_name, buf, buf_size);
}

int oakcodec_decoder_probe_video_stream_count(OakDecoder probe)
{
	ProbeBox *b = probe_box(probe.ctx);
	if (!b)
		return 0;
	return static_cast<int>(b->desc.get_video_streams().size());
}

int oakcodec_decoder_probe_audio_stream_count(OakDecoder probe)
{
	ProbeBox *b = probe_box(probe.ctx);
	if (!b)
		return 0;
	return static_cast<int>(b->desc.get_audio_streams().size());
}

int oakcodec_decoder_probe_subtitle_stream_count(OakDecoder probe)
{
	ProbeBox *b = probe_box(probe.ctx);
	if (!b)
		return 0;
	return static_cast<int>(b->desc.get_subtitle_streams().size());
}

int oakcodec_decoder_probe_get_video_stream(OakDecoder probe, int index,
										oakcodec_video_stream_info *out)
{
	ProbeBox *b = probe_box(probe.ctx);
	if (!b || !out)
		return OAKCODEC_E_INVALID;
	const auto &streams = b->desc.get_video_streams();
	if (index < 0 || index >= static_cast<int>(streams.size()))
		return OAKCODEC_E_NOT_FOUND;
	fill_video_info(streams[static_cast<size_t>(index)], out);
	return OAKCODEC_OK;
}

int oakcodec_decoder_probe_get_audio_stream(OakDecoder probe, int index,
										oakcodec_audio_stream_info *out)
{
	ProbeBox *b = probe_box(probe.ctx);
	if (!b || !out)
		return OAKCODEC_E_INVALID;
	const auto &streams = b->desc.get_audio_streams();
	if (index < 0 || index >= static_cast<int>(streams.size()))
		return OAKCODEC_E_NOT_FOUND;
	fill_audio_info(streams[static_cast<size_t>(index)], out);
	return OAKCODEC_OK;
}

/* ---- Decode session -------------------------------------------------------- */

OakDecoder oakcodec_decoder_init(void)
{
	return oakcodec::make_handle_in_place<OakDecoder, DecoderBox>();
}

void oakcodec_decoder_free(OakDecoder *decoder)
{
	oakcodec::free_handle(decoder);
}

int oakcodec_decoder_open(OakDecoder decoder, const char *filename,
						  int stream_index)
{
	DecoderBox *b = decoder_box(decoder.ctx);
	if (!b || !filename || stream_index < 0)
		return OAKCODEC_E_INVALID;

	if (b->open && b->decoder) {
		if (b->open_filename == filename && b->open_stream == stream_index)
			return OAKCODEC_OK; // already open on this stream
		b->decoder->close();
		b->open = false;
	}

	if (!file_exists(filename)) {
		b->last_error = std::string("file not found: ") + filename;
		return OAKCODEC_E_NOT_FOUND;
	}

	std::string decoder_name;
	olive::FootageDescription desc;
	if (!probe_with_any_decoder(filename, &decoder_name, &desc)) {
		b->last_error =
			std::string("no decoder recognizes this file: ") + filename;
		return OAKCODEC_E_FAILED;
	}

	b->decoder = olive::Decoder::create_from_id(decoder_name);
	if (!b->decoder) {
		b->last_error = std::string("failed to create decoder: ") + decoder_name;
		return OAKCODEC_E_FAILED;
	}

	if (!b->decoder->open(
			olive::Decoder::CodecStream(filename, stream_index, nullptr))) {
		b->last_error = "failed to open stream";
		b->decoder.reset();
		return OAKCODEC_E_FAILED;
	}

	b->last_error.clear();
	b->open_filename = filename;
	b->open_stream = stream_index;
	b->open = true;
	return OAKCODEC_OK;
}

int oakcodec_decoder_close(OakDecoder decoder)
{
	DecoderBox *b = decoder_box(decoder.ctx);
	if (!b)
		return OAKCODEC_E_INVALID;
	if (b->open && b->decoder) {
		b->decoder->close();
	}
	b->open = false;
	return OAKCODEC_OK;
}

int oakcodec_decoder_is_open(OakDecoder decoder)
{
	DecoderBox *b = decoder_box(decoder.ctx);
	return (b && b->open) ? 1 : 0;
}

OakFrame oakcodec_decoder_decode_video(OakDecoder decoder, int numerator,
								   int denominator)
{
	DecoderBox *b = decoder_box(decoder.ctx);
	if (!b || !b->open || !b->decoder)
		return OakFrame{};

	olive::Decoder::RetrieveVideoParams p;
	p.time = olive::Rational(numerator, denominator);

	olive::FramePtr frame = b->decoder->retrieve_video_frame(p);
	if (!frame) {
		b->last_error = "failed to decode video frame";
		return OakFrame{};
	}

	return oakcodec::make_handle<OakFrame>(std::move(frame));
}

int oakcodec_decoder_decode_audio(OakDecoder decoder, int in_num, int in_den,
								  int out_num, int out_den, int sample_rate,
								  uint64_t channel_layout, float *buf,
								  int buf_frames)
{
	DecoderBox *b = decoder_box(decoder.ctx);
	if (!b || (!buf && buf_frames > 0) || buf_frames < 0)
		return OAKCODEC_E_INVALID;
	if (!b->open || !b->decoder)
		return OAKCODEC_E_STATE;

	olive::AudioParams params(sample_rate, channel_layout,
						  olive::core::SampleFormat::f32);
	olive::TimeRange range(olive::Rational(in_num, in_den),
					   olive::Rational(out_num, out_den));

	olive::SampleBuffer samples;
	olive::Decoder::RetrieveAudioStatus status = b->decoder->retrieve_audio(
		samples, range, params, std::string(), OAKCOMMON_LOOP_MODE_OFF,
		olive::RenderMode::k_offline);

	if (status == olive::Decoder::k_waiting_for_conform) {
		// Interim state (pre-M8): conform tasks require a task registrar.
		b->last_error =
			"audio requires a conform, but no task submit callback is "
			"registered (see oakcodec_set_task_submit_cb)";
		return OAKCODEC_E_STATE;
	}
	if (status != olive::Decoder::k_ok || !samples.is_allocated()) {
		b->last_error = "failed to decode audio";
		return OAKCODEC_E_FAILED;
	}

	int channels = samples.channel_count();
	int available = static_cast<int>(samples.sample_count());
	int frames = std::min(available, buf_frames);
	for (int c = 0; c < channels; c++) {
		const float *src = samples.data(c);
		for (int i = 0; i < frames; i++) {
			buf[static_cast<size_t>(i) * channels + c] = src[i];
		}
	}
	return frames;
}

int oakcodec_decoder_last_error(OakDecoder decoder, char *buf, int buf_size)
{
	DecoderBox *b = decoder_box(decoder.ctx);
	if (!b)
		return string_out("", buf, buf_size);
	return string_out(b->last_error, buf, buf_size);
}
