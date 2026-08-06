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

#ifndef OAK_FFMPEGDECODER_H
#define OAK_FFMPEGDECODER_H

#include <inttypes.h>

#include <list>
#include <string>
#include <vector>

#include <ffmpeg_bridge/ffmpeg_bridge.h>

#include "decoder.h"
#include "ffmpeg/avframeptr.h"

namespace olive
{

/**
 * @brief A Decoder derivative that uses the ffmpeg_bridge library as an Olive decoder
 *
 * All media access goes through the pure C API of the ffmpeg_bridge shared
 * library; this class never sees an FFmpeg structure or function.
 */
class FFmpegDecoder : public Decoder {
public:
	// Constructor
	FFmpegDecoder();

	// Destructor
	DECODER_DEFAULT_DESTRUCTOR(FFmpegDecoder)

	virtual std::string id() const override;

	virtual bool supports_video() override
	{
		return true;
	}
	virtual bool supports_audio() override
	{
		return true;
	}

	virtual FootageDescription probe(const std::string &filename,
									 OakCancelAtom *cancelled) const override;

protected:
	virtual bool open_internal() override;
	virtual OakRenderTexture *
	retrieve_video_internal(const RetrieveVideoParams &p) override;
	virtual FramePtr
	retrieve_video_frame_internal(const RetrieveVideoParams &p) override;
	virtual bool
	conform_audio_internal(const std::vector<std::string> &filenames,
						   const AudioParams &params,
						   OakCancelAtom *cancelled) override;
	virtual void close_internal() override;

	virtual Rational get_audio_start_offset() const override;

private:
	/**
   * @brief Handle a bridge error code
   *
   * Uses the bridge API to retrieve a descriptive string for this error code and sends it to Error(). As such, this
   * function also automatically closes the Decoder.
   *
   * @param error_code
   */
	static std::string f_fmpeg_error(int error_code);

	void free_scaler();

	AVFramePtr transfer_hardware_frame(AVFramePtr f);

	static PixelFormat get_native_pixel_format(int pix_fmt);
	static int get_native_channel_count(int pix_fmt);

	static bool is_pixel_format_glsl_compatible(int f);

	AVFramePtr get_frame_from_cache(const int64_t &t) const;

	void clear_frame_cache();

	AVFramePtr pre_process_frame(AVFramePtr f, const RetrieveVideoParams &p);

	OakRenderTexture *process_frame_into_texture(AVFramePtr f,
											 const RetrieveVideoParams &p,
											 const AVFramePtr original);

	AVFramePtr retrieve_frame(const Rational &time, OakCancelAtom *cancelled);

	void remove_first_frame();

	static int maximum_queue_size();

	FBScaler *scaler_;
	int scaler_src_width_;
	int scaler_src_height_;
	int scaler_src_format_;
	int scaler_dst_width_;
	int scaler_dst_height_;
	int scaler_dst_format_;
	int scaler_colrange_;
	int scaler_colspace_;

	FBPacket *working_packet_;

	int64_t second_ts_;

	std::list<AVFramePtr> cached_frames_;

	bool cache_at_zero_;
	bool cache_at_eof_;

	FBDecoder *instance_;

	// Stream parameters cached on open (the stream object itself lives
	// inside the bridge library)
	Rational stream_time_base_;
	int64_t stream_start_time_;
	int64_t stream_duration_;
	int64_t format_start_time_;
	int input_sample_format_;
	int input_sample_rate_;
	uint64_t input_channel_layout_mask_;
};

}

#endif // OAK_FFMPEGDECODER_H
