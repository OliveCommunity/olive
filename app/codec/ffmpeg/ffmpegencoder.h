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

#ifndef OAK_FFMPEGENCODER_H
#define OAK_FFMPEGENCODER_H

#include <ffmpeg_bridge/ffmpeg_bridge.h>

#include "codec/encoder.h"

namespace olive
{

/**
 * @brief An Encoder derivative that uses the ffmpeg_bridge library for encoding
 *
 * All encoding work happens inside the ffmpeg_bridge shared library through
 * its pure C API; this class only translates EncodingParams into a bridge
 * configuration and forwards calls.
 */
class FFmpegEncoder : public Encoder {
	Q_OBJECT
public:
	FFmpegEncoder(const EncodingParams &params);

	virtual QStringList
	get_pixel_formats_for_codec(ExportCodec::Codec c) const override;

	virtual std::vector<SampleFormat>
	get_sample_formats_for_codec(ExportCodec::Codec c) const override;

	virtual bool open() override;

	virtual bool write_frame(olive::FramePtr frame,
							olive::core::Rational time) override;

	virtual bool write_audio(const olive::SampleBuffer &audio) override;

	bool write_audio_data(const AudioParams &audio_params, const uint8_t **data,
						int input_sample_count);

	virtual bool write_subtitle(const SubtitleBlock *sub_block) override;

	virtual void close() override;

	virtual PixelFormat get_desired_pixel_format() const override
	{
		return video_conversion_fmt_;
	}

	/**
	 * @brief Derives nclc color tags from an output colorspace name
	 *
	 * Extracted for testability. Returns true when the name maps to
	 * explicit tags (PQ/HLG/BT.2020, sRGB, P3, Rec.601, Rec.709); returns
	 * false for unknown names, in which case the bridge's legacy
	 * Rec.709/sRGB inference applies.
	 */
	static bool get_color_tags_for_colorspace(const QString &colorspace,
											  int *primaries, int *trc,
											  int *matrix);

private:
	/**
   * @brief Copy the last error message from the bridge into the encoder error state
   */
	void set_error_from_bridge();

	static int export_codec_to_bridge(ExportCodec::Codec c);

	FBEncoder *encoder_;

	PixelFormat video_conversion_fmt_;

	bool open_;
};

}

#endif // OAK_FFMPEGENCODER_H
