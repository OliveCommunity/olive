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

#ifndef OAKENGINE_EXPORTER_H
#define OAKENGINE_EXPORTER_H

#include <stdint.h>

#include "export.h"
#include "init.h"
#include "timeline.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file exporter.h
 * @brief C ABI for synchronous offline export (render + encode)
 *
 * oakengine_export_render() renders a sequence range offline
 * (RenderMode::k_online) and encodes it straight to a file, driving the
 * engine's own export path (ExportTask over EncodingParams + the
 * FFmpeg/OIIO encoders, engine/task/export/export.cpp) synchronously on the
 * calling thread. The engine's task machinery itself has no UI dependency;
 * the export dialog stays out of the picture by design. (Named exporter.h
 * because oakengine/export.h already holds the symbol visibility macros.)
 *
 * Rendering requires OAKENGINE_INIT_RENDER (video frames go through the
 * render worker pool and may need GL); codec probing
 * (oakengine_export_has_video_codec()/_has_audio_codec()) does not.
 *
 * Conventions match the other facade families: 0 (OAKENGINE_OK) / negative
 * OAKENGINE_E_* codes, buf/size strings, NULL handles are no-ops. Failures
 * record a human-readable reason in the thread-local last-error string
 * (oakengine_export_last_error()).
 */

/** @brief Video codecs for oak_export_options::video_codec. */
#define OAKENGINE_EXPORT_VIDEO_H264 0 /**< H.264 in an MP4 container. */
#define OAKENGINE_EXPORT_VIDEO_H265 1 /**< H.265/HEVC in an MP4 container. */
#define OAKENGINE_EXPORT_VIDEO_PNG_SEQUENCE 2 /**< PNG still-image sequence. */

/** @brief Audio codecs for oak_export_options::audio_codec. */
#define OAKENGINE_EXPORT_AUDIO_AAC 0
#define OAKENGINE_EXPORT_AUDIO_PCM 1
/** @brief Disable the audio track entirely (not a codec). */
#define OAKENGINE_EXPORT_AUDIO_NONE (-1)

/**
 * @brief POD export parameters. 0 (or negative) fields select the default
 * documented per field.
 */
typedef struct oak_export_options {
	/** OAKENGINE_EXPORT_VIDEO_* value; default H264. */
	int video_codec;
	/** OAKENGINE_EXPORT_AUDIO_* value; default AAC; AUDIO_NONE disables. */
	int audio_codec;
	/** Video bit rate in bit/s; <= 0 lets the encoder choose (FFmpeg
	 * defaults). */
	int64_t video_bit_rate;
	/** Audio sample rate in Hz; <= 0 uses the sequence's rate. */
	int audio_sample_rate;
	/** Audio channel count (1 = mono, 2 = stereo); <= 0 uses the
	 * sequence's layout. */
	int audio_channel_count;
} oak_export_options;

/**
 * @brief Render `seq`'s [in_ts, out_ts) range offline and encode it to
 * `path`.
 *
 * `in_ts`/`out_ts` are frame timestamps in the sequence's frame-rate
 * timebase (the export frame rate is the sequence frame rate). `width` and
 * `height` <= 0 fall back to the sequence's video dimensions; when they
 * differ, the frames are scaled to fit (EncodingParams::k_fit). Video is
 * encoded with the options' codec (PNG sequence: `path` is the filename
 * template -- a "-%04d" frame placeholder is inserted before the extension
 * when absent), audio with the options' codec at the requested rate/layout,
 * and color is transformed from the project's reference space to sRGB OETF
 * (the application export dialog's default output).
 *
 * The call blocks until the export finishes. Progress is reported through
 * the callback set with oakengine_export_set_progress_callback().
 *
 * Image sequences: `path` is the filename template; the engine's frame
 * placeholder ("_[#####]") is inserted before the extension when absent
 * (same bracketed-hash form the application uses).
 *
 * @return OAKENGINE_OK on success; OAKENGINE_E_INVALID for bad arguments;
 * OAKENGINE_E_STATE when the engine lacks OAKENGINE_INIT_RENDER;
 * OAKENGINE_E_FAILED for render/encode failures (see
 * oakengine_export_last_error()).
 */
OAKENGINE_API int oakengine_export_render(OakEngineSequence *seq,
										  const char *path, int64_t in_ts,
										  int64_t out_ts, int width,
										  int height,
										  const oak_export_options *opts);

