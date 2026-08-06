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

#include "codec/encoder.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "common/colortransform.h"
#include "common/videoparams.h"
#include "encoder.h"
#include "frame.h"
#include "refcounted.h"

namespace
{

constexpr int k_rgba_channel_count = 4;

struct EncoderBox {
	std::unique_ptr<olive::Encoder> encoder;
	olive::EncodingParams params;
	bool open = false;
	bool flushed = false;
};

EncoderBox *box(void *ctx)
{
	return oakcodec::handle_impl<EncoderBox>(ctx);
}

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

olive::EncodingParams to_native(const oakcodec_encoding_params *p)
{
	using namespace olive;

	EncodingParams n;
	n.set_filename(p->filename);
	n.set_format(static_cast<ExportFormat::Format>(p->format));

	if (p->video_enabled) {
		OakVideoParams vp = oakcommon_videoparams_init_with_time_base(
			p->video_width, p->video_height, p->video_time_base_num,
			p->video_time_base_den, p->video_pixel_format,
			k_rgba_channel_count, p->video_pixel_aspect_num,
			p->video_pixel_aspect_den, p->video_interlacing, 1);
		n.enable_video(vp, static_cast<ExportCodec::Codec>(p->video_codec));
		oakcommon_videoparams_free(&vp);
		n.set_video_bit_rate(p->video_bit_rate);
		n.set_video_min_bit_rate(p->video_min_bit_rate);
		n.set_video_max_bit_rate(p->video_max_bit_rate);
		n.set_video_buffer_size(p->video_buffer_size);
		n.set_video_threads(p->video_threads);
		n.set_video_pix_fmt(p->video_pix_fmt);
		n.set_video_is_image_sequence(p->video_is_image_sequence != 0);
		n.set_video_scaling_method(
			static_cast<EncodingParams::VideoScalingMethod>(
				p->video_scaling_method));
	}

	if (p->audio_enabled) {
		AudioParams ap(p->audio_sample_rate, p->audio_channel_layout,
				   static_cast<core::SampleFormat::Format>(
					   p->audio_sample_format));
		n.enable_audio(ap, static_cast<ExportCodec::Codec>(p->audio_codec));
		n.set_audio_bit_rate(p->audio_bit_rate);
	}

	if (p->subtitles_enabled) {
		if (p->subtitles_are_sidecar) {
			n.enable_sidecar_subtitles(
				static_cast<ExportFormat::Format>(
					p->subtitles_sidecar_format),
				static_cast<ExportCodec::Codec>(p->subtitles_codec));
		} else {
			n.enable_subtitles(
				static_cast<ExportCodec::Codec>(p->subtitles_codec));
		}
	}

	if (p->color_transform_output[0] != '\0') {
		OakColorTransform ct =
			oakcommon_colortransform_init_output(p->color_transform_output);
		n.set_color_transform(ct);
		oakcommon_colortransform_free(&ct);
	}

	if (p->export_length_den != 0) {
		n.set_export_length(
			Rational(p->export_length_num, p->export_length_den));
	}

	return n;
}

} // namespace

OakEncoder oakcodec_encoder_init(const oakcodec_encoding_params *params)
{
	if (!params)
		return OakEncoder{};

	OakEncoder h = oakcodec::make_handle_in_place<OakEncoder, EncoderBox>();
	EncoderBox *b = box(h.ctx);
	if (!b)
		return OakEncoder{};

	try {
		b->params = to_native(params);
	} catch (...) {
		oakcodec_encoder_free(&h);
		return OakEncoder{};
	}

	if (!b->params.is_valid()) {
		oakcodec_encoder_free(&h);
		return OakEncoder{};
	}

	return h;
}

void oakcodec_encoder_free(OakEncoder *encoder)
{
	oakcodec::free_handle(encoder);
}

