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

#ifndef OAK_ENCODER_H
#define OAK_ENCODER_H

#include <array>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "olive/core/render/audioparams.h"
#include "olive/core/render/pixelformat.h"
#include "olive/core/render/samplebuffer.h"
#include "olive/core/render/sampleformat.h"
#include "olive/core/util/rational.h"
#include "olive/core/util/timerange.h"

#include "common/colortransform.h"
#include "common/videoparams.h"
#include "exportcodec.h"
#include "exportformat.h"
#include "frame.h"
#include "xmlutils.h"

namespace olive
{

using core::AudioParams;
using core::PixelFormat;
using core::Rational;
using core::SampleBuffer;
using core::SampleFormat;
using core::TimeRange;

class Encoder;
using EncoderPtr = std::shared_ptr<Encoder>;

/**
 * @brief Parameters for an export encode
 *
 * Holds OakVideoParams / OakColorTransform C handles directly (no adapter
 * layer). Copy constructor/assignment addref the handles, destructor
 * releases them, so EncodingParams remains safely copyable by value
 * (Encoder::params_ stores a copy).
 */
class EncodingParams {
public:
	enum VideoScalingMethod { k_fit, k_stretch, k_crop };

	EncodingParams();
	EncodingParams(const EncodingParams &other);
	EncodingParams &operator=(const EncodingParams &other);
	~EncodingParams();

	static std::string get_preset_path();
	static std::vector<std::string> get_list_of_presets();

	bool is_valid() const
	{
		return video_enabled_ || audio_enabled_ || subtitles_enabled_;
	}

	void set_filename(const std::string &filename)
	{
		filename_ = filename;
	}

	/**
	 * @brief Enable video with the given parameter set
	 *
	 * addrefs @p video_params; the caller keeps ownership of its own
	 * reference.
	 */
	void enable_video(const OakVideoParams &video_params,
					 const ExportCodec::Codec &vcodec);
	void enable_audio(const AudioParams &audio_params,
					 const ExportCodec::Codec &acodec);
	void enable_subtitles(const ExportCodec::Codec &scodec);
	void enable_sidecar_subtitles(const ExportFormat::Format &sfmt,
								const ExportCodec::Codec &scodec);

	void disable_video();
	void disable_audio();
	void disable_subtitles();

	const ExportFormat::Format &format() const
	{
		return format_;
	}
	void set_format(const ExportFormat::Format &format)
	{
		format_ = format;
	}

	void set_video_option(const std::string &key, const std::string &value)
	{
		video_opts_[key] = value;
	}
	void set_video_bit_rate(const int64_t &rate)
	{
		video_bit_rate_ = rate;
	}
	void set_video_min_bit_rate(const int64_t &rate)
	{
		video_min_bit_rate_ = rate;
	}
	void set_video_max_bit_rate(const int64_t &rate)
	{
		video_max_bit_rate_ = rate;
	}
	void set_video_buffer_size(const int64_t &sz)
	{
		video_buffer_size_ = sz;
	}
	void set_video_threads(const int &threads)
	{
		video_threads_ = threads;
	}
	void set_video_pix_fmt(const std::string &s)
	{
		video_pix_fmt_ = s;
	}
	void set_video_is_image_sequence(bool s)
	{
		video_is_image_sequence_ = s;
	}
	/**
	 * @brief Set the export color transform
	 *
	 * addrefs @p color_transform and releases the previously held handle.
	 */
	void set_color_transform(const OakColorTransform &color_transform);

	const std::string &filename() const
	{
		return filename_;
	}

	bool video_enabled() const
	{
		return video_enabled_;
	}
	const ExportCodec::Codec &video_codec() const
	{
		return video_codec_;
	}
	/**
	 * @brief Borrowed video parameter handle (valid while this object lives)
	 */
	const OakVideoParams &video_params() const
	{
		return video_params_;
	}
	const std::map<std::string, std::string> &video_opts() const
	{
		return video_opts_;
	}
	std::string video_option(const std::string &key) const
	{
		auto it = video_opts_.find(key);
		return it != video_opts_.end() ? it->second : std::string();
	}
	bool has_video_opt(const std::string &key) const
	{
		return video_opts_.count(key) > 0;
	}
	const int64_t &video_bit_rate() const
	{
		return video_bit_rate_;
	}
	const int64_t &video_min_bit_rate() const
	{
		return video_min_bit_rate_;
	}
	const int64_t &video_max_bit_rate() const
	{
		return video_max_bit_rate_;
	}
	const int64_t &video_buffer_size() const
	{
		return video_buffer_size_;
	}
	const int &video_threads() const
	{
		return video_threads_;
	}
	const std::string &video_pix_fmt() const
	{
		return video_pix_fmt_;
	}
	bool video_is_image_sequence() const
	{
		return video_is_image_sequence_;
	}
	/**
	 * @brief Borrowed color transform handle (valid while this object lives)
	 */
	const OakColorTransform &color_transform() const
	{
		return color_transform_;
	}

	bool audio_enabled() const
	{
		return audio_enabled_;
	}
	const ExportCodec::Codec &audio_codec() const
	{
		return audio_codec_;
	}
	const AudioParams &audio_params() const
	{
		return audio_params_;
	}
	const int64_t &audio_bit_rate() const
	{
		return audio_bit_rate_;
	}

	void set_audio_bit_rate(const int64_t &b)
	{
		audio_bit_rate_ = b;
	}

