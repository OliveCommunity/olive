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

#ifndef OAK_EDITOR_CODEC_FORMAT_H
#define OAK_EDITOR_CODEC_FORMAT_H

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file format.h
 * @brief C ABI for the oakcodec container-format / codec metadata queries
 *        (olive::ExportFormat / olive::ExportCodec / olive::Encoder statics).
 *
 * This family is the module-side mirror of the facade's
 * oakengine_encoding_format_* / codec_* surface (oakengine/encoding.h):
 * the export dialog queries it to populate its format/codec combo boxes and
 * to enable/disable the bit-rate controls. The functions are stateless —
 * no handles involved.
 *
 * Enum int fields carry the engine's own enum values
 * (olive::ExportFormat::Format, olive::ExportCodec::Codec,
 * olive::core::SampleFormat::Format) — the same values oakengine/encoding.h
 * documents. Return-code convention follows include/codec/error.h: 0
 * (OAKCODEC_OK) on success, a negative OAKCODEC_E_* code on failure, and
 * string getters return the required buffer size in bytes INCLUDING the
 * terminating NUL as a non-negative value (two-stage convention). Note this
 * differs from oakcodec_export_format_get_extension() (encoder.h), which
 * predates this family and reports unknown formats as the empty string.
 */

/**
 * @brief Container formats (olive::ExportFormat::Format) referenced by name
 * in UI code. Only append; the values are serialized in project/preset
 * files. The complete list lives in src/codec/src/exportformat.h.
 */
#define OAKCODEC_ENCODING_FORMAT_MATROSKA 1
#define OAKCODEC_ENCODING_FORMAT_MPEG4_VIDEO 2
#define OAKCODEC_ENCODING_FORMAT_QUICKTIME 4
#define OAKCODEC_ENCODING_FORMAT_PNG 5
#define OAKCODEC_ENCODING_FORMAT_WAV 7
#define OAKCODEC_ENCODING_FORMAT_SRT 13

/**
 * @brief Codecs (olive::ExportCodec::Codec) referenced by name in UI code.
 * Only append; the values are serialized. The complete list lives in
 * src/codec/src/exportcodec.h.
 */
#define OAKCODEC_ENCODING_CODEC_H264 1
#define OAKCODEC_ENCODING_CODEC_H264RGB 2
#define OAKCODEC_ENCODING_CODEC_H265 3
#define OAKCODEC_ENCODING_CODEC_CINEFORM 7
#define OAKCODEC_ENCODING_CODEC_AAC 12
#define OAKCODEC_ENCODING_CODEC_PCM 13
#define OAKCODEC_ENCODING_CODEC_SRT 17
#define OAKCODEC_ENCODING_CODEC_AV1 18

/* ---- Container format / codec metadata ---------------------------------- */

/**
 * @brief Number of container formats (olive::ExportFormat::k_format_count).
 */
OAKCODEC_API int oakcodec_encoding_format_count(void);

/**
 * @brief Display name of a container format (buf/size, two-stage).
 *
 * @return The required buffer size (including the NUL), or
 *         OAKCODEC_E_INVALID when `format` is out of range.
 */
OAKCODEC_API int oakcodec_encoding_format_name(int format, char *buf,
												int buf_size);

/**
 * @brief File extension (no dot) of a container format (buf/size,
 *        two-stage); same return convention as
 *        oakcodec_encoding_format_name().
 */
OAKCODEC_API int oakcodec_encoding_format_extension(int format, char *buf,
													 int buf_size);

/**
 * @brief Number of video codecs a container format supports, or
 *        OAKCODEC_E_INVALID when the format is invalid.
 */
OAKCODEC_API int oakcodec_encoding_format_video_codec_count(int format);

/**
 * @brief The `index`-th video codec of `format` as an
 *        olive::ExportCodec::Codec value.
 *
 * @return OAKCODEC_E_INVALID when the format is invalid, or
 *         OAKCODEC_E_NOT_FOUND when the index is out of range.
 */
OAKCODEC_API int oakcodec_encoding_format_video_codec_at(int format,
														  int index);

