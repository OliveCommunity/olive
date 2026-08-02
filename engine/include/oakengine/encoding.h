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

#ifndef OAKENGINE_ENCODING_H
#define OAKENGINE_ENCODING_H

#include <stdint.h>

#include "export.h"
#include "timeline.h"
#include "videoparams.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file encoding.h
 * @brief C ABI for the encoding parameter surface (EncodingParams /
 * ExportFormat / ExportCodec)
 *
 * This family exposes everything the application's export dialog (and the
 * audio-recording path) needs without touching the engine's C++ classes:
 *
 * - Container/codec metadata queries (format names/extensions, codec lists
 *   per format, codec names/flags, supported pixel and sample formats).
 * - An opaque OakEngineEncodingParams handle wrapping the engine's
 *   EncodingParams: full getter/setter surface, preset path/listing and
 *   preset load/save.
 * - oakengine_export_render_with_params(): runs the same synchronous export
 *   path as oakengine_export_render_ex() (oakengine/exporter.h) using a
 *   params handle assembled through this family.
 *
 * Enum int fields carry the engine's own enum values
 * (olive::ExportFormat::Format, olive::ExportCodec::Codec,
 * olive::VideoParams::Interlacing/ColorRange, olive::PixelFormat::Format,
 * olive::core::SampleFormat::Format). Conventions match the other facade
 * families: 0 (OAKENGINE_OK) / negative OAKENGINE_E_* codes, buf/size
 * strings (return value is the would-be length excluding the NUL), NULL
 * handles are no-ops returning the documented failure value.
 */

/** @brief Opaque encoding-parameters handle (olive::EncodingParams). */
typedef struct OakEngineEncodingParams OakEngineEncodingParams;

/** @brief Scaling method values (EncodingParams::VideoScalingMethod). */
#define OAKENGINE_ENCODING_SCALING_FIT 0
#define OAKENGINE_ENCODING_SCALING_STRETCH 1
#define OAKENGINE_ENCODING_SCALING_CROP 2

/**
 * @brief Container formats (olive::ExportFormat::Format) referenced by name
 * in UI code. Only append; the values are serialized in project/preset
 * files. The complete list lives in engine/codec/exportformat.h.
 */
#define OAKENGINE_ENCODING_FORMAT_MATROSKA 1
#define OAKENGINE_ENCODING_FORMAT_MPEG4_VIDEO 2
#define OAKENGINE_ENCODING_FORMAT_QUICKTIME 4
#define OAKENGINE_ENCODING_FORMAT_PNG 5
#define OAKENGINE_ENCODING_FORMAT_WAV 7
#define OAKENGINE_ENCODING_FORMAT_SRT 13

/**
 * @brief Codecs (olive::ExportCodec::Codec) referenced by name in UI code.
 * Only append; the values are serialized. The complete list lives in
 * engine/codec/exportcodec.h.
 */
#define OAKENGINE_ENCODING_CODEC_H264 1
#define OAKENGINE_ENCODING_CODEC_H264RGB 2
#define OAKENGINE_ENCODING_CODEC_H265 3
#define OAKENGINE_ENCODING_CODEC_CINEFORM 7
#define OAKENGINE_ENCODING_CODEC_AAC 12
#define OAKENGINE_ENCODING_CODEC_PCM 13
#define OAKENGINE_ENCODING_CODEC_SRT 17
#define OAKENGINE_ENCODING_CODEC_AV1 18

/** @brief olive::VideoParams::ColorRange values. */
#define OAKENGINE_ENCODING_COLOR_RANGE_LIMITED 0
#define OAKENGINE_ENCODING_COLOR_RANGE_FULL 1

/** @brief olive::VideoParams::Interlacing values. */
#define OAKENGINE_ENCODING_INTERLACE_NONE 0
#define OAKENGINE_ENCODING_INTERLACE_TOP_FIRST 1
#define OAKENGINE_ENCODING_INTERLACE_BOTTOM_FIRST 2

/* ---- Container format / codec metadata ---------------------------------- */

/** @brief Number of container formats (olive::ExportFormat::k_format_count). */
OAKENGINE_API int oakengine_encoding_format_count(void);

