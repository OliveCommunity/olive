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

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/ffmpegutils.h"
#include "common/subtitleparams.h"
#include "common/videoparams.h"

namespace olive
{

namespace
{

std::string to_lower(const std::string &s)
{
	std::string r = s;
	std::transform(r.begin(), r.end(), r.begin(),
				   [](unsigned char c) { return char(std::tolower(c)); });
	return r;
}

bool contains(const std::string &haystack, const std::string &needle)
{
	return haystack.find(needle) != std::string::npos;
}

Rational video_params_pixel_aspect_ratio(OakVideoParams vp)
{
	int n = 0, d = 1;
	oakcommon_videoparams_get_pixel_aspect_ratio(vp, &n, &d);
	return Rational(n, d);
}

Rational video_params_frame_rate_as_time_base(OakVideoParams vp)
{
	int n = 0, d = 1;
	oakcommon_videoparams_frame_rate_as_time_base(vp, &n, &d);
	return Rational(n, d);
}

std::string colortransform_get_output(OakColorTransform ct)
{
	std::string result;
	int size = oakcommon_colortransform_get_output(ct, nullptr, 0);
	if (size > 0) {
		result.assign(size_t(size) - 1, '\0');
		oakcommon_colortransform_get_output(ct, result.data(), size);
	}
	return result;
}

} // namespace

FFmpegEncoder::FFmpegEncoder(const EncodingParams &params)
	: Encoder(params)
	, encoder_(nullptr)
	, open_(false)
{
}

bool FFmpegEncoder::get_color_tags_for_colorspace(const std::string &colorspace,
												  int *primaries, int *trc,
												  int *matrix)
{
	const std::string name = to_lower(colorspace);

	if (contains(name, "pq") || contains(name, "2084")) {
		*primaries = fb_color_primaries_bt2020;
		*trc = fb_color_trc_pq;
		*matrix = fb_col_spc_b_t2020_ncl;
		return true;
	}

	if (contains(name, "hlg")) {
		*primaries = fb_color_primaries_bt2020;
		*trc = fb_color_trc_hlg;
		*matrix = fb_col_spc_b_t2020_ncl;
		return true;
	}

	if (contains(name, "2020")) {
		*primaries = fb_color_primaries_bt2020;
		*trc = fb_color_trc_bt709;
		*matrix = fb_col_spc_b_t2020_ncl;
		return true;
	}

	if (contains(name, "p3")) {
		*primaries = fb_color_primaries_smpte432;
		*trc = fb_color_trc_srgb;
		*matrix = fb_col_spc_b_t709;
		return true;
	}

	if (contains(name, "srgb")) {
		*primaries = fb_color_primaries_bt709;
		*trc = fb_color_trc_srgb;
		*matrix = fb_col_spc_b_t709;
		return true;
	}

	if (contains(name, "pal")) {
		*primaries = fb_color_primaries_bt470bg;
		*trc = fb_color_trc_gamma28;
		*matrix = fb_col_spc_b_t470_bg;
		return true;
	}

	if (contains(name, "ntsc")) {
		*primaries = fb_color_primaries_smpte170m;
		*trc = fb_color_trc_smpte170m;
		*matrix = fb_col_spc_smpt_e170_m;
		return true;
	}

	if (contains(name, "1886") || contains(name, "709")) {
		*primaries = fb_color_primaries_bt709;
		*trc = fb_color_trc_bt709;
		*matrix = fb_col_spc_b_t709;
		return true;
	}

	return false;
}

std::vector<std::string>
FFmpegEncoder::get_pixel_formats_for_codec(ExportCodec::Codec c) const
{
	std::vector<std::string> pix_fmts;

	int bridge_codec = export_codec_to_bridge(c);
	if (bridge_codec != fb_codec_none) {
		int count =
			fb_encoder_codec_get_pixel_formats(bridge_codec, nullptr, 0);
		if (count > 0) {
			std::vector<const char *> names(static_cast<size_t>(count));
			fb_encoder_codec_get_pixel_formats(bridge_codec, names.data(),
											   count);
			for (int i = 0; i < count; i++) {
				pix_fmts.push_back(names[size_t(i)] ? names[size_t(i)] : "");
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
					int native = -1;
					oakcommon_ffmpegutils_get_native_sample_format(fmt,
																   &native);
					if (native != SampleFormat::invalid) {
						f.push_back(
							SampleFormat(static_cast<SampleFormat::Format>(
								native)));
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

	FBEncoderConfig config;
	memset(&config, 0, sizeof(config));
	config.filename = params().filename().c_str();

	// Storage keeping C strings alive until fb_encoder_create deep-copies them
	std::string pix_fmt_str;
	std::string subtitle_header;
	std::vector<std::string> opt_key_storage;
	std::vector<std::string> opt_value_storage;
	std::vector<const char *> opt_keys;
	std::vector<const char *> opt_values;

	// Set up video if it's enabled
	if (params().video_enabled()) {
		const OakVideoParams &vp = params().video_params();

		config.video_enabled = 1;
		config.video_codec = export_codec_to_bridge(params().video_codec());
		oakcommon_videoparams_get_width(vp, &config.video_width);
		oakcommon_videoparams_get_height(vp, &config.video_height);
		Rational pixel_aspect = video_params_pixel_aspect_ratio(vp);
		config.video_pixel_aspect_num = pixel_aspect.numerator();
		config.video_pixel_aspect_den = pixel_aspect.denominator();
		Rational time_base = video_params_frame_rate_as_time_base(vp);
		config.video_time_base_num = time_base.numerator();
		config.video_time_base_den = time_base.denominator();
		int frame_rate_num = 0, frame_rate_den = 1;
		oakcommon_videoparams_get_frame_rate(vp, &frame_rate_num,
											 &frame_rate_den);
		config.video_frame_rate_num = frame_rate_num;
		config.video_frame_rate_den = frame_rate_den;

		pix_fmt_str = params().video_pix_fmt();
		config.video_pix_fmt = pix_fmt_str.c_str();

		// This is the format we will expect frames received in Write() to be in
		int native_pixel_fmt = -1;
		oakcommon_videoparams_get_format(vp, &native_pixel_fmt);

		// This is the format we will need to convert the frame to for the bridge to understand it
		int compatible_fmt = -1;
		oakcommon_ffmpegutils_get_compatible_pixel_format(native_pixel_fmt,
														  &compatible_fmt);
		video_conversion_fmt_ =
			PixelFormat(static_cast<PixelFormat::Format>(compatible_fmt));

		// These are the equivalent pixel formats as bridge pixel formats
		int src_alpha_pix_fmt = fb_pix_fmt_none;
		int src_noalpha_pix_fmt = fb_pix_fmt_none;
		oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			compatible_fmt, OAKCOMMON_RGBA_CHANNEL_COUNT, &src_alpha_pix_fmt);
		oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
			compatible_fmt, OAKCOMMON_RGB_CHANNEL_COUNT, &src_noalpha_pix_fmt);

		if (src_alpha_pix_fmt == fb_pix_fmt_none ||
			src_noalpha_pix_fmt == fb_pix_fmt_none) {
			set_error("Failed to find suitable pixel format for this buffer");
			return false;
		}

		config.video_src_pix_fmt = src_alpha_pix_fmt;

		int color_range = OAKCOMMON_COLOR_RANGE_LIMITED;
		oakcommon_videoparams_get_color_range(vp, &color_range);
		config.video_color_range = color_range == OAKCOMMON_COLOR_RANGE_FULL ?
									   fb_color_range_jpeg :
									   fb_color_range_mpeg;

		int interlacing = OAKCOMMON_VIDEO_INTERLACE_NONE;
		oakcommon_videoparams_get_interlacing(vp, &interlacing);
		switch (interlacing) {
		case OAKCOMMON_VIDEO_INTERLACED_TOP_FIRST:
			config.video_field_order = fb_field_order_tt;
			break;
		case OAKCOMMON_VIDEO_INTERLACED_BOTTOM_FIRST:
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

		const std::string color_output =
			colortransform_get_output(params().color_transform());
		config.video_color_srgb =
			contains(to_lower(color_output), "srgb") ? 1 : 0;

		// Derive explicit nclc tags (HDR etc.) from the export colorspace;
		// the bridge falls back to the legacy sRGB/Rec.709 logic when these
		// are unspecified
		int color_primaries = fb_color_primaries_unspec;
		int color_trc = fb_color_trc_unspec;
		int color_matrix = fb_col_spc_unspec;
		get_color_tags_for_colorspace(color_output, &color_primaries,
									  &color_trc, &color_matrix);
		config.video_color_primaries = color_primaries;
		config.video_color_trc = color_trc;
		config.video_colorspace = color_matrix;

		// Custom options (skip Olive-internal keys)
		for (const auto &opt : params().video_opts()) {
			if (opt.first.compare(0, 4, "ove_") != 0) {
				opt_key_storage.push_back(opt.first);
				opt_value_storage.push_back(opt.second);
			}
		}
		for (size_t i = 0; i < opt_key_storage.size(); i++) {
			opt_keys.push_back(opt_key_storage[i].c_str());
			opt_values.push_back(opt_value_storage[i].c_str());
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
		int audio_sample_fmt = -1;
		oakcommon_ffmpegutils_get_ffmpeg_sample_format(
			static_cast<int>(params().audio_params().format()),
			&audio_sample_fmt);
		config.audio_sample_format = audio_sample_fmt;
		config.audio_bit_rate = params().audio_bit_rate();
	}

	// Set up subtitles if they're enabled
	if (params().subtitles_enabled()) {
		config.subtitles_enabled = 1;
		config.subtitle_codec = export_codec_to_bridge(params().subtitles_codec());
		int header_size =
			oakcommon_subtitleparams_generate_ass_header(nullptr, 0);
		if (header_size > 0) {
			subtitle_header.assign(size_t(header_size) - 1, '\0');
			oakcommon_subtitleparams_generate_ass_header(
				subtitle_header.data(), header_size);
		}
		config.subtitle_header =
			reinterpret_cast<const uint8_t *>(subtitle_header.data());
		config.subtitle_header_size = int(subtitle_header.size());
	}

	encoder_ = fb_encoder_create(&config);
	if (!encoder_) {
		set_error("Failed to create encoder");
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
	// The render worker pool finishes tickets without a result when no
	// worker is available (or the worker crashed); a null frame must fail
	// the encode cleanly instead of crashing the export task.
	if (!frame) {
		fprintf(stderr,
				"FFmpegEncoder::write_frame called with null frame\n");
		return false;
	}

	// We may need to convert this frame to a frame that the bridge will understand
	if (frame->format() != static_cast<int>(video_conversion_fmt_)) {
		frame = frame->convert(static_cast<int>(video_conversion_fmt_));
	}

	int src_pix_fmt = fb_pix_fmt_none;
	oakcommon_ffmpegutils_get_ffmpeg_pixel_format(
		frame->format(), frame->channel_count(), &src_pix_fmt);

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

	int sample_fmt = -1;
	oakcommon_ffmpegutils_get_ffmpeg_sample_format(
		static_cast<int>(audio.audio_params().format()), &sample_fmt);

	int r = fb_encoder_write_audio(
		encoder_, channel_data.data(),
		audio.audio_params().channel_count(), sample_fmt,
		audio_params.sample_rate(), int64_t(audio_params.channel_layout()),
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
	int sample_fmt = -1;
	oakcommon_ffmpegutils_get_ffmpeg_sample_format(
		static_cast<int>(audio_params.format()), &sample_fmt);

	int r = fb_encoder_write_audio(
		encoder_, data, audio_params.channel_count(), sample_fmt,
		audio_params.sample_rate(), int64_t(audio_params.channel_layout()),
		input_sample_count);
	if (r != 0) {
		set_error_from_bridge();
		return false;
	}

	return true;
}

bool FFmpegEncoder::write_subtitle(const char *text, double in_seconds,
								   double out_seconds)
{
	int r = fb_encoder_write_subtitle(encoder_, text, in_seconds, out_seconds);
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
	const char *err = fb_encoder_get_error(encoder_);
	set_error(err ? err : "");
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