	bool subtitles_enabled() const
	{
		return subtitles_enabled_;
	}
	bool subtitles_are_sidecar() const
	{
		return subtitles_are_sidecar_;
	}
	ExportFormat::Format subtitle_sidecar_fmt() const
	{
		return subtitle_sidecar_fmt_;
	}
	ExportCodec::Codec subtitles_codec() const
	{
		return subtitles_codec_;
	}

	const Rational &get_export_length() const
	{
		return export_length_;
	}
	void set_export_length(const Rational &export_length)
	{
		export_length_ = export_length;
	}

	bool load(const std::string &xml);
	bool load(XmlStreamReader *reader);

	std::string save_to_string() const;
	void save(XmlStreamWriter *writer) const;

	bool has_custom_range() const
	{
		return has_custom_range_;
	}
	const TimeRange &custom_range() const
	{
		return custom_range_;
	}
	void set_custom_range(const TimeRange &custom_range)
	{
		has_custom_range_ = true;
		custom_range_ = custom_range;
	}

	const VideoScalingMethod &video_scaling_method() const
	{
		return video_scaling_method_;
	}
	void
	set_video_scaling_method(const VideoScalingMethod &video_scaling_method)
	{
		video_scaling_method_ = video_scaling_method;
	}

	/**
	 * @brief Generate a scaling matrix for the given scaling method
	 *
	 * De-Qt note: formerly returned QMatrix4x4. Now returns 16 floats in
	 * row-major order (m[row * 4 + column], matching QMatrix4x4's
	 * operator()(row, column) layout). The result is always a diagonal
	 * matrix: identity for k_stretch (or aspect-equal sources), otherwise
	 * a uniform axis scale at (0,0) and (1,1).
	 */
	static std::array<float, 16>
	generate_matrix(VideoScalingMethod method, int source_width,
					int source_height, int dest_width, int dest_height);

private:
	static const int k_encoder_params_version = 1;

	bool load_v1(XmlStreamReader *reader);

	std::string filename_;
	ExportFormat::Format format_ = ExportFormat::k_format_count;

	bool video_enabled_;
	ExportCodec::Codec video_codec_ = ExportCodec::k_codec_count;
	OakVideoParams video_params_;
	std::map<std::string, std::string> video_opts_;
	int64_t video_bit_rate_;
	int64_t video_min_bit_rate_;
	int64_t video_max_bit_rate_;
	int64_t video_buffer_size_;
	int video_threads_;
	std::string video_pix_fmt_;
	bool video_is_image_sequence_;
	OakColorTransform color_transform_;

	bool audio_enabled_;
	ExportCodec::Codec audio_codec_ = ExportCodec::k_codec_count;
	AudioParams audio_params_;
	int64_t audio_bit_rate_;

	bool subtitles_enabled_;
	bool subtitles_are_sidecar_;
	ExportFormat::Format subtitle_sidecar_fmt_ = ExportFormat::k_format_count;
	ExportCodec::Codec subtitles_codec_ = ExportCodec::k_codec_count;

	Rational export_length_;
	VideoScalingMethod video_scaling_method_;

	bool has_custom_range_;
	TimeRange custom_range_;
};

class Encoder {
public:
	Encoder(const EncodingParams &params);

	virtual ~Encoder() = default;

	enum Type { k_encoder_type_none = -1, k_encoder_type_f_fmpeg, k_encoder_type_oiio };

	/**
   * @brief Create a Encoder instance using a Encoder ID
   *
   * @return
   *
   * A Encoder instance or nullptr if a Decoder with this ID does not exist
   */
	static Encoder *create_from_id(Type id, const EncodingParams &params);

	static Type get_type_from_format(ExportFormat::Format f);

	static Encoder *create_from_format(ExportFormat::Format f,
									 const EncodingParams &params);

	static Encoder *create_from_params(const EncodingParams &params);

	virtual std::vector<std::string>
	get_pixel_formats_for_codec(ExportCodec::Codec c) const;
	virtual std::vector<SampleFormat>
	get_sample_formats_for_codec(ExportCodec::Codec c) const;

	const EncodingParams &params() const;

	virtual PixelFormat get_desired_pixel_format() const
	{
		return PixelFormat::invalid;
	}

	const std::string &get_error() const
	{
		return error_;
	}

	std::string get_filename_for_frame(const Rational &frame);

	static int get_image_sequence_placeholder_digit_count(const std::string &filename);

	static bool filename_contains_digit_placeholder(const std::string &filename);
	static std::string filename_remove_digit_placeholder(std::string filename);

	static const std::regex k_image_sequence_contains_digits;
	static const std::regex k_image_sequence_remove_digits;

	virtual bool open() = 0;

	virtual bool write_frame(olive::FramePtr frame,
							olive::core::Rational time) = 0;
	virtual bool write_audio(const olive::SampleBuffer &audio) = 0;

	/**
	 * @brief Write one subtitle entry
	 *
	 * De-Qt note: formerly took a `const SubtitleBlock *` (an oaknode C++
	 * type). Now takes the flattened text and in/out times in seconds;
	 * callers extract them from the subtitle block via the oaknode C API.
	 */
	virtual bool write_subtitle(const char *text, double in_seconds,
								double out_seconds) = 0;

	virtual void close() = 0;

protected:
	void set_error(const std::string &err)
	{
		error_ = err;
	}

private:
	EncodingParams params_;

	std::string error_;
};

}

#endif // OAK_ENCODER_H