/** @brief Display name of a container format (buf/size); -1 invalid. */
OAKENGINE_API int oakengine_encoding_format_name(int format, char *buf,
												 int buf_size);

/** @brief File extension (no dot) of a container format (buf/size). */
OAKENGINE_API int oakengine_encoding_format_extension(int format, char *buf,
													  int buf_size);

/**
 * @brief Number of video codecs a container format supports; -1 when the
 * format is invalid.
 */
OAKENGINE_API int oakengine_encoding_format_video_codec_count(int format);

/**
 * @brief The `index`-th video codec of `format` as an
 * olive::ExportCodec::Codec value; -1 when out of range.
 */
OAKENGINE_API int oakengine_encoding_format_video_codec_at(int format,
														   int index);

/** @brief Audio-codec variant of the two functions above. */
OAKENGINE_API int oakengine_encoding_format_audio_codec_count(int format);
OAKENGINE_API int oakengine_encoding_format_audio_codec_at(int format,
														   int index);

/** @brief Subtitle-codec variant of the two functions above. */
OAKENGINE_API int oakengine_encoding_format_subtitle_codec_count(int format);
OAKENGINE_API int oakengine_encoding_format_subtitle_codec_at(int format,
															  int index);

/** @brief Display name of a codec (buf/size); -1 when invalid. */
OAKENGINE_API int oakengine_encoding_codec_name(int codec, char *buf,
												int buf_size);

/** @brief 1 when `codec` encodes still images (PNG/TIFF/OpenEXR). */
OAKENGINE_API int oakengine_encoding_codec_is_still_image(int codec);

/** @brief 1 when `codec` is lossless (no bit-rate setting applies). */
OAKENGINE_API int oakengine_encoding_codec_is_lossless(int codec);

/**
 * @brief Number of encoded pixel formats (e.g. "yuv420p") usable with
 * `codec` inside `format`; -1 when invalid.
 */
OAKENGINE_API int oakengine_encoding_pix_fmt_count(int format, int codec);

/** @brief The `index`-th encoded pixel format name (buf/size). */
OAKENGINE_API int oakengine_encoding_pix_fmt_at(int format, int codec,
												int index, char *buf,
												int buf_size);

/**
 * @brief Index of `pix_fmt` (e.g. "yuv420p") in `codec`'s supported pixel
 * format list; 0 (the codec's preferred format) when absent or `pix_fmt` is
 * NULL/empty.
 */
OAKENGINE_API int oakengine_encoding_pix_fmt_index(int codec,
												   const char *pix_fmt);

/**
 * @brief Number of sample formats usable with `codec` inside `format`;
 * -1 when invalid.
 */
OAKENGINE_API int oakengine_encoding_sample_format_count(int format,
														 int codec);

/**
 * @brief The `index`-th sample format as an olive::core::SampleFormat::Format
 * value; -1 when out of range.
 */
OAKENGINE_API int oakengine_encoding_sample_format_at(int format, int codec,
													  int index);

/* ---- Image-sequence filename helpers (olive::Encoder statics) ----------- */

/** @brief 1 when `filename` contains a "[#####]" digit placeholder. */
OAKENGINE_API int
oakengine_encoding_filename_contains_digit_placeholder(const char *filename);

/**
 * @brief Digit count of the filename's "[#####]" placeholder; 0 when none.
 */
OAKENGINE_API int
oakengine_encoding_image_sequence_digit_count(const char *filename);

/** @brief `filename` with the digit placeholder removed (buf/size). */
OAKENGINE_API int
oakengine_encoding_filename_remove_digit_placeholder(const char *filename,
													 char *buf, int buf_size);

/**
 * @brief Fit/stretch/crop transform matrix
 * (EncodingParams::generate_matrix()).
 *
 * Writes the 16 floats of the column-major 4x4 matrix to `out16`
 * (QMatrix4x4 layout). `method` is OAKENGINE_ENCODING_SCALING_*.
 *
 * @return OAKENGINE_OK, or OAKENGINE_E_INVALID for bad arguments.
 */
OAKENGINE_API int oakengine_encoding_generate_matrix(int method, int src_width,
													 int src_height,
													 int dest_width,
													 int dest_height,
													 float out16[16]);

