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

#ifndef OAK_EXPORTCODEC_H
#define OAK_EXPORTCODEC_H

#include <string>

namespace olive
{

class ExportCodec {
public:
	// Only append to this list (never insert) because indexes are used in serialized files
	enum Codec {
		k_codec_d_nx_hd,
		k_codec_h264,
		k_codec_h264rgb,
		k_codec_h265,
		k_codec_open_exr,
		k_codec_png,
		k_codec_pro_res,
		k_codec_cineform,
		k_codec_tiff,
		k_codec_v_p9,
		k_codec_m_p2,
		k_codec_m_p3,
		k_codec_aac,
		k_codec_pcm,
		k_codec_opus,
		k_codec_vorbis,
		k_codec_flac,
		k_codec_srt,
		k_codec_a_v1,

		k_codec_count
	};

	static std::string get_codec_name(Codec c);

	static bool is_codec_a_still_image(Codec c);

	static bool is_codec_lossless(Codec c);
};

}

#endif // OAK_EXPORTCODEC_H
