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

namespace fb
{

void ChannelLayoutFromMask(AVChannelLayout *layout, uint64_t mask,
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

uint64_t ValidateStreamChannelLayoutMask(const AVStream *stream)
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

int SwsColorspaceFromAVColorSpace(AVColorSpace cs)
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

void SetError(char *error_buffer, size_t error_buffer_size, const char *context,
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
	return av_get_pix_fmt_name(static_cast<AVPixelFormat>(pix_fmt));
}

int fb_pix_fmt_from_name(const char *name)
{
	return av_get_pix_fmt(name);
}

int fb_pix_fmt_bits_per_pixel(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(static_cast<AVPixelFormat>(pix_fmt));
	if (!desc) {
		return 0;
	}
	return av_get_bits_per_pixel(desc);
}

int fb_pix_fmt_has_alpha(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(static_cast<AVPixelFormat>(pix_fmt));
	return desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
}

int fb_pix_fmt_is_planar(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(static_cast<AVPixelFormat>(pix_fmt));
	return desc && (desc->flags & AV_PIX_FMT_FLAG_PLANAR);
}

int fb_pix_fmt_component_size(int pix_fmt)
{
	const AVPixFmtDescriptor *desc =
		av_pix_fmt_desc_get(static_cast<AVPixelFormat>(pix_fmt));
	if (!desc || desc->nb_components == 0) {
		return 0;
	}
	// Bytes used to store one component: 1 for 8-bit formats, 2 for 9-16bit
	return (desc->comp[0].depth + 7) / 8;
}

int fb_find_best_pix_fmt_of_list(const int *list, int pix_fmt)
{
	// Count the list
	int count = 0;
	while (list[count] != FB_PIX_FMT_NONE) {
		count++;
	}

	return avcodec_find_best_pix_fmt_of_list(
		reinterpret_cast<const AVPixelFormat *>(list),
		static_cast<AVPixelFormat>(pix_fmt), 1, nullptr);
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
