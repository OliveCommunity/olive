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

#include "ffmpegencoder.h"

#include <cstring>
#include <vector>

#include <QFile>

#include "common/ffmpegutils.h"

namespace olive
{

FFmpegEncoder::FFmpegEncoder(const EncodingParams &params)
	: Encoder(params)
	, encoder_(nullptr)
	, open_(false)
{
}

QStringList FFmpegEncoder::get_pixel_formats_for_codec(ExportCodec::Codec c) const
{
	QStringList pix_fmts;

	int bridge_codec = export_codec_to_bridge(c);
	if (bridge_codec != fb_codec_none) {
		int count =
			fb_encoder_codec_get_pixel_formats(bridge_codec, nullptr, 0);
		if (count > 0) {
			std::vector<const char *> names(static_cast<size_t>(count));
			fb_encoder_codec_get_pixel_formats(bridge_codec, names.data(),
											   count);
			for (int i = 0; i < count; i++) {
				pix_fmts.append(QString::fromUtf8(names[size_t(i)]));
			}
		}
	}

	return pix_fmts;
}

std::vector<SampleFormat>
FFmpegEncoder::get_sample_formats_for_codec(ExportCodec::Codec c) const
{
	std::vector<SampleFormat> f;

	if (c == ExportCodec::k_codec_pcm) {
		// FFmpeg lists these as separate codecs so we need custom functionality here
		// We list signed 16 first because ExportDialog will always use the first element by default
		// (because first element is the "default" in FFmpeg)
		f = { SampleFormat::s16, SampleFormat::u8,	SampleFormat::s32,
			  SampleFormat::s64, SampleFormat::f32, SampleFormat::f64 };
	} else {
		int bridge_codec = export_codec_to_bridge(c);
		if (bridge_codec != fb_codec_none) {
			int count =
				fb_encoder_codec_get_sample_formats(bridge_codec, nullptr, 0);
			if (count > 0) {
				std::vector<int> fmts(static_cast<size_t>(count));
				fb_encoder_codec_get_sample_formats(bridge_codec, fmts.data(),
													count);
				for (int fmt : fmts) {
					SampleFormat native =
						FFmpegUtils::get_native_sample_format(fmt);
					if (native != SampleFormat::invalid) {
						f.push_back(native);
					}
				}
			}
		}
	}

	return f;
}

bool FFmpegEncoder::open()
{
	if (open_) {
		return true;
	}

	// Convert QString to C string
	QByteArray filename_bytes = params().filename().toUtf8();

	FBEncoderConfig config;
	memset(&config, 0, sizeof(config));
	config.filename = filename_bytes.constData();

	// Storage keeping C strings alive until fb_encoder_create deep-copies them
	QByteArray pix_fmt_bytes;
	QByteArray subtitle_header;
	std::vector<QByteArray> opt_key_storage;
	std::vector<QByteArray> opt_value_storage;
	std::vector<const char *> opt_keys;
	std::vector<const char *> opt_values;

	// Set up video if it's enabled
	if (params().video_enabled()) {
		config.video_enabled = 1;
		config.video_codec = export_codec_to_bridge(params().video_codec());
		config.video_width = params().video_params().width();
		config.video_height = params().video_params().height();
		config.video_pixel_aspect_num =
			params().video_params().pixel_aspect_ratio().numerator();
		config.video_pixel_aspect_den =
			params().video_params().pixel_aspect_ratio().denominator();
		config.video_time_base_num =
			params().video_params().frame_rate_as_time_base().numerator();
		config.video_time_base_den =
			params().video_params().frame_rate_as_time_base().denominator();
		config.video_frame_rate_num =
			params().video_params().frame_rate().numerator();
		config.video_frame_rate_den =
			params().video_params().frame_rate().denominator();

		pix_fmt_bytes = params().video_pix_fmt().toUtf8();
		config.video_pix_fmt = pix_fmt_bytes.constData();

		// This is the format we will expect frames received in Write() to be in
		PixelFormat native_pixel_fmt = params().video_params().format();

		// This is the format we will need to convert the frame to for the bridge to understand it
		video_conversion_fmt_ =
			FFmpegUtils::get_compatible_pixel_format(native_pixel_fmt);

		// These are the equivalent pixel formats as bridge pixel formats
		int src_alpha_pix_fmt = FFmpegUtils::get_f_fmpeg_pixel_format(
			video_conversion_fmt_, VideoParams::k_rgba_channel_count);
		int src_noalpha_pix_fmt = FFmpegUtils::get_f_fmpeg_pixel_format(
			video_conversion_fmt_, VideoParams::k_rgb_channel_count);

		if (src_alpha_pix_fmt == fb_pix_fmt_none ||
			src_noalpha_pix_fmt == fb_pix_fmt_none) {
			set_error(
				tr("Failed to find suitable pixel format for this buffer"));
			return false;
		}

		config.video_src_pix_fmt = src_alpha_pix_fmt;

		config.video_color_range =
			params().video_params().color_range() ==
					VideoParams::k_color_range_full ?
				fb_color_range_jpeg :
				fb_color_range_mpeg;

		switch (params().video_params().interlacing()) {
		case VideoParams::k_interlaced_top_first:
			config.video_field_order = fb_field_order_tt;
			break;
		case VideoParams::k_interlaced_bottom_first:
			config.video_field_order = fb_field_order_bb;
			break;
		default:
			config.video_field_order = fb_field_order_progressive;
			break;
		}

		config.video_bit_rate = params().video_bit_rate();
		config.video_min_bit_rate = params().video_min_bit_rate();
		config.video_max_bit_rate = params().video_max_bit_rate();
		config.video_buffer_size = params().video_buffer_size();
		config.video_threads = params().video_threads();
		config.video_color_srgb =
			params().color_transform().output().contains(
				QStringLiteral("sRGB"), Qt::CaseInsensitive) ?
				1 :
				0;

		// Custom options (skip Olive-internal keys)
		for (auto i = params().video_opts().begin();
			 i != params().video_opts().end(); i++) {
			if (!i.key().startsWith(QStringLiteral("ove_"))) {
				opt_key_storage.push_back(i.key().toUtf8());
				opt_value_storage.push_back(i.value().toUtf8());
			}
		}
		for (size_t i = 0; i < opt_key_storage.size(); i++) {
			opt_keys.push_back(opt_key_storage[i].constData());
			opt_values.push_back(opt_value_storage[i].constData());
		}
		config.video_opt_keys = opt_keys.data();
		config.video_opt_values = opt_values.data();
		config.video_opt_count = int(opt_keys.size());
	}

	// Set up audio if it's enabled
	if (params().audio_enabled()) {
		config.audio_enabled = 1;
		config.audio_codec = export_codec_to_bridge(params().audio_codec());
		config.audio_sample_rate = params().audio_params().sample_rate();
		config.audio_channel_layout_mask =
			params().audio_params().channel_layout();
		config.audio_sample_format = FFmpegUtils::get_f_fmpeg_sample_format(
			params().audio_params().format());
		config.audio_bit_rate = params().audio_bit_rate();
	}

	// Set up subtitles if they're enabled
	if (params().subtitles_enabled()) {
		config.subtitles_enabled = 1;
		config.subtitle_codec = export_codec_to_bridge(params().subtitles_codec());
		subtitle_header = SubtitleParams::generate_ass_header().toUtf8();
		config.subtitle_header =
			reinterpret_cast<const uint8_t *>(subtitle_header.constData());
		config.subtitle_header_size = subtitle_header.size();
	}

	encoder_ = fb_encoder_create(&config);
	if (!encoder_) {
		set_error(tr("Failed to create encoder"));
		return false;
	}

	if (fb_encoder_open(encoder_) != 0) {
		set_error_from_bridge();
		fb_encoder_free(&encoder_);
		return false;
	}

	open_ = true;
	return true;
}

bool FFmpegEncoder::write_frame(FramePtr frame, Rational time)
{
	// We may need to convert this frame to a frame that the bridge will understand
	if (frame->format() != video_conversion_fmt_) {
		frame = frame->convert(video_conversion_fmt_);
	}

	int src_pix_fmt = FFmpegUtils::get_f_fmpeg_pixel_format(frame->format(),
														frame->channel_count());

	int r = fb_encoder_write_video_frame(
		encoder_, frame->width(), frame->height(), src_pix_fmt,
		reinterpret_cast<const uint8_t *>(frame->data()),
		frame->linesize_bytes(), time.to_double());
	if (r != 0) {
		set_error_from_bridge();
		return false;
	}

	return true;
}

bool FFmpegEncoder::write_audio(const SampleBuffer &audio)
{
	if (!audio.is_allocated()) {
		return true;
	}

	const AudioParams &audio_params = audio.audio_params().is_valid() ?
										  audio.audio_params() :
										  params().audio_params();

	std::vector<const uint8_t *> channel_data(
		size_t(audio.audio_params().channel_count()));
	for (size_t i = 0; i < channel_data.size(); i++) {
		channel_data[i] =
			reinterpret_cast<const uint8_t *>(audio.data(int(i)));
	}

	int r = fb_encoder_write_audio(
		encoder_, channel_data.data(),
		audio.audio_params().channel_count(),
		FFmpegUtils::get_f_fmpeg_sample_format(audio.audio_params().format()),
		audio_params.sample_rate(), audio_params.channel_layout(),
		int64_t(audio.sample_count()));
	if (r != 0) {
		set_error_from_bridge();
		return false;
	}

	return true;
}

bool FFmpegEncoder::write_audio_data(const AudioParams &audio_params,
								   const uint8_t **data,
								   int input_sample_count)
{
	int r = fb_encoder_write_audio(
		encoder_, data, audio_params.channel_count(),
		FFmpegUtils::get_f_fmpeg_sample_format(audio_params.format()),
		audio_params.sample_rate(), audio_params.channel_layout(),
		input_sample_count);
	if (r != 0) {
		set_error_from_bridge();
		return false;
	}

	return true;
}

bool FFmpegEncoder::write_subtitle(const SubtitleBlock *sub_block)
{
	QByteArray utf8_sub = sub_block->get_text().toUtf8();

	int r = fb_encoder_write_subtitle(encoder_, utf8_sub.constData(),
									  sub_block->in().to_double(),
									  sub_block->length().to_double());
	if (r != 0) {
		set_error_from_bridge();
		return false;
	}

	return true;
}

void FFmpegEncoder::close()
{
	if (encoder_) {
		// Flushes encoders, writes the trailer, and frees everything
		fb_encoder_free(&encoder_);
	}

	open_ = false;
}

void FFmpegEncoder::set_error_from_bridge()
{
	set_error(QString::fromUtf8(fb_encoder_get_error(encoder_)));
}

int FFmpegEncoder::export_codec_to_bridge(ExportCodec::Codec c)
{
	switch (c) {
	case ExportCodec::k_codec_h264:
		return fb_codec_h264;
	case ExportCodec::k_codec_h264rgb:
		return fb_codec_h264_rgb;
	case ExportCodec::k_codec_d_nx_hd:
		return fb_codec_dnxhd;
	case ExportCodec::k_codec_pro_res:
		return fb_codec_prores;
	case ExportCodec::k_codec_cineform:
		return fb_codec_cineform;
	case ExportCodec::k_codec_h265:
		return fb_codec_h265;
	case ExportCodec::k_codec_v_p9:
		return fb_codec_v_p9;
	case ExportCodec::k_codec_a_v1:
		return fb_codec_a_v1;
	case ExportCodec::k_codec_open_exr:
		return fb_codec_openexr;
	case ExportCodec::k_codec_png:
		return fb_codec_png;
	case ExportCodec::k_codec_tiff:
		return fb_codec_tiff;
	case ExportCodec::k_codec_m_p2:
		return fb_codec_m_p2;
	case ExportCodec::k_codec_m_p3:
		return fb_codec_m_p3;
	case ExportCodec::k_codec_aac:
		return fb_codec_aac;
	case ExportCodec::k_codec_pcm:
		return fb_codec_pcm;
	case ExportCodec::k_codec_flac:
		return fb_codec_flac;
	case ExportCodec::k_codec_opus:
		return fb_codec_opus;
	case ExportCodec::k_codec_vorbis:
		return fb_codec_vorbis;
	case ExportCodec::k_codec_srt:
		return fb_codec_srt;
	case ExportCodec::k_codec_count:
		break;
	}

	return fb_codec_none;
}

}
