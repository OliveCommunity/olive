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

#include "exportformat.h"

#include "encoder.h"

namespace olive
{

std::string ExportFormat::get_name(olive::ExportFormat::Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
		return "DNxHD";
	case k_format_matroska:
		return "Matroska Video";
	case k_format_mpe_g4_video:
		return "MPEG-4 Video";
	case k_format_mpe_g4_audio:
		return "MPEG-4 Audio";
	case k_format_open_exr:
		return "OpenEXR";
	case k_format_png:
		return "PNG";
	case k_format_tiff:
		return "TIFF";
	case k_format_quick_time:
		return "QuickTime";
	case k_format_wav:
		return "Wave Audio";
	case k_format_aiff:
		return "AIFF";
	case k_format_m_p3:
		return "MP3";
	case k_format_flac:
		return "FLAC";
	case k_format_ogg:
		return "Ogg";
	case k_format_web_m:
		return "WebM";
	case k_format_srt:
		return "SubRip SRT";

	case k_format_count:
		break;
	}

	return "Unknown";
}

std::string ExportFormat::get_extension(ExportFormat::Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
		return "mxf";
	case k_format_matroska:
		return "mkv";
	case k_format_mpe_g4_video:
		return "mp4";
	case k_format_mpe_g4_audio:
		return "m4a";
	case k_format_open_exr:
		return "exr";
	case k_format_png:
		return "png";
	case k_format_tiff:
		return "tiff";
	case k_format_quick_time:
		return "mov";
	case k_format_wav:
		return "wav";
	case k_format_aiff:
		return "aiff";
	case k_format_m_p3:
		return "mp3";
	case k_format_flac:
		return "flac";
	case k_format_ogg:
		return "ogg";
	case k_format_web_m:
		return "webm";
	case k_format_srt:
		return "srt";
	case k_format_count:
		break;
	}

	return std::string();
}

std::vector<ExportCodec::Codec> ExportFormat::get_video_codecs(ExportFormat::Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
		return { ExportCodec::k_codec_d_nx_hd };
	case k_format_matroska:
		return { ExportCodec::k_codec_h264, ExportCodec::k_codec_h264rgb,
				 ExportCodec::k_codec_h265, ExportCodec::k_codec_v_p9 };
	case k_format_mpe_g4_video:
		return { ExportCodec::k_codec_h264, ExportCodec::k_codec_h264rgb,
				 ExportCodec::k_codec_h265 };
	case k_format_open_exr:
		return { ExportCodec::k_codec_open_exr };
	case k_format_png:
		return { ExportCodec::k_codec_png };
	case k_format_tiff:
		return { ExportCodec::k_codec_tiff };
	case k_format_quick_time:
		return { ExportCodec::k_codec_h264, ExportCodec::k_codec_h264rgb,
				 ExportCodec::k_codec_h265, ExportCodec::k_codec_pro_res,
				 ExportCodec::k_codec_cineform };
	case k_format_web_m:
		return { ExportCodec::k_codec_a_v1, ExportCodec::k_codec_v_p9 };
	case k_format_ogg:
	case k_format_wav:
	case k_format_mpe_g4_audio:
	case k_format_aiff:
	case k_format_m_p3:
	case k_format_flac:
	case k_format_srt:
	case k_format_count:
		break;
	}

	return {};
}

std::vector<ExportCodec::Codec> ExportFormat::get_audio_codecs(ExportFormat::Format f)
{
	switch (f) {
	// Video/audio formats
	case k_format_d_nx_hd:
		return { ExportCodec::k_codec_pcm };
	case k_format_matroska:
		return { ExportCodec::k_codec_aac,	ExportCodec::k_codec_m_p2,
				 ExportCodec::k_codec_m_p3,	ExportCodec::k_codec_pcm,
				 ExportCodec::k_codec_vorbis, ExportCodec::k_codec_opus,
				 ExportCodec::k_codec_flac };
	case k_format_mpe_g4_video:
	case k_format_mpe_g4_audio:
		return { ExportCodec::k_codec_aac, ExportCodec::k_codec_m_p2,
				 ExportCodec::k_codec_m_p3 };
	case k_format_quick_time:
		return { ExportCodec::k_codec_aac, ExportCodec::k_codec_m_p2,
				 ExportCodec::k_codec_m_p3, ExportCodec::k_codec_pcm };
	case k_format_web_m:
		return { ExportCodec::k_codec_opus, ExportCodec::k_codec_aac,
				 ExportCodec::k_codec_m_p2,  ExportCodec::k_codec_m_p3,
				 ExportCodec::k_codec_pcm,  ExportCodec::k_codec_vorbis };

	// Audio only formats
	case k_format_wav:
		return { ExportCodec::k_codec_pcm };
	case k_format_aiff:
		return { ExportCodec::k_codec_pcm };
	case k_format_m_p3:
		return { ExportCodec::k_codec_m_p3 };
	case k_format_flac:
		return { ExportCodec::k_codec_flac };
	case k_format_ogg:
		return { ExportCodec::k_codec_opus, ExportCodec::k_codec_vorbis,
				 ExportCodec::k_codec_pcm };

	// Video only formats
	case k_format_open_exr:
	case k_format_png:
	case k_format_tiff:
	case k_format_srt:
	case k_format_count:
		break;
	}

	return {};
}

std::vector<ExportCodec::Codec> ExportFormat::get_subtitle_codecs(Format f)
{
	switch (f) {
	case k_format_d_nx_hd:
	case k_format_mpe_g4_video:
	case k_format_mpe_g4_audio:
	case k_format_open_exr:
	case k_format_quick_time:
	case k_format_png:
	case k_format_tiff:
	case k_format_wav:
	case k_format_aiff:
	case k_format_m_p3:
	case k_format_flac:
	case k_format_ogg:
	case k_format_web_m:
	case k_format_count:
		break;
	case k_format_matroska:
	case k_format_srt:
		return { ExportCodec::k_codec_srt };
	}

	return {};
}

std::vector<std::string> ExportFormat::get_pixel_formats_for_codec(ExportFormat::Format f,
												  ExportCodec::Codec c)
{
	Encoder *e = Encoder::create_from_format(f, EncodingParams());
	std::vector<std::string> list;

	if (e) {
		list = e->get_pixel_formats_for_codec(c);
		delete e;
	}

	return list;
}

std::vector<core::SampleFormat>
ExportFormat::get_sample_formats_for_codec(Format format, ExportCodec::Codec c)
{
	std::vector<core::SampleFormat> f;
	Encoder *e = Encoder::create_from_format(format, EncodingParams());

	if (e) {
		f = e->get_sample_formats_for_codec(c);
		delete e;
	}

	return f;
}

}
