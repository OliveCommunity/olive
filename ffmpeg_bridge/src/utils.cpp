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

#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <mutex>
#include <utility>
#include <vector>

namespace fb
{

namespace
{

/**
 * The public FB_PIX_FMT_* values are fixed identifiers (the host app never
 * sees AVPixelFormat). AVPixelFormat enum values shift between FFmpeg
 * releases because new formats are inserted mid-enum, so the mapping is
 * resolved through pixel format names, which are stable. Formats unknown to
 * the FFmpeg build this library was compiled against resolve to
 * AV_PIX_FMT_NONE.
 */
struct PixFmtName {
	int fb_fmt;
	const char *av_name;
};

constexpr PixFmtName k_pix_fmt_names[] = {
	{ fb_pix_fmt_yu_v420_p, "yuv420p" },
	{ fb_pix_fmt_rg_b24, "rgb24" },
	{ fb_pix_fmt_yu_v422_p, "yuv422p" },
	{ fb_pix_fmt_yu_v444_p, "yuv444p" },
	{ fb_pix_fmt_yu_v410_p, "yuv410p" },
	{ fb_pix_fmt_yu_v411_p, "yuv411p" },
	{ fb_pix_fmt_gra_y8, "gray8" },
	{ fb_pix_fmt_yuv_j420_p, "yuvj420p" },
	{ fb_pix_fmt_yuv_j422_p, "yuvj422p" },
	{ fb_pix_fmt_yuv_j444_p, "yuvj444p" },
	{ fb_pix_fmt_n_v12, "nv12" },
	{ fb_pix_fmt_rgba, "rgba" },
	{ fb_pix_fmt_gra_y16_le, "gray16le" },
	{ fb_pix_fmt_yu_v440_p, "yuv440p" },
	{ fb_pix_fmt_yuv_j440_p, "yuvj440p" },
	{ fb_pix_fmt_rg_b48_le, "rgb48le" },
	{ fb_pix_fmt_yu_v420_p10_le, "yuv420p10le" },
	{ fb_pix_fmt_yu_v422_p10_le, "yuv422p10le" },
	{ fb_pix_fmt_yu_v444_p10_le, "yuv444p10le" },
	{ fb_pix_fmt_rgb_a64_le, "rgba64le" },
	{ fb_pix_fmt_yu_v420_p12_le, "yuv420p12le" },
	{ fb_pix_fmt_yu_v422_p12_le, "yuv422p12le" },
	{ fb_pix_fmt_yu_v444_p12_le, "yuv444p12le" },
	{ fb_pix_fmt_yuv_j411_p, "yuvj411p" },
	{ fb_pix_fmt_p010_le, "p010le" },
	{ fb_pix_fmt_gray_f32_le, "grayf32le" },
	{ fb_pix_fmt_rgba_f16_le, "rgbaf16le" },
	{ fb_pix_fmt_rgb_f32_le, "rgbf32le" },
	{ fb_pix_fmt_rgba_f32_le, "rgbaf32le" },
	{ fb_pix_fmt_rgb_f16_le, "rgbf16le" },
	{ fb_pix_fmt_gray_f16_le, "grayf16le" },
};

} // namespace

namespace
{

/**
 * Formats outside the static table (hardware-download formats like p210le,
 * or anything else a decoder produces) receive process-local identifiers so
 * they still round-trip through the API (e.g. into the scaler) instead of
 * collapsing to FB_PIX_FMT_NONE. Dynamic ids start above every static
 * FB_PIX_FMT_* value and are not stable across runs, which is fine because
 * callers treat the values as opaque.
 */
constexpr int k_dynamic_pix_fmt_base = 1000;
std::mutex g_dynamic_pix_fmt_mutex;
std::vector<std::pair<int, AVPixelFormat>> g_dynamic_pix_fmts;

} // namespace

AVPixelFormat pix_fmt_to_av(int fb_fmt)
{
	if (fb_fmt == fb_pix_fmt_none) {
		return AV_PIX_FMT_NONE;
	}
	for (const PixFmtName &entry : k_pix_fmt_names) {
		if (entry.fb_fmt == fb_fmt) {
			return av_get_pix_fmt(entry.av_name);
		}
	}
	if (fb_fmt >= k_dynamic_pix_fmt_base) {
		std::lock_guard<std::mutex> lock(g_dynamic_pix_fmt_mutex);
		for (const auto &entry : g_dynamic_pix_fmts) {
			if (entry.first == fb_fmt) {
				return entry.second;
			}
		}
	}
	return AV_PIX_FMT_NONE;
}

int pix_fmt_from_av(AVPixelFormat fmt)
{
	if (fmt == AV_PIX_FMT_NONE) {
		return fb_pix_fmt_none;
	}
	const char *name = av_get_pix_fmt_name(fmt);
	if (name) {
		for (const PixFmtName &entry : k_pix_fmt_names) {
			if (strcmp(name, entry.av_name) == 0) {
				return entry.fb_fmt;
			}
		}
	}

	std::lock_guard<std::mutex> lock(g_dynamic_pix_fmt_mutex);
	for (const auto &entry : g_dynamic_pix_fmts) {
		if (entry.second == fmt) {
			return entry.first;
		}
	}
	const int id = k_dynamic_pix_fmt_base + int(g_dynamic_pix_fmts.size());
	g_dynamic_pix_fmts.emplace_back(id, fmt);
	return id;
}

void channel_layout_from_mask(AVChannelLayout *layout, uint64_t mask,
						   int fallback_channels)
{
	if (mask != 0) {
		av_channel_layout_from_mask(layout, mask);
		return;
	}

	if (fallback_channels <= 0) {
		fallback_channels = 2;
	}
	av_channel_layout_default(layout, fallback_channels);
}

uint64_t validate_stream_channel_layout_mask(const AVStream *stream)
{
	if (!stream || !stream->codecpar) {
		return 0;
	}

	const AVChannelLayout &layout = stream->codecpar->ch_layout;
	if (av_channel_layout_check(&layout) &&
		layout.order == AV_CHANNEL_ORDER_NATIVE && layout.u.mask != 0) {
		return layout.u.mask;
	}

	// Fall back to a default layout for the stream's channel count
	if (layout.nb_channels <= 0) {
		return 0;
	}

	AVChannelLayout fallback;
	av_channel_layout_default(&fallback, layout.nb_channels);
	uint64_t mask = 0;
	if (fallback.order == AV_CHANNEL_ORDER_NATIVE) {
		mask = fallback.u.mask;
	}
	av_channel_layout_uninit(&fallback);
	return mask;
}

int sws_colorspace_from_av_color_space(AVColorSpace cs)
{
	switch (cs) {
	case AVCOL_SPC_BT709:
		return SWS_CS_ITU709;
	case AVCOL_SPC_FCC:
		return SWS_CS_FCC;
	case AVCOL_SPC_BT470BG:
		return SWS_CS_ITU624;
	case AVCOL_SPC_SMPTE170M:
		return SWS_CS_SMPTE170M;
	case AVCOL_SPC_SMPTE240M:
		return SWS_CS_SMPTE240M;
	case AVCOL_SPC_BT2020_NCL:
		return SWS_CS_BT2020;
	default:
		break;
	}

	return SWS_CS_DEFAULT;
}

void set_error(char *error_buffer, size_t error_buffer_size, const char *context,
			  int error_code)
{
	if (!error_buffer || error_buffer_size == 0) {
		return;
	}

	char ffmpeg_err[512];
	av_strerror(error_code, ffmpeg_err, sizeof(ffmpeg_err));
	snprintf(error_buffer, error_buffer_size, "%s: %s (%d)", context,
			 ffmpeg_err, error_code);
}

} // namespace fb

