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

#include "common/ffmpegutils.h"

#include "../src/ffmpegutils.h"

int oakcommon_ffmpegutils_get_compatible_bridge_pixel_format(
	int pix_fmt, int maximum_pix_fmt, int *out)
{
	if (!out)
		return OAKCOMMON_E_INVALID;

	try {
		*out = olive::FFmpegUtils::get_compatible_bridge_pixel_format(
			pix_fmt,
			static_cast<olive::core::PixelFormat::Format>(
				maximum_pix_fmt));
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}

	return OAKCOMMON_OK;
}

int oakcommon_ffmpegutils_get_compatible_pixel_format(int pix_fmt, int *out)
{
	if (!out)
		return OAKCOMMON_E_INVALID;

	try {
		*out = olive::FFmpegUtils::get_compatible_pixel_format(
			static_cast<olive::core::PixelFormat::Format>(pix_fmt));
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}

	return OAKCOMMON_OK;
}

int oakcommon_ffmpegutils_get_ffmpeg_pixel_format(int pix_fmt,
						  int channel_count, int *out)
{
	if (!out)
		return OAKCOMMON_E_INVALID;

	try {
		*out = olive::FFmpegUtils::get_ffmpeg_pixel_format(
			static_cast<olive::core::PixelFormat::Format>(pix_fmt),
			channel_count);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}

	return OAKCOMMON_OK;
}

int oakcommon_ffmpegutils_get_native_sample_format(int smp_fmt, int *out)
{
	if (!out)
		return OAKCOMMON_E_INVALID;

	try {
		*out = olive::FFmpegUtils::get_native_sample_format(smp_fmt);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}

	return OAKCOMMON_OK;
}

int oakcommon_ffmpegutils_get_ffmpeg_sample_format(int smp_fmt, int *out)
{
	if (!out)
		return OAKCOMMON_E_INVALID;

	try {
		*out = olive::FFmpegUtils::get_ffmpeg_sample_format(
			static_cast<olive::core::SampleFormat::Format>(smp_fmt));
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}

	return OAKCOMMON_OK;
}

int oakcommon_ffmpegutils_convert_jpeg_space_to_regular_space(int pix_fmt,
							      int *out)
{
	if (!out)
		return OAKCOMMON_E_INVALID;

	try {
		*out = olive::FFmpegUtils::convert_jpeg_space_to_regular_space(
			pix_fmt);
	} catch (...) {
		return OAKCOMMON_E_FAILED;
	}

	return OAKCOMMON_OK;
}
