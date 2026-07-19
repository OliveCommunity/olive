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

#ifndef OAK_FFMPEG_BRIDGE_INTERNAL_H
#define OAK_FFMPEG_BRIDGE_INTERNAL_H

// Fixes weird define issue when including <avfilter.h>
#include <inttypes.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "ffmpeg_bridge/ffmpeg_bridge.h"

/* ------------------------------------------------------------------------- */
/* Compile-time verification that the public constants match FFmpeg          */
/* ------------------------------------------------------------------------- */

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wenum-compare"
#endif

static_assert(FB_ERROR_EOF == AVERROR_EOF, "FB_ERROR_EOF mismatch");
static_assert(FB_NOPTS_VALUE == AV_NOPTS_VALUE, "FB_NOPTS_VALUE mismatch");
static_assert(FB_TIME_BASE == AV_TIME_BASE, "FB_TIME_BASE mismatch");
static_assert(FB_SCALER_POINT == SWS_POINT, "FB_SCALER_POINT mismatch");

static_assert(fb_pix_fmt_none == AV_PIX_FMT_NONE, "FB_PIX_FMT_NONE mismatch");

static_assert(fb_sample_fmt_none == AV_SAMPLE_FMT_NONE, "samplefmt mismatch");
static_assert(fb_sample_fmt_u8 == AV_SAMPLE_FMT_U8, "samplefmt mismatch");
static_assert(fb_sample_fmt_s16 == AV_SAMPLE_FMT_S16, "samplefmt mismatch");
static_assert(fb_sample_fmt_s32 == AV_SAMPLE_FMT_S32, "samplefmt mismatch");
static_assert(fb_sample_fmt_flt == AV_SAMPLE_FMT_FLT, "samplefmt mismatch");
static_assert(fb_sample_fmt_dbl == AV_SAMPLE_FMT_DBL, "samplefmt mismatch");
static_assert(fb_sample_fmt_u8_p == AV_SAMPLE_FMT_U8P, "samplefmt mismatch");
static_assert(fb_sample_fmt_s16_p == AV_SAMPLE_FMT_S16P, "samplefmt mismatch");
static_assert(fb_sample_fmt_s32_p == AV_SAMPLE_FMT_S32P, "samplefmt mismatch");
static_assert(fb_sample_fmt_fltp == AV_SAMPLE_FMT_FLTP, "samplefmt mismatch");
static_assert(fb_sample_fmt_dblp == AV_SAMPLE_FMT_DBLP, "samplefmt mismatch");
static_assert(fb_sample_fmt_s64 == AV_SAMPLE_FMT_S64, "samplefmt mismatch");
static_assert(fb_sample_fmt_s64_p == AV_SAMPLE_FMT_S64P, "samplefmt mismatch");

static_assert(fb_color_range_unspec == AVCOL_RANGE_UNSPECIFIED, "range mismatch");
static_assert(fb_color_range_mpeg == AVCOL_RANGE_MPEG, "range mismatch");
static_assert(fb_color_range_jpeg == AVCOL_RANGE_JPEG, "range mismatch");

static_assert(fb_col_spc_rgb == AVCOL_SPC_RGB, "colspace mismatch");
static_assert(fb_col_spc_b_t709 == AVCOL_SPC_BT709, "colspace mismatch");
static_assert(fb_col_spc_unspec == AVCOL_SPC_UNSPECIFIED, "colspace mismatch");
static_assert(fb_col_spc_fcc == AVCOL_SPC_FCC, "colspace mismatch");
static_assert(fb_col_spc_b_t470_bg == AVCOL_SPC_BT470BG, "colspace mismatch");
static_assert(fb_col_spc_smpt_e170_m == AVCOL_SPC_SMPTE170M, "colspace mismatch");
static_assert(fb_col_spc_smpt_e240_m == AVCOL_SPC_SMPTE240M, "colspace mismatch");
static_assert(fb_col_spc_b_t2020_ncl == AVCOL_SPC_BT2020_NCL, "colspace mismatch");

static_assert(fb_media_type_video == AVMEDIA_TYPE_VIDEO, "mediatype mismatch");
static_assert(fb_media_type_audio == AVMEDIA_TYPE_AUDIO, "mediatype mismatch");
static_assert(fb_media_type_data == AVMEDIA_TYPE_DATA, "mediatype mismatch");
static_assert(fb_media_type_subtitle == AVMEDIA_TYPE_SUBTITLE, "mediatype mismatch");

static_assert(fb_field_order_unknown == AV_FIELD_UNKNOWN, "fieldorder mismatch");
static_assert(fb_field_order_progressive == AV_FIELD_PROGRESSIVE, "fieldorder mismatch");
static_assert(fb_field_order_tt == AV_FIELD_TT, "fieldorder mismatch");
static_assert(fb_field_order_bb == AV_FIELD_BB, "fieldorder mismatch");
static_assert(fb_field_order_tb == AV_FIELD_TB, "fieldorder mismatch");
static_assert(fb_field_order_bt == AV_FIELD_BT, "fieldorder mismatch");

static_assert(FB_CH_LAYOUT_MONO == AV_CH_LAYOUT_MONO, "layout mismatch");
static_assert(FB_CH_LAYOUT_STEREO == AV_CH_LAYOUT_STEREO, "layout mismatch");
static_assert(FB_CH_LAYOUT_2_1 == AV_CH_LAYOUT_2_1, "layout mismatch");
static_assert(FB_CH_LAYOUT_5POINT1 == AV_CH_LAYOUT_5POINT1, "layout mismatch");
static_assert(FB_CH_LAYOUT_7POINT1 == AV_CH_LAYOUT_7POINT1, "layout mismatch");

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

/* ------------------------------------------------------------------------- */
/* Handle bodies                                                              */
/* ------------------------------------------------------------------------- */

struct FBFrame {
	AVFrame *frame;
};

struct FBPacket {
	AVPacket *pkt;
};

namespace fb
{

/** Allocate a channel layout from a mask, falling back to a default layout
 * derived from `fallback_channels` when the mask is zero. */
void channel_layout_from_mask(AVChannelLayout *layout, uint64_t mask,
						   int fallback_channels);

/** Validate a stream's channel layout, returning a usable mask (never zero
 * unless the stream truly has no channels). */
uint64_t validate_stream_channel_layout_mask(const AVStream *stream);

/** Map an AVColorSpace to the corresponding SWS_CS_* constant. */
int sws_colorspace_from_av_color_space(AVColorSpace cs);

/**
 * Translate an FB_PIX_FMT_* value to the AVPixelFormat of the FFmpeg build
 * this library was compiled against. AVPixelFormat enum values shift between
 * FFmpeg releases (new formats are inserted mid-enum), so the public FB
 * values are fixed identifiers resolved by pixel format name. Returns
 * AV_PIX_FMT_NONE for FB_PIX_FMT_NONE and for static FB formats unknown to
 * this FFmpeg build (e.g. rgbf16le/grayf16le on FFmpeg < 7.1).
 */
AVPixelFormat pix_fmt_to_av(int fb_fmt);

/**
 * Reverse of PixFmtToAV. Returns FB_PIX_FMT_NONE for AV_PIX_FMT_NONE.
 * Formats without a static FB_PIX_FMT_* identifier (hardware-download
 * formats such as p210le, etc.) receive a process-local dynamic id >= 1000
 * so they can still round-trip through the API.
 */
int pix_fmt_from_av(AVPixelFormat fmt);

void set_error(char *error_buffer, size_t error_buffer_size, const char *context,
			  int error_code);

} // namespace fb

#endif // OAK_FFMPEG_BRIDGE_INTERNAL_H