/* ---- Encoding parameters handle ----------------------------------------- */

/**
 * @brief Create an empty encoding-parameters handle (all tracks disabled,
 * format unset). Destroy with oakengine_encoding_params_destroy().
 */
OAKENGINE_API OakEngineEncodingParams *oakengine_encoding_params_create(void);

/** @brief Destroy a handle created by oakengine_encoding_params_create(). */
OAKENGINE_API void
oakengine_encoding_params_destroy(OakEngineEncodingParams *params);

/**
 * @brief 1 when at least one of video/audio/subtitles is enabled
 * (EncodingParams::is_valid()).
 */
OAKENGINE_API int
oakengine_encoding_params_is_valid(const OakEngineEncodingParams *params);

/** @brief Output filename (buf/size convention). */
OAKENGINE_API int
oakengine_encoding_params_set_filename(OakEngineEncodingParams *params,
									   const char *filename);
OAKENGINE_API int
oakengine_encoding_params_filename(const OakEngineEncodingParams *params,
								   char *buf, int buf_size);

/**
 * @brief Container format as olive::ExportFormat::Format; the getter returns
 * -1 when unset. The setter rejects out-of-range values with
 * OAKENGINE_E_INVALID.
 */
OAKENGINE_API int
oakengine_encoding_params_set_format(OakEngineEncodingParams *params,
									 int format);
OAKENGINE_API int
oakengine_encoding_params_format(const OakEngineEncodingParams *params);

/**
 * @brief Enable video with the given parameters and codec
 * (EncodingParams::enable_video()).
 */
OAKENGINE_API int
oakengine_encoding_params_enable_video(OakEngineEncodingParams *params,
									   const oak_video_params *video,
									   int codec);

/**
 * @brief Enable audio (EncodingParams::enable_audio()). `sample_format` is
 * an olive::core::SampleFormat::Format value.
 */
OAKENGINE_API int
oakengine_encoding_params_enable_audio(OakEngineEncodingParams *params,
									   int sample_rate,
									   uint64_t channel_layout,
									   int sample_format, int codec);

/** @brief Enable embedded subtitles. */
OAKENGINE_API int
oakengine_encoding_params_enable_subtitles(OakEngineEncodingParams *params,
										   int codec);

/** @brief Enable sidecar subtitles with the given sidecar container. */
OAKENGINE_API int oakengine_encoding_params_enable_sidecar_subtitles(
	OakEngineEncodingParams *params, int format, int codec);

OAKENGINE_API void
oakengine_encoding_params_disable_video(OakEngineEncodingParams *params);
OAKENGINE_API void
oakengine_encoding_params_disable_audio(OakEngineEncodingParams *params);
OAKENGINE_API void
oakengine_encoding_params_disable_subtitles(OakEngineEncodingParams *params);

OAKENGINE_API int
oakengine_encoding_params_video_enabled(const OakEngineEncodingParams *params);
OAKENGINE_API int
oakengine_encoding_params_video_codec(const OakEngineEncodingParams *params);

/**
 * @brief Read back the video parameters (any field may be NULL);
 * OAKENGINE_E_STATE when video is disabled.
 */
OAKENGINE_API int oakengine_encoding_params_get_video_params(
	const OakEngineEncodingParams *params, oak_video_params *out);

OAKENGINE_API int
oakengine_encoding_params_audio_enabled(const OakEngineEncodingParams *params);
OAKENGINE_API int
oakengine_encoding_params_audio_codec(const OakEngineEncodingParams *params);

/**
 * @brief Read back the audio parameters (any field may be NULL);
 * OAKENGINE_E_STATE when audio is disabled.
 */
OAKENGINE_API int oakengine_encoding_params_get_audio_params(
	const OakEngineEncodingParams *params, int *sample_rate,
	uint64_t *channel_layout, int *sample_format);

OAKENGINE_API int oakengine_encoding_params_subtitles_enabled(
	const OakEngineEncodingParams *params);
OAKENGINE_API int oakengine_encoding_params_subtitles_are_sidecar(
	const OakEngineEncodingParams *params);
OAKENGINE_API int oakengine_encoding_params_subtitles_sidecar_format(
	const OakEngineEncodingParams *params);
