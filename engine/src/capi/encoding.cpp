/***

  Oak - Non-Linear Video Editor
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

#include "oakengine/encoding.h"
#include "oakengine/exporter.h"

#include <QFile>
#include <QMatrix4x4>
#include <QString>

#include "audio/audiomanager.h"
#include "coreengine.h"
#include "exportinternal.h"
#include "node/project.h"
#include "node/project/sequence/sequence.h"
#include "render/rendermanager.h"
#include "codec/encoder.h"
#include "codec/ffmpeg/ffmpegencoder.h"
#include "node/output/viewer/viewer.h"

namespace
{

// buf/size convention: returns the would-be length excluding the NUL.
int string_to_buf(const QString &s, char *buf, int buf_size)
{
	const QByteArray utf = s.toUtf8();
	if (buf && buf_size > 0) {
		snprintf(buf, size_t(buf_size), "%s", utf.constData());
	}
	return int(utf.size());
}

bool valid_format(int format)
{
	return format >= 0 && format < olive::ExportFormat::k_format_count;
}

bool valid_codec(int codec)
{
	return codec >= 0 && codec < olive::ExportCodec::k_codec_count;
}

olive::VideoParams to_cpp(const oak_video_params &v)
{
	olive::VideoParams vp(
		v.width, v.height,
		olive::Rational(v.time_base_num, v.time_base_den),
		static_cast<olive::PixelFormat::Format>(v.format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(v.pixel_aspect_num, v.pixel_aspect_den),
		static_cast<olive::VideoParams::Interlacing>(v.interlacing),
		v.divider > 0 ? v.divider : 1);
	vp.set_color_range(static_cast<olive::VideoParams::ColorRange>(v.color_range));
	return vp;
}

void from_cpp(const olive::VideoParams &vp, oak_video_params *out)
{
	out->width = vp.width();
	out->height = vp.height();
	out->time_base_num = vp.time_base().numerator();
	out->time_base_den = vp.time_base().denominator();
	out->format = int(vp.format());
	out->pixel_aspect_num = vp.pixel_aspect_ratio().numerator();
	out->pixel_aspect_den = vp.pixel_aspect_ratio().denominator();
	out->interlacing = int(vp.interlacing());
	out->color_range = int(vp.color_range());
	out->divider = vp.divider();
}

olive::EncodingParams *impl(OakEngineEncodingParams *p)
{
	return reinterpret_cast<olive::EncodingParams *>(p);
}

const olive::EncodingParams *impl(const OakEngineEncodingParams *p)
{
	return reinterpret_cast<const olive::EncodingParams *>(p);
}

} // namespace

struct OakEngineEncodingParams : public olive::EncodingParams {
};

extern "C"
{

/* ---- Container format / codec metadata ---------------------------------- */

int oakengine_encoding_format_count(void)
{
	return olive::ExportFormat::k_format_count;
}

int oakengine_encoding_format_name(int format, char *buf, int buf_size)
{
	if (!valid_format(format)) {
		return -1;
	}
	return string_to_buf(
		olive::ExportFormat::get_name(olive::ExportFormat::Format(format)), buf,
		buf_size);
}

int oakengine_encoding_format_extension(int format, char *buf, int buf_size)
{
	if (!valid_format(format)) {
		return -1;
	}
	return string_to_buf(
		olive::ExportFormat::get_extension(olive::ExportFormat::Format(format)),
		buf, buf_size);
}

int oakengine_encoding_format_video_codec_count(int format)
{
	if (!valid_format(format)) {
		return -1;
	}
	return olive::ExportFormat::get_video_codecs(
			   olive::ExportFormat::Format(format))
		.size();
}

int oakengine_encoding_format_video_codec_at(int format, int index)
{
	if (!valid_format(format)) {
		return -1;
	}
	const auto l =
		olive::ExportFormat::get_video_codecs(olive::ExportFormat::Format(format));
	return (index >= 0 && index < l.size()) ? int(l.at(index)) : -1;
}

int oakengine_encoding_format_audio_codec_count(int format)
{
	if (!valid_format(format)) {
		return -1;
	}
	return olive::ExportFormat::get_audio_codecs(
			   olive::ExportFormat::Format(format))
		.size();
}

