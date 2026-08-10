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

#ifndef OAK_EDITOR_CODEC_DECODER_H
#define OAK_EDITOR_CODEC_DECODER_H

#include <stdint.h>

#include "error.h"
#include "frame.h"
#include "render/cancelatom.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file decoder.h
 * @brief C ABI for oakcodec media decoders (olive::Decoder and its
 *        FFmpeg/OIIO implementations): probing, stream enumeration and
 *        CPU-frame decoding.
 *
 * Handles follow the neutral by-value convention documented in frame.h
 * (and oakcommon's common/handle.h). Two usage patterns share the
 * OakDecoder handle:
 *
 *   - Probe: oakcodec_decoder_probe() inspects a file WITHOUT opening a
 *     decode session; the stream getters describe what was found.
 *   - Decode: oakcodec_decoder_init() + oakcodec_decoder_open() attach a
 *     decoder instance to one (filename, stream) pair; the decode
 *     functions then produce frames/audio.
 */

typedef struct OakDecoder {
	void *ctx; /**< Opaque pointer to the reference-counted object. */
	void (*addref)(void *ctx); /**< Atomically increments the count. */
	void (*release)(void *ctx); /**< Decrements the count, destroys at 0. */
	uint32_t abi_version; /**< OAKCODEC_ABI_VERSION. */
} OakDecoder;

/**
 * @brief POD description of one probed video stream.
 *
 * duration_ts counts units of the stream's time base;
 * time_base_num/den is seconds per time-base unit. color_primaries and
 * color_trc carry the ISO/IEC 23001-8 code points the decoder reports
 * (0 = unknown). interlaced is 1 when the stream is interlaced.
 * format is an OakPixelFormat value (the decoder's native delivery
 * format), channel_count its plane channel count.
 */
typedef struct oakcodec_video_stream_info {
	int stream_index;
	int width;
	int height;
	int frame_rate_num;
	int frame_rate_den;
	int64_t duration_ts;
	int time_base_num;
	int time_base_den;
	int format;
	int channel_count;
	int color_primaries;
	int color_trc;
	int interlaced;
} oakcodec_video_stream_info;

/**
 * @brief POD description of one probed audio stream.
 *
 * channel_layout is the ffmpeg-style channel mask (e.g. 0x3 = stereo).
 */
typedef struct oakcodec_audio_stream_info {
	int stream_index;
	int sample_rate;
	uint64_t channel_layout;
	int channel_count;
	int64_t duration_ts;
	int time_base_num;
	int time_base_den;
} oakcodec_audio_stream_info;

/* ---- Probe (stateless inspection) ---------------------------------------- */

/**
 * @brief Probe a media file: decoder name plus stream inventory.
 *
 * Tries each available decoder implementation (FFmpeg, then OIIO) and
 * wraps the first one that recognizes the file. The returned handle only
 * carries probe results; it cannot decode (use init + open for that).
 *
 * @return Handle with reference count 1, or an empty handle (ctx == NULL)
 *         when no decoder recognizes the file (oakcodec_probe_last_error()
 *         carries the reason).
 */
OAKCODEC_API OakDecoder oakcodec_decoder_probe(const char *filename);

/**
 * @brief Thread-local error detail of the last failed probe on this
 * thread (buf/size string getter convention).
 */
OAKCODEC_API int oakcodec_probe_last_error(char *buf, int buf_size);

/** @brief Probed decoder id ("ffmpeg"/"oiio", buf/size getter). */
OAKCODEC_API int oakcodec_decoder_probe_decoder_name(OakDecoder probe, char *buf,
									int buf_size);

OAKCODEC_API int oakcodec_decoder_probe_video_stream_count(OakDecoder probe);
OAKCODEC_API int oakcodec_decoder_probe_audio_stream_count(OakDecoder probe);
OAKCODEC_API int oakcodec_decoder_probe_subtitle_stream_count(OakDecoder probe);

/**
 * @brief Fill `out` with the video stream at `index` (0-based within the
 * video stream list).
 *
 * @return OAKCODEC_OK, OAKCODEC_E_INVALID, or OAKCODEC_E_NOT_FOUND when
 *         index is out of range.
 */
OAKCODEC_API int oakcodec_decoder_probe_get_video_stream(OakDecoder probe, int index,
										oakcodec_video_stream_info *out);
OAKCODEC_API int oakcodec_decoder_probe_get_audio_stream(OakDecoder probe, int index,
										oakcodec_audio_stream_info *out);

/* ---- Decode session ------------------------------------------------------- */

/**
 * @brief Create a closed decoder handle (count 1).
 */
OAKCODEC_API OakDecoder oakcodec_decoder_init(void);

/**
 * @brief Release one reference to a decoder. No-op on NULL/empty.
 */
OAKCODEC_API void oakcodec_decoder_free(OakDecoder *decoder);

/**
 * @brief Open `filename`'s stream `stream_index` for decoding.
 *
 * The decoder implementation is chosen automatically from the probe
 * results. Opening an already-open decoder on the same stream is a
 * successful no-op.
 *
 * @return OAKCODEC_OK on success, OAKCODEC_E_NOT_FOUND when the file
 *         does not exist, OAKCODEC_E_FAILED otherwise (see
 *         oakcodec_decoder_last_error()).
 */
OAKCODEC_API int oakcodec_decoder_open(OakDecoder decoder, const char *filename,
						  int stream_index);

/** @brief Close the current stream (safe when closed). */
OAKCODEC_API int oakcodec_decoder_close(OakDecoder decoder);

/** @brief 1 when a stream is open, 0 otherwise. */
OAKCODEC_API int oakcodec_decoder_is_open(OakDecoder decoder);

/**
 * @brief Decode the video frame at `numerator/denominator` seconds.
 *
 * Before the start of the footage the first frame is returned, after the
 * end the last frame.
 *
 * @return A frame handle with reference count 1 (caller releases), or an
 *         empty handle (ctx == NULL) on error/EOF — check
 *         oakcodec_decoder_last_error().
 */
OAKCODEC_API OakFrame oakcodec_decoder_decode_video(OakDecoder decoder, int numerator,
								   int denominator);

/**
 * @brief Decode audio into a float buffer.
 *
 * Decodes the interleaved audio covering [in, out) seconds (rational
 * pairs), resampled/laid out to `sample_rate`/`channel_layout`.
 * `buf` must hold at least `buf_frames` frames worth of interleaved
 * floats.
 *
 * @return The number of frames written (>= 0), or a negative
 *         OAKCODEC_E_* code. Conform generation is NOT triggered by this
 *         family in the current intermediate state (no task registrar);
 *         media requiring a conform yields OAKCODEC_E_STATE.
 */
OAKCODEC_API int oakcodec_decoder_decode_audio(OakDecoder decoder, int in_num, int in_den,
								  int out_num, int out_den, int sample_rate,
								  uint64_t channel_layout, float *buf,
								  int buf_frames);

/**
 * @brief Conform the open stream's audio into per-channel pcm cache files
 *        (Decoder::conform_audio()).
 *
 * `output_filenames` is an array of `filename_count` final per-channel
 * paths. `sample_format` is olive::core::SampleFormat::Format as int.
 * `cancelled` may be an empty OakCancelAtom (ctx == NULL).
 *
 * @return OAKCODEC_OK on success, OAKCODEC_E_STATE when no stream is
 *         open, OAKCODEC_E_CANCELLED when cancelled, OAKCODEC_E_FAILED
 *         otherwise.
 */
OAKCODEC_API int oakcodec_decoder_conform_audio(OakDecoder decoder,
		const char *const *output_filenames, int filename_count,
		int sample_rate, uint64_t channel_layout, int sample_format,
		OakCancelAtom cancelled);

/**
 * @brief Image-sequence filename heuristics (Decoder::get_image_sequence_*).
 *
 * digit_count: number of trailing digits in the filename stem (0 = not an
 * image sequence filename). index: the numeric value of those digits (-1
 * when none). transform: substitute `number` into the digit field,
 * two-stage string getter.
 */
OAKCODEC_API int oakcodec_decoder_get_image_sequence_digit_count(
		const char *filename);
OAKCODEC_API int64_t oakcodec_decoder_get_image_sequence_index(
		const char *filename);
OAKCODEC_API int oakcodec_decoder_transform_image_sequence_file_name(
		const char *filename, int64_t number, char *buf, int buf_size);

/**
 * @brief Human-readable detail of the last error on this decoder
 *        (buf/size string getter convention).
 */
OAKCODEC_API int oakcodec_decoder_last_error(OakDecoder decoder, char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif //OAK_EDITOR_CODEC_DECODER_H