OAKENGINE_API int oakengine_encoding_params_subtitles_codec(
	const OakEngineEncodingParams *params);

/** @brief Video bit rates / buffer size (bit/s, bytes). */
OAKENGINE_API void
oakengine_encoding_params_set_video_bit_rate(OakEngineEncodingParams *params,
											 int64_t rate);
OAKENGINE_API int64_t
oakengine_encoding_params_video_bit_rate(const OakEngineEncodingParams *params);
OAKENGINE_API void
oakengine_encoding_params_set_video_min_bit_rate(
	OakEngineEncodingParams *params, int64_t rate);
OAKENGINE_API int64_t oakengine_encoding_params_video_min_bit_rate(
	const OakEngineEncodingParams *params);
OAKENGINE_API void
oakengine_encoding_params_set_video_max_bit_rate(
	OakEngineEncodingParams *params, int64_t rate);
OAKENGINE_API int64_t oakengine_encoding_params_video_max_bit_rate(
	const OakEngineEncodingParams *params);
OAKENGINE_API void
oakengine_encoding_params_set_video_buffer_size(
	OakEngineEncodingParams *params, int64_t size);
OAKENGINE_API int64_t oakengine_encoding_params_video_buffer_size(
	const OakEngineEncodingParams *params);

/** @brief Encoder thread count (0 = auto). */
OAKENGINE_API void
oakengine_encoding_params_set_video_threads(OakEngineEncodingParams *params,
											int threads);
OAKENGINE_API int
oakengine_encoding_params_video_threads(const OakEngineEncodingParams *params);

/** @brief Audio bit rate (bit/s). */
OAKENGINE_API void
oakengine_encoding_params_set_audio_bit_rate(OakEngineEncodingParams *params,
											 int64_t rate);
OAKENGINE_API int64_t
oakengine_encoding_params_audio_bit_rate(const OakEngineEncodingParams *params);

/** @brief Encoded pixel format name (e.g. "yuv420p"; buf/size getter). */
OAKENGINE_API int
oakengine_encoding_params_set_video_pix_fmt(OakEngineEncodingParams *params,
											const char *pix_fmt);
OAKENGINE_API int
oakengine_encoding_params_video_pix_fmt(
	const OakEngineEncodingParams *params, char *buf, int buf_size);

/** @brief Image-sequence flag (0/1). */
OAKENGINE_API void
oakengine_encoding_params_set_video_is_image_sequence(
	OakEngineEncodingParams *params, int is_image_sequence);
OAKENGINE_API int oakengine_encoding_params_video_is_image_sequence(
	const OakEngineEncodingParams *params);

/**
 * @brief Output color transform by OCIO color space name; an empty/NULL
 * name selects the reference space (no transform).
 */
OAKENGINE_API int oakengine_encoding_params_set_color_transform(
	OakEngineEncodingParams *params, const char *output_name);
OAKENGINE_API int oakengine_encoding_params_color_transform_output(
	const OakEngineEncodingParams *params, char *buf, int buf_size);

/** @brief Export length as rational seconds. */
OAKENGINE_API void
oakengine_encoding_params_set_export_length(OakEngineEncodingParams *params,
											int num, int den);
OAKENGINE_API int
oakengine_encoding_params_get_export_length(
	const OakEngineEncodingParams *params, int *num, int *den);

/**
 * @brief Custom export range as rational seconds [in, out). The getter
 * returns OAKENGINE_E_NOT_FOUND when no custom range is set.
 */
OAKENGINE_API void
oakengine_encoding_params_set_custom_range(OakEngineEncodingParams *params,
										   int64_t in_num, int64_t in_den,
										   int64_t out_num, int64_t out_den);
OAKENGINE_API int
oakengine_encoding_params_has_custom_range(
	const OakEngineEncodingParams *params);
OAKENGINE_API int
oakengine_encoding_params_get_custom_range(
	const OakEngineEncodingParams *params, int64_t *in_num, int64_t *in_den,
	int64_t *out_num, int64_t *out_den);

/** @brief Scaling method (OAKENGINE_ENCODING_SCALING_*). */
OAKENGINE_API int
oakengine_encoding_params_set_video_scaling_method(
	OakEngineEncodingParams *params, int method);