void fb_error_string(int error_code, char *buffer, int buffer_size)
{
	if (!buffer || buffer_size <= 0) {
		return;
	}

	if (error_code == FB_ERROR_EOF) {
		snprintf(buffer, size_t(buffer_size), "End of file");
		return;
	}

	if (av_strerror(error_code, buffer, size_t(buffer_size)) < 0) {
		snprintf(buffer, size_t(buffer_size), "Unknown error %d", error_code);
	}
}

const char *fb_version_string(void)
{
	static char version[128];
	snprintf(version, sizeof(version), "libavcodec %s, libavformat %s, libavutil %s, libswscale %s, libswresample %s, libavfilter %s",
			 LIBAVCODEC_IDENT, LIBAVFORMAT_IDENT, LIBAVUTIL_IDENT,
			 LIBSWSCALE_IDENT, LIBSWRESAMPLE_IDENT, LIBAVFILTER_IDENT);
	return version;
}

const char *fb_pix_fmt_name(int pix_fmt)
{
	return av_get_pix_fmt_name(fb::pix_fmt_to_av(pix_fmt));
}

int fb_pix_fmt_from_name(const char *name)
{
	return fb::pix_fmt_from_av(av_get_pix_fmt(name));
}

int fb_pix_fmt_bits_per_pixel(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(fb::pix_fmt_to_av(pix_fmt));
	if (!desc) {
		return 0;
	}
	return av_get_bits_per_pixel(desc);
}

