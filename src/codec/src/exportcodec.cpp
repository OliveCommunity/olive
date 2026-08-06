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

#include "exportcodec.h"

extern "C" {
}

namespace olive
{

std::string ExportCodec::get_codec_name(ExportCodec::Codec c)
{
	switch (c) {
	case k_codec_d_nx_hd:
		return "DNxHD";
	case k_codec_h264:
		return "H.264";
	case k_codec_h264rgb:
		return "H.264 RGB";
	case k_codec_h265:
		return "H.265";
	case k_codec_open_exr:
		return "OpenEXR";
	case k_codec_png:
		return "PNG";
	case k_codec_pro_res:
		return "ProRes";
	case k_codec_cineform:
		return "Cineform";
	case k_codec_tiff:
		return "TIFF";
	case k_codec_m_p2:
		return "MP2";
	case k_codec_m_p3:
		return "MP3";
	case k_codec_aac:
		return "AAC";
	case k_codec_pcm:
		return "PCM (Uncompressed)";
	case k_codec_flac:
		return "FLAC";
	case k_codec_opus:
		return "Opus";
	case k_codec_vorbis:
		return "Vorbis";
	case k_codec_v_p9:
		return "VP9";
	case k_codec_a_v1:
		return "AV1";
	case k_codec_srt:
		return "SubRip SRT";
	case k_codec_count:
		break;
	}

	return "Unknown";
}

bool ExportCodec::is_codec_a_still_image(ExportCodec::Codec c)
{
	switch (c) {
	case k_codec_d_nx_hd:
	case k_codec_h264:
	case k_codec_h264rgb:
	case k_codec_h265:
	case k_codec_pro_res:
	case k_codec_cineform:
	case k_codec_m_p2:
	case k_codec_m_p3:
	case k_codec_aac:
	case k_codec_pcm:
	case k_codec_vorbis:
	case k_codec_opus:
	case k_codec_flac:
	case k_codec_v_p9:
	case k_codec_a_v1:
	case k_codec_srt:
		return false;
	case k_codec_open_exr:
	case k_codec_png:
	case k_codec_tiff:
		return true;
	case k_codec_count:
		break;
	}

	return false;
}

bool ExportCodec::is_codec_lossless(Codec c)
{
	switch (c) {
	case k_codec_pcm:
	case k_codec_flac:
		return true;
	case k_codec_d_nx_hd:
	case k_codec_h264:
	case k_codec_h264rgb:
	case k_codec_h265:
	case k_codec_pro_res:
	case k_codec_cineform:
	case k_codec_m_p2:
	case k_codec_m_p3:
	case k_codec_aac:
	case k_codec_vorbis:
	case k_codec_opus:
	case k_codec_v_p9:
	case k_codec_a_v1:
	case k_codec_srt:
	case k_codec_open_exr:
	case k_codec_png:
	case k_codec_tiff:
	case k_codec_count:
		break;
	}

	return false;
}

}