/**
 * @brief Human-readable reason for the last failed export on this thread
 * (buf/size convention).
 */
OAKENGINE_API int oakengine_export_last_error(char *buf, int buf_size);

/**
 * @brief 1 if the OAKENGINE_EXPORT_VIDEO_* codec is encodable here, 0
 * otherwise (unknown codec ids included).
 */
OAKENGINE_API int oakengine_export_has_video_codec(int codec);

/**
 * @brief 1 if the OAKENGINE_EXPORT_AUDIO_* codec is encodable here, 0
 * otherwise (AUDIO_NONE and unknown ids included).
 */
OAKENGINE_API int oakengine_export_has_audio_codec(int codec);

/**
 * @brief Progress callback signature: `fraction` in [0, 1], monotonically
 * non-decreasing during one export.
 */
typedef void (*oakengine_export_progress_fn)(double fraction,
											 void *userdata);

/**
 * @brief Install the progress callback used by subsequent
 * oakengine_export_render() calls on this thread (NULL disables).
 */
OAKENGINE_API void
oakengine_export_set_progress_callback(oakengine_export_progress_fn fn,
									   void *userdata);

/**
 * @brief Cancel the currently running oakengine_export_render()/_ex()
 * call, if any (from another thread).
 *
 * The export aborts at the next opportunity and the blocked render call
 * returns OAKENGINE_E_CANCELLED. A no-op when no export is running.
 */
OAKENGINE_API void oakengine_export_cancel(void);

/**
 * @brief Cancellation return code of oakengine_export_render()/_ex().
 */
#define OAKENGINE_E_CANCELLED (-5)

/* ---- Extended export (oakengine_export_render_ex) -------------------------
 *
 * The extended entry point covers every field of the engine's
 * EncodingParams (engine/codec/encoder.h) through the POD below, so
 * full-featured consumers (like the application's export dialog) can drive
 * the same export path without engine headers. Enum int fields carry the
 * engine's own enum values (olive::ExportFormat::Format,
 * olive::ExportCodec::Codec, olive::VideoParams::Interlacing/ColorRange,
 * olive::core::SampleFormat::Format) unless documented otherwise; <= 0
 * (or -1 where noted) selects the documented default.
 */

/** @brief range_mode values: what part of the sequence to export. */
#define OAKENGINE_EXPORT_RANGE_ENTIRE 0 /**< Whole sequence. */
#define OAKENGINE_EXPORT_RANGE_CUSTOM 1 /**< [range_in_ts, range_out_ts). */
#define OAKENGINE_EXPORT_RANGE_STILL 2 /**< Single frame at still_time_ts. */

/** @brief color_transform values: output color transform. */
#define OAKENGINE_EXPORT_COLOR_SRGB_OETF 0 /**< sRGB OETF (dialog default). */
#define OAKENGINE_EXPORT_COLOR_REC709_OETF 1 /**< Rec.709 OETF. */
#define OAKENGINE_EXPORT_COLOR_REFERENCE 2 /**< Reference space, no transform. */
#define OAKENGINE_EXPORT_COLOR_BT1886_EOTF 3 /**< BT.1886 EOTF. */
#define OAKENGINE_EXPORT_COLOR_CUSTOM (-1) /**< Use color_transform_name. */

/**
 * @brief Full export parameters (POD; covers EncodingParams).
 */