int oakengine_encoding_format_audio_codec_at(int format, int index)
{
	if (!valid_format(format)) {
		return -1;
	}
	const auto l =
		olive::ExportFormat::get_audio_codecs(olive::ExportFormat::Format(format));
	return (index >= 0 && index < l.size()) ? int(l.at(index)) : -1;
}

int oakengine_encoding_format_subtitle_codec_count(int format)
{
	if (!valid_format(format)) {
		return -1;
	}
	return olive::ExportFormat::get_subtitle_codecs(
			   olive::ExportFormat::Format(format))
		.size();
}

int oakengine_encoding_format_subtitle_codec_at(int format, int index)
{
	if (!valid_format(format)) {
		return -1;
	}
	const auto l = olive::ExportFormat::get_subtitle_codecs(
		olive::ExportFormat::Format(format));
	return (index >= 0 && index < l.size()) ? int(l.at(index)) : -1;
}

int oakengine_encoding_codec_name(int codec, char *buf, int buf_size)
{
	if (!valid_codec(codec)) {
		return -1;
	}
	return string_to_buf(
		olive::ExportCodec::get_codec_name(olive::ExportCodec::Codec(codec)), buf,
		buf_size);
}

int oakengine_encoding_codec_is_still_image(int codec)
{
	if (!valid_codec(codec)) {
		return 0;
	}
	return olive::ExportCodec::is_codec_a_still_image(
			   olive::ExportCodec::Codec(codec)) ?
			   1 :
			   0;
}

int oakengine_encoding_codec_is_lossless(int codec)
{
	if (!valid_codec(codec)) {
		return 0;
	}
	return olive::ExportCodec::is_codec_lossless(olive::ExportCodec::Codec(codec)) ?
			   1 :
			   0;
}

int oakengine_encoding_pix_fmt_count(int format, int codec)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return -1;
	}
	return olive::ExportFormat::get_pixel_formats_for_codec(
			   olive::ExportFormat::Format(format),
			   olive::ExportCodec::Codec(codec))
		.size();
}

int oakengine_encoding_pix_fmt_at(int format, int codec, int index, char *buf,
								  int buf_size)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return -1;
	}
	const QStringList l = olive::ExportFormat::get_pixel_formats_for_codec(
		olive::ExportFormat::Format(format), olive::ExportCodec::Codec(codec));
	if (index < 0 || index >= l.size()) {
		return -1;
	}
	return string_to_buf(l.at(index), buf, buf_size);
}

int oakengine_encoding_pix_fmt_index(int codec, const char *pix_fmt)
{
	if (!valid_codec(codec) || !pix_fmt || !pix_fmt[0]) {
		return 0;
	}
	olive::FFmpegEncoder probe{ olive::EncodingParams() };
	const int index =
		probe.get_pixel_formats_for_codec(olive::ExportCodec::Codec(codec))
			.indexOf(QString::fromUtf8(pix_fmt));
	return index >= 0 ? index : 0;
}

int oakengine_encoding_sample_format_count(int format, int codec)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return -1;
	}
	return int(olive::ExportFormat::get_sample_formats_for_codec(
				   olive::ExportFormat::Format(format),
				   olive::ExportCodec::Codec(codec))
				   .size());
}

int oakengine_encoding_sample_format_at(int format, int codec, int index)
{
	if (!valid_format(format) || !valid_codec(codec)) {
		return -1;
	}
	const auto l = olive::ExportFormat::get_sample_formats_for_codec(
		olive::ExportFormat::Format(format), olive::ExportCodec::Codec(codec));
	return (index >= 0 && index < int(l.size())) ? int(l[size_t(index)]) : -1;
}

/* ---- Image-sequence filename helpers ------------------------------------ */

int oakengine_encoding_filename_contains_digit_placeholder(const char *filename)
{
	if (!filename) {
		return 0;
	}
	return olive::Encoder::filename_contains_digit_placeholder(
			   QString::fromUtf8(filename)) ?
			   1 :
			   0;
}

int oakengine_encoding_image_sequence_digit_count(const char *filename)
{
	if (!filename) {
		return 0;
	}
	return olive::Encoder::get_image_sequence_placeholder_digit_count(
		QString::fromUtf8(filename));
}

