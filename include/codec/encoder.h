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

#ifndef OAK_EDITOR_CODEC_ENCODER_H
#define OAK_EDITOR_CODEC_ENCODER_H

#include <stdint.h>

#include "error.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file encoder.h
 * @brief C ABI for oakcodec media encoders (olive::Encoder and its
 *        FFmpeg/OIIO implementations).
 *
 * Handles follow the neutral by-value convention documented in frame.h.
 * The workflow is: fill an oakcodec_encoding_params POD (all fields,
 * zeroed = disabled) -> oakcodec_encoder_init() ->
 * oakcodec_encoder_open() -> oakcodec_encoder_write_*() ->
 * oakcodec_encoder_flush(). Encoder-specific options
 * (e.g. "crf" = "18") go through oakcodec_encoder_set_video_option()
 * between init and open.
 *
 * Enum int fields carry the engine's own enum values
 * (olive::ExportFormat::Format, olive::ExportCodec::Codec,
 * OakPixelFormat, olive::VideoParams::Interlacing,
 * olive::core::SampleFormat::Format) — the same values
 * oakengine/encoding.h documents.
 */

typedef struct OakEncoder {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCODEC_ABI_VERSION. */
} OakEncoder;

/** @brief olive::VideoParams::Interlacing values. */
#define OAKCODEC_INTERLACE_NONE 0
#define OAKCODEC_INTERLACE_TOP_FIRST 1
#define OAKCODEC_INTERLACE_BOTTOM_FIRST 2

/** @brief EncodingParams::VideoScalingMethod values. */
#define OAKCODEC_ENCODING_SCALING_FIT 0
#define OAKCODEC_ENCODING_SCALING_STRETCH 1
#define OAKCODEC_ENCODING_SCALING_CROP 2

/**
 * @brief Flattened encoding parameters (olive::EncodingParams).
 *
 * A zeroed struct describes an all-tracks-disabled configuration. The
 * filename (and image-sequence "[#####]" template when
 * video_is_image_sequence is set) lives in `filename`.
 * video_time_base_* is the frame duration (frame rate flipped), matching
 * oak_video_params' convention.
 */
typedef struct oakcodec_encoding_params {
	char filename[1024];
	int format; /**< olive::ExportFormat::Format. */

	int video_enabled; /**< 1/0. */
	int video_codec; /**< olive::ExportCodec::Codec. */
	int video_width;
	int video_height;
	int video_time_base_num; /**< Frame duration numerator. */
	int video_time_base_den;
	int video_pixel_format; /**< OakPixelFormat (delivery format). */
	int video_interlacing; /**< OAKCODEC_INTERLACE_*. */
	int video_pixel_aspect_num;
	int video_pixel_aspect_den;
	int64_t video_bit_rate; /**< bit/s, 0 = codec default. */
	int64_t video_min_bit_rate;
	int64_t video_max_bit_rate;
	int64_t video_buffer_size; /**< bytes. */
	int video_threads; /**< 0 = auto. */
	char video_pix_fmt[64]; /**< Encoded pixel format name ("yuv420p"). */
	int video_is_image_sequence; /**< 1/0. */
	int video_scaling_method; /**< OAKCODEC_ENCODING_SCALING_*. */

	int audio_enabled; /**< 1/0. */
	int audio_codec; /**< olive::ExportCodec::Codec. */
	int audio_sample_rate;
	uint64_t audio_channel_layout; /**< ffmpeg-style channel mask. */
	int audio_sample_format; /**< olive::core::SampleFormat::Format. */
	int64_t audio_bit_rate; /**< bit/s. */

	int subtitles_enabled; /**< 1/0. */
	int subtitles_codec; /**< olive::ExportCodec::Codec. */
	int subtitles_are_sidecar; /**< 1/0. */
	int subtitles_sidecar_format; /**< olive::ExportFormat::Format. */

	/** Output OCIO colorspace name; empty = reference space (no transform). */
	char color_transform_output[256];

	int export_length_num; /**< Export length in seconds (rational). */
	int export_length_den;
} oakcodec_encoding_params;

/**
 * @brief Create an encoder for `params` (count 1).
 *
 * The implementation (FFmpeg/OIIO) is chosen from params.format and the
 * enabled tracks. The file is NOT opened yet. Returns an empty handle
 * (ctx == NULL) when the configuration is invalid.
 */
OAKCODEC_API OakEncoder oakcodec_encoder_init(const oakcodec_encoding_params *params);

/** @brief Release one reference to an encoder. No-op on NULL/empty. */
OAKCODEC_API void oakcodec_encoder_free(OakEncoder *encoder);

/**
 * @brief Set an encoder-specific video option (e.g. "crf" = "18").
 *
 * Only valid between init and open.
 *
 * @return OAKCODEC_OK, or OAKCODEC_E_STATE when already open.
 */
OAKCODEC_API int oakcodec_encoder_set_video_option(OakEncoder encoder, const char *key,
									  const char *value);

/**
 * @brief Open the output file and write stream headers.
 *
 * @return OAKCODEC_OK, OAKCODEC_E_STATE (already open), or
 *         OAKCODEC_E_FAILED (see oakcodec_encoder_last_error()).
 */
OAKCODEC_API int oakcodec_encoder_open(OakEncoder encoder);

/**
 * @brief Encode one video frame.
 *
 * The frame's parameters must match the encoding parameters (the encoder
 * converts the delivery pixel format to the encoded one internally).
 */
OAKCODEC_API int oakcodec_encoder_write_video(OakEncoder encoder, OakFrame frame);

/**
 * @brief Encode interleaved float audio samples.
 *
 * @param samples frame_count * channel_count interleaved floats.
 * @return OAKCODEC_OK or a negative OAKCODEC_E_* code.
 */
OAKCODEC_API int oakcodec_encoder_write_audio(OakEncoder encoder, const float *samples,
								 int frame_count);

/**
 * @brief Encode one subtitle entry (times in seconds).
 */
OAKCODEC_API int oakcodec_encoder_write_subtitle(OakEncoder encoder, const char *text,
								double in_seconds, double out_seconds);

/**
 * @brief Flush the encoders, write the trailer and close the file.
 *
 * Idempotent; after a successful flush the encoder cannot be written to
 * (write calls return OAKCODEC_E_STATE).
 */
OAKCODEC_API int oakcodec_encoder_flush(OakEncoder encoder);

/**
 * @brief Human-readable detail of the last error on this encoder
 *        (buf/size string getter convention).
 */
OAKCODEC_API int oakcodec_encoder_last_error(OakEncoder encoder, char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_CODEC_ENCODER_H