int fb_pix_fmt_has_alpha(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(fb::pix_fmt_to_av(pix_fmt));
	return desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
}

int fb_pix_fmt_is_planar(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(fb::pix_fmt_to_av(pix_fmt));
	return desc && (desc->flags & AV_PIX_FMT_FLAG_PLANAR);
}

int fb_pix_fmt_component_size(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(fb::pix_fmt_to_av(pix_fmt));
	if (!desc || desc->nb_components == 0) {
		return 0;
	}
	// Bytes used to store one component: 1 for 8-bit formats, 2 for 9-16bit
	return (desc->comp[0].depth + 7) / 8;
}

int fb_find_best_pix_fmt_of_list(const int *list, int pix_fmt)
{
	// Translate the FB_PIX_FMT_NONE-terminated list to AVPixelFormat values,
	// skipping formats unknown to this FFmpeg build
	std::vector<AVPixelFormat> av_list;
	for (int i = 0; list[i] != fb_pix_fmt_none; i++) {
		AVPixelFormat fmt = fb::pix_fmt_to_av(list[i]);
		if (fmt != AV_PIX_FMT_NONE) {
			av_list.push_back(fmt);
		}
	}
	if (av_list.empty()) {
		return fb_pix_fmt_none;
	}

	// With an unknown source format there is no loss metric to compare
	// against; prefer the first (most desirable) list entry
	AVPixelFormat av_src = fb::pix_fmt_to_av(pix_fmt);
	if (av_src == AV_PIX_FMT_NONE) {
		return list[0];
	}

	av_list.push_back(AV_PIX_FMT_NONE);
	return fb::pix_fmt_from_av(avcodec_find_best_pix_fmt_of_list(
		av_list.data(), av_src, 1, nullptr));
}

int fb_channel_layout_get_channels(uint64_t mask)
{
	AVChannelLayout layout;
	av_channel_layout_from_mask(&layout, mask);
	int channels = layout.nb_channels;
	av_channel_layout_uninit(&layout);
	return channels;
}

uint64_t fb_channel_layout_default(int nb_channels)
{
	AVChannelLayout layout;
	av_channel_layout_default(&layout, nb_channels);
	uint64_t mask = 0;
	if (layout.order == AV_CHANNEL_ORDER_NATIVE) {
		mask = layout.u.mask;
	}
	av_channel_layout_uninit(&layout);
	return mask;
}