int oakengine_encoding_filename_remove_digit_placeholder(const char *filename,
														 char *buf, int buf_size)
{
	if (!filename) {
		return -1;
	}
	return string_to_buf(olive::Encoder::filename_remove_digit_placeholder(
							 QString::fromUtf8(filename)),
						 buf, buf_size);
}

int oakengine_encoding_generate_matrix(int method, int src_width,
									   int src_height, int dest_width,
									   int dest_height, float out16[16])
{
	if (!out16 || method < 0 || method > 2 || src_width <= 0 || src_height <= 0 ||
		dest_width <= 0 || dest_height <= 0) {
		return OAKENGINE_E_INVALID;
	}
	const QMatrix4x4 m = olive::EncodingParams::generate_matrix(
		olive::EncodingParams::VideoScalingMethod(method), src_width, src_height,
		dest_width, dest_height);
	m.copyDataTo(out16);
	return OAKENGINE_OK;
}

/* ---- Encoding parameters handle ----------------------------------------- */

OakEngineEncodingParams *oakengine_encoding_params_create(void)
{
	return new OakEngineEncodingParams;
}

void oakengine_encoding_params_destroy(OakEngineEncodingParams *params)
{
	delete params;
}

int oakengine_encoding_params_is_valid(const OakEngineEncodingParams *params)
{
	return params && impl(params)->is_valid() ? 1 : 0;
}