/** @brief Audio-codec variant of the two functions above. */
OAKCODEC_API int oakcodec_encoding_format_audio_codec_count(int format);
OAKCODEC_API int oakcodec_encoding_format_audio_codec_at(int format,
														  int index);

/** @brief Subtitle-codec variant of the two functions above. */
OAKCODEC_API int oakcodec_encoding_format_subtitle_codec_count(int format);
OAKCODEC_API int oakcodec_encoding_format_subtitle_codec_at(int format,
															int index);

/**
 * @brief Display name of a codec (buf/size, two-stage).
 *
 * @return The required buffer size (including the NUL), or
 *         OAKCODEC_E_INVALID when `codec` is out of range.
 */
OAKCODEC_API int oakcodec_encoding_codec_name(int codec, char *buf,
											  int buf_size);

/** @brief 1 when `codec` encodes still images (PNG/TIFF/OpenEXR), else 0
 *         (0 also for an invalid codec). */
OAKCODEC_API int oakcodec_encoding_codec_is_still_image(int codec);

/** @brief 1 when `codec` is lossless (no bit-rate setting applies), else 0
 *         (0 also for an invalid codec). */
OAKCODEC_API int oakcodec_encoding_codec_is_lossless(int codec);

/**
 * @brief Number of encoded pixel formats (e.g. "yuv420p") usable with
 *        `codec` inside `format`, or OAKCODEC_E_INVALID when either
 *        argument is out of range. The list is queried from the format's
 *        encoder (FFmpeg/OIIO), so codecs without an encoder report 0.
 */
OAKCODEC_API int oakcodec_encoding_pix_fmt_count(int format, int codec);

/**
 * @brief The `index`-th encoded pixel format name (buf/size, two-stage).
 *
 * @return The required buffer size (including the NUL), or
 *         OAKCODEC_E_INVALID for bad format/codec, or
 *         OAKCODEC_E_NOT_FOUND when the index is out of range.
 */
OAKCODEC_API int oakcodec_encoding_pix_fmt_at(int format, int codec,
											  int index, char *buf,
											  int buf_size);

/**
 * @brief Index of `pix_fmt` (e.g. "yuv420p") in `codec`'s supported pixel
 *        format list; 0 (the codec's preferred format) when absent or
 *        `pix_fmt` is NULL/empty or `codec` is invalid.
 */
OAKCODEC_API int oakcodec_encoding_pix_fmt_index(int codec,
												 const char *pix_fmt);

/**
 * @brief Number of sample formats usable with `codec` inside `format`, or
 *        OAKCODEC_E_INVALID when either argument is out of range.
 */
OAKCODEC_API int oakcodec_encoding_sample_format_count(int format,
													   int codec);

/**
 * @brief The `index`-th sample format as an olive::core::SampleFormat::Format
 *        value.
 *
 * @return OAKCODEC_E_INVALID for bad format/codec, or
 *         OAKCODEC_E_NOT_FOUND when the index is out of range.
 */
OAKCODEC_API int oakcodec_encoding_sample_format_at(int format, int codec,
													int index);

/* ---- Image-sequence filename helpers (olive::Encoder statics) ----------- */

/** @brief 1 when `filename` contains a "[#####]" digit placeholder, else 0
 *         (0 for NULL). */
OAKCODEC_API int
oakcodec_encoding_filename_contains_digit_placeholder(const char *filename);

/** @brief Digit count of the filename's "[#####]" placeholder; 0 when none
 *         (0 for NULL). */
OAKCODEC_API int
oakcodec_encoding_image_sequence_digit_count(const char *filename);

/**
 * @brief `filename` with the digit placeholder removed (buf/size, two-stage;
 *        a leading separator like "_"/"-"/"."/" " before the placeholder is
 *        removed along with it).
 *
 * @return The required buffer size (including the NUL), or
 *         OAKCODEC_E_INVALID when `filename` is NULL.
 */
OAKCODEC_API int
oakcodec_encoding_filename_remove_digit_placeholder(const char *filename,
													char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_CODEC_FORMAT_H