typedef struct oak_export_options_ex {
	/** OAKENGINE_EXPORT_RANGE_*; default ENTIRE. */
	int range_mode;
	/** range_mode == CUSTOM: range [in, out) as frame timestamps in the
	 * sequence's frame-rate timebase. */
	int64_t range_in_ts;
	int64_t range_out_ts;
	/** range_mode == STILL: the frame to export as a timestamp. */
	int64_t still_time_ts;

	/** Container format as olive::ExportFormat::Format
	 * (0 = MP4, 5 = PNG sequence, others per engine/codec/exportformat.h). */
	int format;

	int video_enabled; /**< 0/1; default 1. */
	/** Video codec as olive::ExportCodec::Codec (1 = H.264, 3 = H.265,
	 * 5 = PNG, others per engine/codec/exportcodec.h). */
	int video_codec;
	int audio_enabled; /**< 0/1; default 1. */
	/** Audio codec as olive::ExportCodec::Codec (11 = AAC, 12 = PCM,
	 * others per engine/codec/exportcodec.h). */
	int audio_codec;

	int subtitles_enabled; /**< 0/1; default 0. */
	int subtitles_sidecar; /**< 0 = embedded, 1 = sidecar file; default 0. */
	/** Sidecar container as olive::ExportFormat::Format (13 = SRT). */
	int subtitles_format;
	/** Subtitle codec as olive::ExportCodec::Codec (16 = SRT). */
	int subtitles_codec;

	/** Video bit rate in bit/s; <= 0 lets the encoder choose. */
	int64_t video_bit_rate;
	/** Audio bit rate in bit/s; <= 0 lets the encoder choose. */
	int64_t audio_bit_rate;

	/** Encoded pixel format as an index into
	 * FFmpegEncoder::get_pixel_formats_for_codec(video_codec)
	 * (0 = the codec's preferred format, e.g. yuv420p for H.264;
	 * -1 = same as 0). Out-of-range indexes fail with
	 * OAKENGINE_E_INVALID. */
	int video_pix_fmt;

	/** Audio sample rate in Hz; <= 0 uses the sequence's rate. */
	int audio_sample_rate;
	/** Audio channel layout mask (0 = the sequence's layout). */
	uint64_t audio_channel_layout;
	/** Audio sample format: <= 0 selects the engine's float planar default
	 * (f32_p); >= 1 is an explicit olive::core::SampleFormat::Format value
	 * in engine units (s16_p = 1, s32_p = 2, s64_p = 3, f32_p = 4,
	 * f64_p = 5, u8 = 6, s16 = 7, s32 = 8, s64 = 9, f32 = 10, f64 = 11).
	 * u8_p (raw 0) overlaps the default and is not expressible. */
	int audio_sample_format;

	/** OAKENGINE_EXPORT_COLOR_*; default SRGB_OETF. */
	int color_transform;
	/** OCIO color space name, used when color_transform ==
	 * OAKENGINE_EXPORT_COLOR_CUSTOM. Empty otherwise. */
	char color_transform_name[64];

	/** Output dimensions; <= 0 uses the sequence's dimensions. */
	int video_width;
	int video_height;
	/** Output frame rate; <= 0 uses the sequence's frame rate. */
	int frame_rate_num;
	int frame_rate_den;
	/** Pixel aspect ratio; <= 0 uses the sequence's ratio. */
	int pixel_aspect_num;
	int pixel_aspect_den;
	/** olive::VideoParams::Interlacing; -1 = the sequence's mode. */
	int interlacing;
	/** Render pixel format as olive::core::PixelFormat::Format;
	 * -1 = the OfflinePixelFormat config default. */
	int pixel_format;
	/** Scaling method (EncodingParams::VideoScalingMethod):
	 * -1 = fit (default), 0 = fit, 1 = stretch, 2 = crop. */
	int scaling_method;
	/** olive::VideoParams::ColorRange; -1 = limited (default). */
	int color_range;
	/** Encoder thread count; <= 0 = auto. */
	int video_threads;
	/** 1 = write an image sequence (still-image codecs); default 0. */
	int is_image_sequence;
} oak_export_options_ex;

/**
 * @brief Extended synchronous export with full EncodingParams coverage.
 *
 * Assembles the engine's EncodingParams from `opts` and runs the same
 * synchronous ExportTask path as oakengine_export_render() (progress
 * callback, conform prewarm, event-loop drive). PNG sequences accept a
 * filename template ("-%04d" is inserted before the extension when
 * absent), same as the simple entry point.
 *
 * @return OAKENGINE_OK on success; OAKENGINE_E_INVALID for bad options;
 * OAKENGINE_E_STATE without OAKENGINE_INIT_RENDER; OAKENGINE_E_FAILED for
 * render/encode failures; OAKENGINE_E_CANCELLED after
 * oakengine_export_cancel().
 */
OAKENGINE_API int oakengine_export_render_ex(OakEngineSequence *seq,
											 const char *path,
											 const oak_export_options_ex *opts);

/**
 * @brief Set an encoder-specific video option (key/value strings, e.g.
 * "crf" = "18") applied by subsequent oakengine_export_render()/_ex()
 * calls on this thread (mirrors EncodingParams::set_video_option()).
 * Repeated calls accumulate; NULL value clears all options.
 */
OAKENGINE_API void oakengine_export_set_video_option(const char *key,
													 const char *value);

#ifdef __cplusplus
}
#endif

#endif /* OAKENGINE_EXPORTER_H */