int oakengine_encoding_params_set_filename(OakEngineEncodingParams *params,
										   const char *filename)
{
	if (!params || !filename) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->set_filename(QString::fromUtf8(filename));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_filename(const OakEngineEncodingParams *params,
									   char *buf, int buf_size)
{
	if (!params) {
		return -1;
	}
	return string_to_buf(impl(params)->filename(), buf, buf_size);
}

int oakengine_encoding_params_set_format(OakEngineEncodingParams *params,
										 int format)
{
	if (!params || !valid_format(format)) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->set_format(olive::ExportFormat::Format(format));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_format(const OakEngineEncodingParams *params)
{
	if (!params || impl(params)->format() == olive::ExportFormat::k_format_count) {
		return -1;
	}
	return int(impl(params)->format());
}

int oakengine_encoding_params_enable_video(OakEngineEncodingParams *params,
										   const oak_video_params *video,
										   int codec)
{
	if (!params || !video || !valid_codec(codec) || video->width <= 0 ||
		video->height <= 0 || video->time_base_num <= 0 ||
		video->time_base_den <= 0) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->enable_video(to_cpp(*video), olive::ExportCodec::Codec(codec));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_enable_audio(OakEngineEncodingParams *params,
										   int sample_rate,
										   uint64_t channel_layout,
										   int sample_format, int codec)
{
	if (!params || !valid_codec(codec) || sample_rate <= 0 ||
		channel_layout == 0) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->enable_audio(
		olive::AudioParams(sample_rate, channel_layout,
						   olive::core::SampleFormat::Format(sample_format)),
		olive::ExportCodec::Codec(codec));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_enable_subtitles(OakEngineEncodingParams *params,
											   int codec)
{
	if (!params || !valid_codec(codec)) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->enable_subtitles(olive::ExportCodec::Codec(codec));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_enable_sidecar_subtitles(
	OakEngineEncodingParams *params, int format, int codec)
{
	if (!params || !valid_format(format) || !valid_codec(codec)) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->enable_sidecar_subtitles(olive::ExportFormat::Format(format),
										   olive::ExportCodec::Codec(codec));
	return OAKENGINE_OK;
}

void oakengine_encoding_params_disable_video(OakEngineEncodingParams *params)
{
	if (params) {
		impl(params)->disable_video();
	}
}

void oakengine_encoding_params_disable_audio(OakEngineEncodingParams *params)
{
	if (params) {
		impl(params)->disable_audio();
	}
}

void oakengine_encoding_params_disable_subtitles(OakEngineEncodingParams *params)
{
	if (params) {
		impl(params)->disable_subtitles();
	}
}

int oakengine_encoding_params_video_enabled(const OakEngineEncodingParams *params)
{
	return params && impl(params)->video_enabled() ? 1 : 0;
}

int oakengine_encoding_params_video_codec(const OakEngineEncodingParams *params)
{
	return params ? int(impl(params)->video_codec()) : -1;
}

int oakengine_encoding_params_get_video_params(
	const OakEngineEncodingParams *params, oak_video_params *out)
{
	if (!params || !out) {
		return OAKENGINE_E_INVALID;
	}
	if (!impl(params)->video_enabled()) {
		return OAKENGINE_E_STATE;
	}
	from_cpp(impl(params)->video_params(), out);
	return OAKENGINE_OK;
}

int oakengine_encoding_params_audio_enabled(const OakEngineEncodingParams *params)
{
	return params && impl(params)->audio_enabled() ? 1 : 0;
}

int oakengine_encoding_params_audio_codec(const OakEngineEncodingParams *params)
{
	return params ? int(impl(params)->audio_codec()) : -1;
}

int oakengine_encoding_params_get_audio_params(
	const OakEngineEncodingParams *params, int *sample_rate,
	uint64_t *channel_layout, int *sample_format)
{
	if (!params) {
		return OAKENGINE_E_INVALID;
	}
	if (!impl(params)->audio_enabled()) {
		return OAKENGINE_E_STATE;
	}
	const olive::AudioParams &ap = impl(params)->audio_params();
	if (sample_rate) {
		*sample_rate = ap.sample_rate();
	}
	if (channel_layout) {
		*channel_layout = ap.channel_layout();
	}
	if (sample_format) {
		*sample_format = int(ap.format());
	}
	return OAKENGINE_OK;
}

int oakengine_encoding_params_subtitles_enabled(
	const OakEngineEncodingParams *params)
{
	return params && impl(params)->subtitles_enabled() ? 1 : 0;
}

int oakengine_encoding_params_subtitles_are_sidecar(
	const OakEngineEncodingParams *params)
{
	return params && impl(params)->subtitles_are_sidecar() ? 1 : 0;
}

int oakengine_encoding_params_subtitles_sidecar_format(
	const OakEngineEncodingParams *params)
{
	return params ? int(impl(params)->subtitle_sidecar_fmt()) : -1;
}

int oakengine_encoding_params_subtitles_codec(
	const OakEngineEncodingParams *params)
{
	return params ? int(impl(params)->subtitles_codec()) : -1;
}

void oakengine_encoding_params_set_video_bit_rate(
	OakEngineEncodingParams *params, int64_t rate)
{
	if (params) {
		impl(params)->set_video_bit_rate(rate);
	}
}

int64_t
oakengine_encoding_params_video_bit_rate(const OakEngineEncodingParams *params)
{
	return params ? impl(params)->video_bit_rate() : 0;
}

void oakengine_encoding_params_set_video_min_bit_rate(
	OakEngineEncodingParams *params, int64_t rate)
{
	if (params) {
		impl(params)->set_video_min_bit_rate(rate);
	}
}

int64_t oakengine_encoding_params_video_min_bit_rate(
	const OakEngineEncodingParams *params)
{
	return params ? impl(params)->video_min_bit_rate() : 0;
}

void oakengine_encoding_params_set_video_max_bit_rate(
	OakEngineEncodingParams *params, int64_t rate)
{
	if (params) {
		impl(params)->set_video_max_bit_rate(rate);
	}
}

int64_t oakengine_encoding_params_video_max_bit_rate(
	const OakEngineEncodingParams *params)
{
	return params ? impl(params)->video_max_bit_rate() : 0;
}

void oakengine_encoding_params_set_video_buffer_size(
	OakEngineEncodingParams *params, int64_t size)
{
	if (params) {
		impl(params)->set_video_buffer_size(size);
	}
}

int64_t oakengine_encoding_params_video_buffer_size(
	const OakEngineEncodingParams *params)
{
	return params ? impl(params)->video_buffer_size() : 0;
}

void oakengine_encoding_params_set_video_threads(OakEngineEncodingParams *params,
												 int threads)
{
	if (params) {
		impl(params)->set_video_threads(threads);
	}
}

int oakengine_encoding_params_video_threads(
	const OakEngineEncodingParams *params)
{
	return params ? impl(params)->video_threads() : 0;
}

void oakengine_encoding_params_set_audio_bit_rate(
	OakEngineEncodingParams *params, int64_t rate)
{
	if (params) {
		impl(params)->set_audio_bit_rate(rate);
	}
}

int64_t
oakengine_encoding_params_audio_bit_rate(const OakEngineEncodingParams *params)
{
	return params ? impl(params)->audio_bit_rate() : 0;
}

int oakengine_encoding_params_set_video_pix_fmt(OakEngineEncodingParams *params,
												const char *pix_fmt)
{
	if (!params || !pix_fmt) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->set_video_pix_fmt(QString::fromUtf8(pix_fmt));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_video_pix_fmt(
	const OakEngineEncodingParams *params, char *buf, int buf_size)
{
	if (!params) {
		return -1;
	}
	return string_to_buf(impl(params)->video_pix_fmt(), buf, buf_size);
}

void oakengine_encoding_params_set_video_is_image_sequence(
	OakEngineEncodingParams *params, int is_image_sequence)
{
	if (params) {
		impl(params)->set_video_is_image_sequence(is_image_sequence != 0);
	}
}

int oakengine_encoding_params_video_is_image_sequence(
	const OakEngineEncodingParams *params)
{
	return params && impl(params)->video_is_image_sequence() ? 1 : 0;
}

int oakengine_encoding_params_set_color_transform(
	OakEngineEncodingParams *params, const char *output_name)
{
	if (!params) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->set_color_transform(
		olive::ColorTransform(QString::fromUtf8(output_name ? output_name : "")));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_color_transform_output(
	const OakEngineEncodingParams *params, char *buf, int buf_size)
{
	if (!params) {
		return -1;
	}
	return string_to_buf(impl(params)->color_transform().output(), buf, buf_size);
}

void oakengine_encoding_params_set_export_length(
	OakEngineEncodingParams *params, int num, int den)
{
	if (params && den != 0) {
		impl(params)->set_export_length(olive::Rational(num, den));
	}
}

int oakengine_encoding_params_get_export_length(
	const OakEngineEncodingParams *params, int *num, int *den)
{
	if (!params) {
		return OAKENGINE_E_INVALID;
	}
	const olive::Rational r = impl(params)->get_export_length();
	if (num) {
		*num = r.numerator();
	}
	if (den) {
		*den = r.denominator();
	}
	return OAKENGINE_OK;
}

void oakengine_encoding_params_set_custom_range(OakEngineEncodingParams *params,
												int64_t in_num, int64_t in_den,
												int64_t out_num,
												int64_t out_den)
{
	if (params && in_den != 0 && out_den != 0) {
		impl(params)->set_custom_range(
			olive::TimeRange(olive::Rational(in_num, in_den),
							 olive::Rational(out_num, out_den)));
	}
}

int oakengine_encoding_params_has_custom_range(
	const OakEngineEncodingParams *params)
{
	return params && impl(params)->has_custom_range() ? 1 : 0;
}

int oakengine_encoding_params_get_custom_range(
	const OakEngineEncodingParams *params, int64_t *in_num, int64_t *in_den,
	int64_t *out_num, int64_t *out_den)
{
	if (!params) {
		return OAKENGINE_E_INVALID;
	}
	if (!impl(params)->has_custom_range()) {
		return OAKENGINE_E_NOT_FOUND;
	}
	const olive::TimeRange &r = impl(params)->custom_range();
	if (in_num) {
		*in_num = r.in().numerator();
	}
	if (in_den) {
		*in_den = r.in().denominator();
	}
	if (out_num) {
		*out_num = r.out().numerator();
	}
	if (out_den) {
		*out_den = r.out().denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_encoding_params_set_video_scaling_method(
	OakEngineEncodingParams *params, int method)
{
	if (!params || method < 0 || method > 2) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->set_video_scaling_method(
		olive::EncodingParams::VideoScalingMethod(method));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_video_scaling_method(
	const OakEngineEncodingParams *params)
{
	return params ? int(impl(params)->video_scaling_method()) : -1;
}

int oakengine_encoding_params_set_video_option(OakEngineEncodingParams *params,
											   const char *key,
											   const char *value)
{
	if (!params || !key || !value) {
		return OAKENGINE_E_INVALID;
	}
	impl(params)->set_video_option(QString::fromUtf8(key),
								   QString::fromUtf8(value));
	return OAKENGINE_OK;
}

int oakengine_encoding_params_video_option(const OakEngineEncodingParams *params,
										   const char *key, char *buf,
										   int buf_size)
{
	if (!params || !key) {
		return -1;
	}
	const QString k = QString::fromUtf8(key);
	if (!impl(params)->has_video_opt(k)) {
		return OAKENGINE_E_NOT_FOUND;
	}
	return string_to_buf(impl(params)->video_option(k), buf, buf_size);
}

/* ---- Presets ------------------------------------------------------------- */

int oakengine_encoding_preset_path(char *buf, int buf_size)
{
	return string_to_buf(olive::EncodingParams::get_preset_path().absolutePath(),
						 buf, buf_size);
}

int oakengine_encoding_preset_count(void)
{
	return olive::EncodingParams::get_list_of_presets().size();
}

int oakengine_encoding_preset_name(int index, char *buf, int buf_size)
{
	const QStringList l = olive::EncodingParams::get_list_of_presets();
	if (index < 0 || index >= l.size()) {
		return -1;
	}
	return string_to_buf(l.at(index), buf, buf_size);
}

int oakengine_encoding_params_load_file(OakEngineEncodingParams *params,
										const char *path)
{
	if (!params || !path) {
		return OAKENGINE_E_INVALID;
	}
	QFile f(QString::fromUtf8(path));
	if (!f.open(QFile::ReadOnly)) {
		return OAKENGINE_E_FAILED;
	}
	const bool ok = impl(params)->load(&f);
	f.close();
	return ok ? OAKENGINE_OK : OAKENGINE_E_FAILED;
}

int oakengine_encoding_params_save_file(const OakEngineEncodingParams *params,
										const char *path)
{
	if (!params || !path) {
		return OAKENGINE_E_INVALID;
	}
	QFile f(QString::fromUtf8(path));
	if (!f.open(QFile::WriteOnly)) {
		return OAKENGINE_E_FAILED;
	}
	impl(params)->save(&f);
	f.close();
	return OAKENGINE_OK;
}

/* ---- Export execution / per-sequence last-used --------------------------- */

OakEngineEncodingParams *
oakengine_encoding_params_get_last_used(OakEngineSequence *seq)
{
	olive::ViewerOutput *viewer = reinterpret_cast<olive::ViewerOutput *>(seq);
	if (!viewer || !viewer->get_last_used_encoding_params().is_valid()) {
		return nullptr;
	}
	auto *copy = new OakEngineEncodingParams;
	*static_cast<olive::EncodingParams *>(copy) =
		viewer->get_last_used_encoding_params();
	return copy;
}

void oakengine_encoding_params_set_last_used(
	OakEngineSequence *seq, const OakEngineEncodingParams *params)
{
	olive::ViewerOutput *viewer = reinterpret_cast<olive::ViewerOutput *>(seq);
	if (viewer && params) {
		viewer->set_last_used_encoding_params(*impl(params));
	}
}

int oakengine_encoding_start_audio_recording(
	const OakEngineEncodingParams *params, char *errbuf, int errbuf_size)
{
	if (!params || !impl(params)->audio_enabled()) {
		return OAKENGINE_E_INVALID;
	}
	if (!olive::AudioManager::instance()) {
		return OAKENGINE_E_STATE;
	}
	QString error;
	if (!olive::AudioManager::instance()->start_recording(*impl(params), &error)) {
		string_to_buf(error, errbuf, errbuf_size);
		return OAKENGINE_E_FAILED;
	}
	return OAKENGINE_OK;
}

/* ---- VideoParams static data (oakengine/videoparams.h) ------------------- */

int oakengine_video_params_supported_frame_rate_count(void)
{
	return olive::VideoParams::k_supported_frame_rates.size();
}

int oakengine_video_params_supported_frame_rate_at(int index, int *num, int *den)
{
	const auto &l = olive::VideoParams::k_supported_frame_rates;
	if (index < 0 || index >= l.size()) {
		return OAKENGINE_E_INVALID;
	}
	if (num) {
		*num = l.at(index).numerator();
	}
	if (den) {
		*den = l.at(index).denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_video_params_frame_rate_to_string(int num, int den, char *buf,
												int buf_size)
{
	if (den == 0) {
		return -1;
	}
	return string_to_buf(
		olive::VideoParams::frame_rate_to_string(olive::Rational(num, den)), buf,
		buf_size);
}

int oakengine_video_params_standard_pixel_aspect_count(void)
{
	return olive::VideoParams::k_standard_pixel_aspects.size();
}

int oakengine_video_params_standard_pixel_aspect_at(int index, int *num,
													int *den)
{
	const auto &l = olive::VideoParams::k_standard_pixel_aspects;
	if (index < 0 || index >= l.size()) {
		return OAKENGINE_E_INVALID;
	}
	if (num) {
		*num = l.at(index).numerator();
	}
	if (den) {
		*den = l.at(index).denominator();
	}
	return OAKENGINE_OK;
}

int oakengine_video_params_standard_pixel_aspect_name(int index, char *buf,
													  int buf_size)
{
	const QStringList l =
		olive::VideoParams::get_standard_pixel_aspect_ratio_names();
	if (index < 0 || index >= l.size()) {
		return -1;
	}
	return string_to_buf(l.at(index), buf, buf_size);
}

int oakengine_video_params_format_pixel_aspect_ratio_string(
	const char *format, int num, int den, char *buf, int buf_size)
{
	if (!format || den == 0) {
		return -1;
	}
	return string_to_buf(olive::VideoParams::format_pixel_aspect_ratio_string(
							 QString::fromUtf8(format), olive::Rational(num, den)),
						 buf, buf_size);
}

int oakengine_video_params_supported_divider_count(void)
{
	return olive::VideoParams::k_supported_dividers.size();
}

int oakengine_video_params_supported_divider_at(int index)
{
	const auto &l = olive::VideoParams::k_supported_dividers;
	return (index >= 0 && index < l.size()) ? l.at(index) : -1;
}

int oakengine_video_params_divider_name(int divider, char *buf, int buf_size)
{
	return string_to_buf(olive::VideoParams::get_name_for_divider(divider), buf,
						 buf_size);
}

int oakengine_video_params_format_is_float(int format)
{
	return olive::VideoParams::format_is_float(olive::PixelFormat::Format(format)) ?
			   1 :
			   0;
}

int oakengine_video_params_pixel_format_name(int format, char *buf,
											 int buf_size)
{
	return string_to_buf(olive::VideoParams::get_format_name(
							 olive::PixelFormat::Format(format)),
						 buf, buf_size);
}

int oakengine_video_params_effective_size(int width, int height, int divider,
										  int *out_width, int *out_height)
{
	if (width <= 0 || height <= 0 || divider <= 0) {
		return OAKENGINE_E_INVALID;
	}
	if (out_width) {
		*out_width = olive::VideoParams::get_scaled_dimension(width, divider);
	}
	if (out_height) {
		*out_height = olive::VideoParams::get_scaled_dimension(height, divider);
	}
	return OAKENGINE_OK;
}

int oakengine_video_params_make(oak_video_params *p, int width, int height,
								int time_base_num, int time_base_den,
								int format, int pixel_aspect_num,
								int pixel_aspect_den, int interlacing,
								int color_range, int divider)
{
	if (!p) {
		return OAKENGINE_E_INVALID;
	}
	p->width = width;
	p->height = height;
	p->time_base_num = time_base_num;
	p->time_base_den = time_base_den;
	p->format = format;
	p->pixel_aspect_num = pixel_aspect_num;
	p->pixel_aspect_den = pixel_aspect_den;
	p->interlacing = interlacing;
	p->color_range = color_range;
	p->divider = divider;
	return OAKENGINE_OK;
}

void *oakengine_video_params_create(const oak_video_params *pod)
{
	if (!pod) {
		return nullptr;
	}

	olive::VideoParams *p;
	if (pod->width > 0 && pod->height > 0 && pod->time_base_num != 0 &&
		pod->time_base_den != 0) {
		p = new olive::VideoParams(
			pod->width, pod->height,
			olive::Rational(pod->time_base_num, pod->time_base_den),
			olive::PixelFormat::Format(pod->format),
			olive::VideoParams::k_internal_channel_count,
			olive::Rational(pod->pixel_aspect_num, pod->pixel_aspect_den),
			static_cast<olive::VideoParams::Interlacing>(pod->interlacing),
			pod->divider > 0 ? pod->divider : 1);
	} else if (pod->width > 0 && pod->height > 0) {
		p = new olive::VideoParams(
			pod->width, pod->height,
			olive::PixelFormat::Format(pod->format),
			olive::VideoParams::k_internal_channel_count,
			olive::Rational(pod->pixel_aspect_num, pod->pixel_aspect_den),
			static_cast<olive::VideoParams::Interlacing>(pod->interlacing),
			pod->divider > 0 ? pod->divider : 1);
	} else {
		p = new olive::VideoParams();
	}

	p->set_color_range(
		static_cast<olive::VideoParams::ColorRange>(pod->color_range));
	p->set_video_type(static_cast<olive::VideoParams::Type>(pod->video_type));
	p->set_premultiplied_alpha(pod->premultiplied_alpha != 0);
	return p;
}

void oakengine_video_params_free(void *params)
{
	delete static_cast<olive::VideoParams *>(params);
}

int oakengine_video_params_equal(const oak_video_params *a,
								 const oak_video_params *b)
{
	if (!a || !b) {
		return 0;
	}
	return (a->width == b->width && a->height == b->height &&
			a->time_base_num == b->time_base_num &&
			a->time_base_den == b->time_base_den && a->format == b->format &&
			a->pixel_aspect_num == b->pixel_aspect_num &&
			a->pixel_aspect_den == b->pixel_aspect_den &&
			a->interlacing == b->interlacing && a->divider == b->divider) ?
			   1 :
			   0;
}

int oakengine_video_params_is_valid(const oak_video_params *p)
{
	if (!p) {
		return 0;
	}
	const olive::VideoParams vp(
		p->width, p->height,
		olive::Rational(p->time_base_num, p->time_base_den),
		olive::PixelFormat::Format(p->format),
		olive::VideoParams::k_internal_channel_count,
		olive::Rational(p->pixel_aspect_num, p->pixel_aspect_den),
		olive::VideoParams::Interlacing(p->interlacing), p->divider);
	return vp.is_valid() ? 1 : 0;
}

int oakengine_video_params_bytes_per_pixel(int format, int channels)
{
	return olive::VideoParams::get_bytes_per_pixel(
		olive::PixelFormat::Format(format), channels);
}

int oakengine_video_params_internal_channel_count(void)
{
	return olive::VideoParams::k_internal_channel_count;
}

int oakengine_export_render_with_params(OakEngineSequence *seq,
										const OakEngineEncodingParams *params)
{
	oakengine_export_set_error_string(QString());
	if (!seq || !params) {
		oakengine_export_set_error_string(
			QStringLiteral("invalid arguments"));
		return OAKENGINE_E_INVALID;
	}
	// Validate the sequence handle by pointer membership in the active
	// project's node list. A dynamic_cast on a bogus handle (e.g. an
	// OakEngineEncodingParams pointer, which has no vtable) crashes, and
	// there is no safe way to dynamic_cast an arbitrary address -- pointer
	// comparison is the only safe check. Limitation: the sequence must
	// belong to the active project (same scope the export dialog uses).
	olive::Sequence *sequence = nullptr;
	if (olive::EngineCore::instance() &&
		olive::EngineCore::instance()->open_project()) {
		for (olive::Node *n :
			 olive::EngineCore::instance()->open_project()->nodes()) {
			if (reinterpret_cast<OakEngineSequence *>(n) == seq) {
				sequence = dynamic_cast<olive::Sequence *>(n);
				break;
			}
		}
	}
	if (!sequence) {
		oakengine_export_set_error_string(
			QStringLiteral("handle is not a sequence of the active project"));
		return OAKENGINE_E_INVALID;
	}
	if (!olive::RenderManager::instance()) {
		oakengine_export_set_error_string(
			QStringLiteral("engine not initialized with "
						   "OAKENGINE_INIT_RENDER"));
		return OAKENGINE_E_STATE;
	}
	olive::Project *project = sequence->project();
	if (!project) {
		oakengine_export_set_error_string(
			QStringLiteral("sequence is not attached to a project"));
		return OAKENGINE_E_INVALID;
	}

	// The handle publicly inherits olive::EncodingParams, so it drives the
	// same synchronous ExportTask machinery as oakengine_export_render()/_ex()
	// directly (progress callback + cancellation are shared engine state).
	auto *ep = const_cast<OakEngineEncodingParams *>(params);
	const int rc = oakengine_export_render_internal(
		sequence, project, *ep, ep->audio_enabled(),
		ep->audio_enabled() ? ep->audio_params()
							: sequence->get_audio_params());
	return rc;
}

} // extern "C"
