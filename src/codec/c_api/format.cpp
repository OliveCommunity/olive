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

#include "codec/format.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "encoder.h"
#include "ffmpeg/ffmpegencoder.h"

namespace
{

bool valid_format(int format)
{
	return format >= 0 && format < olive::ExportFormat::k_format_count;
}

bool valid_codec(int codec)
{
	return codec >= 0 && codec < olive::ExportCodec::k_codec_count;
}

// buf/size convention: returns the would-be length INCLUDING the NUL
// (include/codec/error.h), unlike the facade which excludes it.
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

} // namespace

/* ---- Container format / codec metadata ---------------------------------- */

int oakcodec_encoding_format_count(void)
{
	return olive::ExportFormat::k_format_count;
}

int oakcodec_encoding_format_name(int format, char *buf, int buf_size)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	return string_out(
		olive::ExportFormat::get_name(olive::ExportFormat::Format(format)), buf,
		buf_size);
}

int oakcodec_encoding_format_extension(int format, char *buf, int buf_size)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	return string_out(
		olive::ExportFormat::get_extension(olive::ExportFormat::Format(format)),
		buf, buf_size);
}

int oakcodec_encoding_format_video_codec_count(int format)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	return int(olive::ExportFormat::get_video_codecs(
				   olive::ExportFormat::Format(format))
				   .size());
}

int oakcodec_encoding_format_video_codec_at(int format, int index)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	const auto l =
		olive::ExportFormat::get_video_codecs(olive::ExportFormat::Format(format));
	if (index < 0 || index >= int(l.size())) {
		return OAKCODEC_E_NOT_FOUND;
	}
	return int(l[size_t(index)]);
}

int oakcodec_encoding_format_audio_codec_count(int format)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	return int(olive::ExportFormat::get_audio_codecs(
				   olive::ExportFormat::Format(format))
				   .size());
}

int oakcodec_encoding_format_audio_codec_at(int format, int index)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	const auto l =
		olive::ExportFormat::get_audio_codecs(olive::ExportFormat::Format(format));
	if (index < 0 || index >= int(l.size())) {
		return OAKCODEC_E_NOT_FOUND;
	}
	return int(l[size_t(index)]);
}

int oakcodec_encoding_format_subtitle_codec_count(int format)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	return int(olive::ExportFormat::get_subtitle_codecs(
				   olive::ExportFormat::Format(format))
				   .size());
}

int oakcodec_encoding_format_subtitle_codec_at(int format, int index)
{
	if (!valid_format(format)) {
		return OAKCODEC_E_INVALID;
	}
	const auto l = olive::ExportFormat::get_subtitle_codecs(
		olive::ExportFormat::Format(format));
	if (index < 0 || index >= int(l.size())) {
		return OAKCODEC_E_NOT_FOUND;
	}
	return int(l[size_t(index)]);
}

int oakcodec_encoding_codec_name(int codec, char *buf, int buf_size)
{
	if (!valid_codec(codec)) {
		return OAKCODEC_E_INVALID;
	}
	return string_out(
		olive::ExportCodec::get_codec_name(olive::ExportCodec::Codec(codec)), buf,
		buf_size);
}

int oakcodec_encoding_codec_is_still_image(int codec)
{
	if (!valid_codec(codec)) {
		return 0;
	}
	return olive::ExportCodec::is_codec_a_still_image(
			   olive::ExportCodec::Codec(codec)) ?
			   1 :
			   0;
}

int oakcodec_encoding_codec_is_lossless(int codec)
{
	if (!valid_codec(codec)) {
		return 0;
	}
	return olive::ExportCodec::is_codec_lossless(olive::ExportCodec::Codec(codec)) ?
			   1 :
			   0;
}

int oakcodec_encoding_pix_fmt_count(int format, int codec)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return OAKCODEC_E_INVALID;
	}
	return int(olive::ExportFormat::get_pixel_formats_for_codec(
				   olive::ExportFormat::Format(format),
				   olive::ExportCodec::Codec(codec))
				   .size());
}

int oakcodec_encoding_pix_fmt_at(int format, int codec, int index, char *buf,
								  int buf_size)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return OAKCODEC_E_INVALID;
	}
	const auto l = olive::ExportFormat::get_pixel_formats_for_codec(
		olive::ExportFormat::Format(format), olive::ExportCodec::Codec(codec));
	if (index < 0 || index >= int(l.size())) {
		return OAKCODEC_E_NOT_FOUND;
	}
	return string_out(l[size_t(index)], buf, buf_size);
}

int oakcodec_encoding_pix_fmt_index(int codec, const char *pix_fmt)
{
	if (!valid_codec(codec) || !pix_fmt || !pix_fmt[0]) {
		return 0;
	}
	// Mirrors the facade: query the FFmpeg encoder's list directly (the
	// pixel-format list depends on the codec alone, not the container).
	olive::FFmpegEncoder probe{ olive::EncodingParams() };
	const auto l =
		probe.get_pixel_formats_for_codec(olive::ExportCodec::Codec(codec));
	const std::string needle(pix_fmt);
	const auto it = std::find(l.begin(), l.end(), needle);
	return it != l.end() ? int(it - l.begin()) : 0;
}

int oakcodec_encoding_sample_format_count(int format, int codec)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return OAKCODEC_E_INVALID;
	}
	return int(olive::ExportFormat::get_sample_formats_for_codec(
				   olive::ExportFormat::Format(format),
				   olive::ExportCodec::Codec(codec))
				   .size());
}

int oakcodec_encoding_sample_format_at(int format, int codec, int index)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return OAKCODEC_E_INVALID;
	}
	const auto l = olive::ExportFormat::get_sample_formats_for_codec(
		olive::ExportFormat::Format(format), olive::ExportCodec::Codec(codec));
	if (index < 0 || index >= int(l.size())) {
		return OAKCODEC_E_NOT_FOUND;
	}
	return int(l[size_t(index)]);
}

/* ---- Image-sequence filename helpers ------------------------------------ */

int oakcodec_encoding_filename_contains_digit_placeholder(const char *filename)
{
	if (!filename) {
		return 0;
	}
	return olive::Encoder::filename_contains_digit_placeholder(filename) ? 1 :
																		   0;
}

int oakcodec_encoding_image_sequence_digit_count(const char *filename)
{
	if (!filename) {
		return 0;
	}
	return olive::Encoder::get_image_sequence_placeholder_digit_count(filename);
}

int oakcodec_encoding_filename_remove_digit_placeholder(const char *filename,
														 char *buf,
														 int buf_size)
{
	if (!filename) {
		return OAKCODEC_E_INVALID;
	}
	return string_out(
		olive::Encoder::filename_remove_digit_placeholder(filename), buf,
		buf_size);
}