OAKENGINE_API int oakengine_encoding_params_video_scaling_method(
	const OakEngineEncodingParams *params);

/**
 * @brief Encoder-specific video option (key/value strings, e.g. "crf" =
 * "18"); mirrors EncodingParams::set_video_option(). The getter returns the
 * would-be length (buf/size) or OAKENGINE_E_NOT_FOUND when the key is unset.
 */
OAKENGINE_API int
oakengine_encoding_params_set_video_option(OakEngineEncodingParams *params,
										   const char *key, const char *value);
OAKENGINE_API int
oakengine_encoding_params_video_option(const OakEngineEncodingParams *params,
									   const char *key, char *buf,
									   int buf_size);

/* ---- Presets ------------------------------------------------------------- */

/** @brief Directory where export presets live (buf/size). */
OAKENGINE_API int oakengine_encoding_preset_path(char *buf, int buf_size);

/** @brief Number of saved presets. */
OAKENGINE_API int oakengine_encoding_preset_count(void);

/** @brief Name of the `index`-th preset (buf/size); -1 when out of range. */
OAKENGINE_API int oakengine_encoding_preset_name(int index, char *buf,
												 int buf_size);

/**
 * @brief Load parameters from a preset/XML file (overwrites the handle's
 * contents on success).
 *
 * @return OAKENGINE_OK, OAKENGINE_E_INVALID for bad arguments, or
 * OAKENGINE_E_FAILED when the file cannot be read or parsed.
 */
OAKENGINE_API int
oakengine_encoding_params_load_file(OakEngineEncodingParams *params,
									const char *path);

/** @brief Save parameters to a preset/XML file (same return convention). */
OAKENGINE_API int
oakengine_encoding_params_save_file(const OakEngineEncodingParams *params,
									const char *path);

/* ---- Export execution / per-sequence last-used --------------------------- */

/**
 * @brief Run a synchronous offline export with a params handle assembled
 * through this family.
 *
 * Same blocking/progress/cancel semantics as oakengine_export_render_ex()
 * (oakengine/exporter.h): progress via
 * oakengine_export_set_progress_callback(), cancellation via
 * oakengine_export_cancel(), failure reason via
 * oakengine_export_last_error(). The output filename and image-sequence
 * template come from the handle itself.
 *
 * @return OAKENGINE_OK / OAKENGINE_E_INVALID / OAKENGINE_E_STATE /
 * OAKENGINE_E_FAILED / OAKENGINE_E_CANCELLED.
 */
OAKENGINE_API int
oakengine_export_render_with_params(OakEngineSequence *seq,
									const OakEngineEncodingParams *params);

/**
 * @brief Copy of the sequence's last-used encoding parameters
 * (ViewerOutput::get_last_used_encoding_params()), or NULL when none is
 * valid. Caller destroys with oakengine_encoding_params_destroy().
 */
OAKENGINE_API OakEngineEncodingParams *
oakengine_encoding_params_get_last_used(OakEngineSequence *seq);

/**
 * @brief Store `params` as the sequence's last-used encoding parameters
 * (ViewerOutput::set_last_used_encoding_params()); NULL is a no-op.
 */
OAKENGINE_API void oakengine_encoding_params_set_last_used(
	OakEngineSequence *seq, const OakEngineEncodingParams *params);

/**
 * @brief Start audio recording to the file described by `params`
 * (AudioManager::start_recording(); audio must be enabled on the handle).
 *
 * @return OAKENGINE_OK on success; OAKENGINE_E_INVALID for bad arguments;
 * OAKENGINE_E_STATE when the audio manager is not running;
 * OAKENGINE_E_FAILED otherwise (a human-readable reason is written to
 * `errbuf`/`errbuf_size` when given).
 */
OAKENGINE_API int
oakengine_encoding_start_audio_recording(const OakEngineEncodingParams *params,
										 char *errbuf, int errbuf_size);

#ifdef __cplusplus
}
#include <QtCore/qmetatype.h>
Q_DECLARE_OPAQUE_POINTER(OakEngineEncodingParams *)
#endif

#endif /* OAKENGINE_ENCODING_H */