int oakcodec_encoder_set_video_option(OakEncoder encoder, const char *key,
									  const char *value)
{
	EncoderBox *b = box(encoder.ctx);
	if (!b || !key)
		return OAKCODEC_E_INVALID;
	if (b->open)
		return OAKCODEC_E_STATE;
	b->params.set_video_option(key, value ? value : "");
	return OAKCODEC_OK;
}

int oakcodec_encoder_open(OakEncoder encoder)
{
	EncoderBox *b = box(encoder.ctx);
	if (!b)
		return OAKCODEC_E_INVALID;
	if (b->open)
		return OAKCODEC_E_STATE;

	b->encoder.reset(olive::Encoder::create_from_params(b->params));
	if (!b->encoder)
		return OAKCODEC_E_FAILED;

	if (!b->encoder->open()) {
		return OAKCODEC_E_FAILED;
	}

	b->open = true;
	return OAKCODEC_OK;
}

int oakcodec_encoder_write_video(OakEncoder encoder, OakFrame frame)
{
	EncoderBox *b = box(encoder.ctx);
	if (!b || !frame.ctx)
		return OAKCODEC_E_INVALID;
	if (!b->open || b->flushed || !b->encoder)
		return OAKCODEC_E_STATE;

	// OakFrame boxes hold an olive::FramePtr (see c_api/frame.cpp).
	auto *fp = oakcodec::handle_impl<olive::FramePtr>(frame.ctx);
	if (!fp || !*fp)
		return OAKCODEC_E_INVALID;
	olive::Frame *f = fp->get();

	return b->encoder->write_frame(*fp, f->timestamp()) ? OAKCODEC_OK
													  : OAKCODEC_E_FAILED;
}

int oakcodec_encoder_write_audio(OakEncoder encoder, const float *samples,
								 int frame_count)
{
	EncoderBox *b = box(encoder.ctx);
	if (!b || (!samples && frame_count > 0) || frame_count < 0)
		return OAKCODEC_E_INVALID;
	if (!b->open || b->flushed || !b->encoder)
		return OAKCODEC_E_STATE;

	const olive::AudioParams &ap = b->params.audio_params();
	int channels = ap.channel_count();
	if (channels <= 0)
		return OAKCODEC_E_STATE;

	// Deinterleave into a planar SampleBuffer.
	olive::SampleBuffer buf(ap, static_cast<size_t>(frame_count));
	buf.allocate();
	std::vector<float> channel_data(static_cast<size_t>(frame_count));
	for (int c = 0; c < channels; c++) {
		for (int i = 0; i < frame_count; i++) {
			channel_data[i] = samples[static_cast<size_t>(i) * channels + c];
		}
		buf.set(c, channel_data.data(),
				static_cast<size_t>(frame_count));
	}

	return b->encoder->write_audio(buf) ? OAKCODEC_OK : OAKCODEC_E_FAILED;
}

int oakcodec_encoder_write_subtitle(OakEncoder encoder, const char *text,
								double in_seconds, double out_seconds)
{
	EncoderBox *b = box(encoder.ctx);
	if (!b || !text)
		return OAKCODEC_E_INVALID;
	if (!b->open || b->flushed || !b->encoder)
		return OAKCODEC_E_STATE;

	return b->encoder->write_subtitle(text, in_seconds, out_seconds)
			   ? OAKCODEC_OK
			   : OAKCODEC_E_FAILED;
}

int oakcodec_encoder_flush(OakEncoder encoder)
{
	EncoderBox *b = box(encoder.ctx);
	if (!b)
		return OAKCODEC_E_INVALID;
	if (!b->open)
		return OAKCODEC_E_STATE;
	if (b->flushed)
		return OAKCODEC_OK;

	b->encoder->close();
	b->flushed = true;
	return OAKCODEC_OK;
}

int oakcodec_encoder_last_error(OakEncoder encoder, char *buf, int buf_size)
{
	EncoderBox *b = box(encoder.ctx);
	if (!b)
		return string_out("", buf, buf_size);
	return string_out(b->encoder ? b->encoder->get_error() : std::string(),
				  buf, buf_size);
}
