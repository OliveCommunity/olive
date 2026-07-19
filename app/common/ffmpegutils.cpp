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

#include "common/ffmpegutils.h"

namespace olive
{

int FFmpegUtils::get_compatible_bridge_pixel_format(int pix_fmt,
												PixelFormat maximum)
{
	int possible_pix_fmts[4];

	possible_pix_fmts[0] = fb_pix_fmt_rgba;

	if (maximum == PixelFormat::u8) {
		possible_pix_fmts[1] = fb_pix_fmt_none;
	} else {
		possible_pix_fmts[1] = fb_pix_fmt_rgb_a64_le;
		if (maximum == PixelFormat::f32) {
			possible_pix_fmts[2] = fb_pix_fmt_rgba_f32_le;
			possible_pix_fmts[3] = fb_pix_fmt_none;
		} else {
			possible_pix_fmts[2] = fb_pix_fmt_none;
		}
	}

	return fb_find_best_pix_fmt_of_list(possible_pix_fmts, pix_fmt);
}

SampleFormat FFmpegUtils::get_native_sample_format(int smp_fmt)
{
	switch (smp_fmt) {
	case fb_sample_fmt_u8:
		return SampleFormat::u8;
	case fb_sample_fmt_s16:
		return SampleFormat::s16;
	case fb_sample_fmt_s32:
		return SampleFormat::s32;
	case fb_sample_fmt_s64:
		return SampleFormat::s64;
	case fb_sample_fmt_flt:
		return SampleFormat::f32;
	case fb_sample_fmt_dbl:
		return SampleFormat::f64;
	case fb_sample_fmt_u8_p:
		return SampleFormat::u8_p;
	case fb_sample_fmt_s16_p:
		return SampleFormat::s16_p;
	case fb_sample_fmt_s32_p:
		return SampleFormat::s32_p;
	case fb_sample_fmt_s64_p:
		return SampleFormat::s64_p;
	case fb_sample_fmt_fltp:
		return SampleFormat::f32_p;
	case fb_sample_fmt_dblp:
		return SampleFormat::f64_p;
	default:
		break;
	}

	return SampleFormat::invalid;
}

int FFmpegUtils::get_f_fmpeg_sample_format(const SampleFormat &smp_fmt)
{
	switch (smp_fmt) {
	case SampleFormat::u8:
		return fb_sample_fmt_u8;
	case SampleFormat::s16:
		return fb_sample_fmt_s16;
	case SampleFormat::s32:
		return fb_sample_fmt_s32;
	case SampleFormat::s64:
		return fb_sample_fmt_s64;
	case SampleFormat::f32:
		return fb_sample_fmt_flt;
	case SampleFormat::f64:
		return fb_sample_fmt_dbl;
	case SampleFormat::u8_p:
		return fb_sample_fmt_u8_p;
	case SampleFormat::s16_p:
		return fb_sample_fmt_s16_p;
	case SampleFormat::s32_p:
		return fb_sample_fmt_s32_p;
	case SampleFormat::s64_p:
		return fb_sample_fmt_s64_p;
	case SampleFormat::f32_p:
		return fb_sample_fmt_fltp;
	case SampleFormat::f64_p:
		return fb_sample_fmt_dblp;
	case SampleFormat::invalid:
	case SampleFormat::count:
		break;
	}

	return fb_sample_fmt_none;
}

int FFmpegUtils::convert_jpeg_space_to_regular_space(int f)
{
	switch (f) {
	case fb_pix_fmt_yuv_j420_p:
		return fb_pix_fmt_yu_v420_p;
	case fb_pix_fmt_yuv_j422_p:
		return fb_pix_fmt_yu_v422_p;
	case fb_pix_fmt_yuv_j444_p:
		return fb_pix_fmt_yu_v444_p;
	case fb_pix_fmt_yuv_j440_p:
		return fb_pix_fmt_yu_v440_p;
	case fb_pix_fmt_yuv_j411_p:
		return fb_pix_fmt_yu_v411_p;
	default:
		break;
	}

	return f;
}

int FFmpegUtils::get_f_fmpeg_pixel_format(const PixelFormat &pix_fmt,
									  int channel_layout)
{
	if (channel_layout == VideoParams::k_rgb_channel_count) {
		switch (pix_fmt) {
		case PixelFormat::u8:
			return fb_pix_fmt_rg_b24;
		case PixelFormat::u10:
			return fb_pix_fmt_none;
		case PixelFormat::u16:
			return fb_pix_fmt_rg_b48_le;
		case PixelFormat::f16:
			return fb_pix_fmt_rgb_f16_le;
		case PixelFormat::f32:
			return fb_pix_fmt_rgb_f32_le;
		case PixelFormat::invalid:
		case PixelFormat::count:
			break;
		}
	} else if (channel_layout == VideoParams::k_rgba_channel_count) {
		switch (pix_fmt) {
		case PixelFormat::u8:
			return fb_pix_fmt_rgba;
		case PixelFormat::u10:
			return fb_pix_fmt_none;
		case PixelFormat::u16:
			return fb_pix_fmt_rgb_a64_le;
		case PixelFormat::f16:
			return fb_pix_fmt_rgba_f16_le;
		case PixelFormat::f32:
			return fb_pix_fmt_rgba_f32_le;
		case PixelFormat::invalid:
		case PixelFormat::count:
			break;
		}
	}

	return fb_pix_fmt_none;
}

PixelFormat FFmpegUtils::get_compatible_pixel_format(const PixelFormat &pix_fmt)
{
	switch (pix_fmt) {
	case PixelFormat::u8:
		return PixelFormat::u8;
	case PixelFormat::u10:
		return PixelFormat::u8;
	case PixelFormat::u16:
	case PixelFormat::f16:
	case PixelFormat::f32:
		return PixelFormat::u16;
	case PixelFormat::invalid:
	case PixelFormat::count:
		break;
	}

	return PixelFormat::invalid;
}

}
