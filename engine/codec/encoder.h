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

#include <memory>
#include <QRegularExpression>
#include <QString>
#include <QXmlStreamWriter>

#include "codec/exportcodec.h"
#include "codec/exportformat.h"
#include "codec/frame.h"
#include "node/block/subtitle/subtitle.h"
#include "render/colortransform.h"
#include "render/subtitleparams.h"
#include "render/videoparams.h"
//这个代码也许是导出编码视频用的？
namespace olive
{

class Encoder;
using EncoderPtr = std::shared_ptr<Encoder>;

class EncodingParams {
public:
	enum VideoScalingMethod { k_fit, k_stretch, k_crop };

	EncodingParams();

	static QDir get_preset_path();
	static QStringList get_list_of_presets();

	bool is_valid() const
	{
		return video_enabled_ || audio_enabled_ || subtitles_enabled_;
	}

	void set_filename(const QString &filename)
	{
		filename_ = filename;
	}

	void enable_video(const VideoParams &video_params,
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

	void set_video_option(const QString &key, const QString &value)
	{
		video_opts_.insert(key, value);
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
	void set_video_pix_fmt(const QString &s)
	{
		video_pix_fmt_ = s;
	}
	void set_video_is_image_sequence(bool s)
	{
		video_is_image_sequence_ = s;
	}
	void set_color_transform(const ColorTransform &color_transform)
	{
		color_transform_ = color_transform;
	}

	const QString &filename() const
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
	const VideoParams &video_params() const
	{
		return video_params_;
	}
	const QHash<QString, QString> &video_opts() const
	{
		return video_opts_;
	}
	QString video_option(const QString &key) const
	{
		return video_opts_.value(key);
	}
	bool has_video_opt(const QString &key) const
	{
		return video_opts_.contains(key);
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
	const QString &video_pix_fmt() const
	{
		return video_pix_fmt_;
	}
	bool video_is_image_sequence() const
	{
		return video_is_image_sequence_;
	}
	const ColorTransform &color_transform() const
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

	bool load(QIODevice *device);
	bool load(QXmlStreamReader *reader);

	void save(QIODevice *device) const;
	void save(QXmlStreamWriter *writer) const;

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

	static QMatrix4x4 generate_matrix(VideoScalingMethod method,
									 int source_width, int source_height,
									 int dest_width, int dest_height);

private:
	static const int k_encoder_params_version = 1;

	bool load_v1(QXmlStreamReader *reader);

	QString filename_;
	ExportFormat::Format format_ = ExportFormat::k_format_count;

	bool video_enabled_;
	ExportCodec::Codec video_codec_ = ExportCodec::k_codec_count;
	VideoParams video_params_;
	QHash<QString, QString> video_opts_;
	int64_t video_bit_rate_;
	int64_t video_min_bit_rate_;
	int64_t video_max_bit_rate_;
	int64_t video_buffer_size_;
	int video_threads_;
	QString video_pix_fmt_;
	bool video_is_image_sequence_;
	ColorTransform color_transform_;

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

class Encoder : public QObject {
	Q_OBJECT
public:
	Encoder(const EncodingParams &params);

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

	virtual QStringList get_pixel_formats_for_codec(ExportCodec::Codec c) const;
	virtual std::vector<SampleFormat>
	get_sample_formats_for_codec(ExportCodec::Codec c) const;

	const EncodingParams &params() const;

	virtual PixelFormat get_desired_pixel_format() const
	{
		return PixelFormat::invalid;
	}

	const QString &get_error() const
	{
		return error_;
	}

	QString get_filename_for_frame(const Rational &frame);

	static int get_image_sequence_placeholder_digit_count(const QString &filename);

	static bool filename_contains_digit_placeholder(const QString &filename);
	static QString filename_remove_digit_placeholder(QString filename);

	static const QRegularExpression k_image_sequence_contains_digits;
	static const QRegularExpression k_image_sequence_remove_digits;

public slots:
	virtual bool open() = 0;

	virtual bool write_frame(olive::FramePtr frame,
							olive::core::Rational time) = 0;
	virtual bool write_audio(const olive::SampleBuffer &audio) = 0;
	virtual bool write_subtitle(const SubtitleBlock *sub_block) = 0;

	virtual void close() = 0;

protected:
	void set_error(const QString &err)
	{
		error_ = err;
	}

private:
	EncodingParams params_;

	QString error_;
};

}

#endif // OAK_ENCODER_H
