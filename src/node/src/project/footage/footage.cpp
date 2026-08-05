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

#include "footage.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "codec/decoder.h"
#include "filefunctions.h"
#include "qtutils.h"
#include "xmlutils.h"
#include "config/config.h"
#include "olive/core/util/stringutils.h"
#include "color/colormanager/colormanager.h"
#include "project.h"
#include "render/job/footagejob.h"

namespace olive
{

const std::string Footage::k_filename_input = "file_in";

#define super ViewerOutput

// QDateTime::toMSecsSinceEpoch() equivalent for std::filesystem
static int64_t file_modification_time_ms(const std::string &path)
{
	std::error_code ec;
	auto t = std::filesystem::last_write_time(path, ec);
	if (ec) {
		return 0;
	}

	// Portable C++17 file-clock -> system-clock conversion
	auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
		t - decltype(t)::clock::now() + std::chrono::system_clock::now());
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   sys.time_since_epoch())
		.count();
}

// Replaces QStandardPaths::CacheLocation for the footage metadata cache
static std::string get_footage_meta_cache_dir()
{
#ifdef __APPLE__
	const char *home = getenv("HOME");
	if (home != nullptr && home[0] != '\0') {
		return std::string(home) + "/Library/Caches/oak";
	}
#endif
	return FileFunctions::get_configuration_location() + "/cache";
}

Footage::Footage(const std::string &filename)
	: ViewerOutput(false, false)
	, timestamp_(0)
	, has_source_start_time_(false)
	, proxy_enabled_(false)
	, proxy_state_(ProxyManager::k_proxy_missing)
	, proxy_video_stream_index_(-1)
	, proxy_preset_version_(0)
	, has_custom_proxy_params_(false)
	, valid_(false)
	, cancelled_(nullptr)
	, total_stream_count_(0)
{
	set_flag(k_is_item);

	prepend_input(k_filename_input, NodeValue::k_file,
				 InputFlags(k_input_flag_not_connectable |
							k_input_flag_not_keyframable));

	clear();

	if (!filename.empty()) {
		set_filename(filename);
	}

	// NOTE(de-Qt): the 5-second QTimer calling check_footage() and the
	// AudioWaveformCache::validated -> connected_waveform_changed connection
	// were removed with QObject; the facade layer must schedule
	// check_footage() and forward cache notifications instead.
}

void Footage::retranslate()
{
	super::retranslate();

	set_input_name(k_filename_input, "Filename");
}

void Footage::InputValueChangedEvent(const std::string &input, int element)
{
	if (input == k_filename_input) {
		// Reset internal stream cache
		clear();

		reprobe();
	} else {
		super::InputValueChangedEvent(input, element);
	}
}

Rational Footage::verify_length_internal(Track::Type type) const
{
	if (type == Track::k_video) {
		VideoParams first_stream = get_first_enabled_video_stream();

		if (first_stream.is_valid()) {
			return core::Timecode::timestamp_to_time(first_stream.duration(),
													 first_stream.time_base());
		}
	} else if (type == Track::k_audio) {
		AudioParams first_stream = get_first_enabled_audio_stream();

		if (first_stream.is_valid()) {
			return core::Timecode::timestamp_to_time(first_stream.duration(),
													 first_stream.time_base());
		}
	} else if (type == Track::k_subtitle) {
		SubtitleParams first_stream = get_first_enabled_subtitle_stream();

		if (first_stream.is_valid()) {
			return first_stream.duration();
		}
	}

	return 0;
}

std::string Footage::get_colorspace_to_use(const VideoParams &params) const
{
	if (!params.colorspace().empty()) {
		// The user explicitly set this stream's colorspace
		return params.colorspace();
	}

	// No override: try auto-detecting from the media's color tags
	const std::string detected =
		project()->color_manager()->get_colorspace_for_ffmpeg_tags(
			params.color_primaries(), params.color_transfer());
	if (!detected.empty()) {
		return detected;
	}

	return project()->color_manager()->get_default_input_color_space();
}

void Footage::clear()
{
	// Clear all dynamically created inputs
	input_array_resize(k_video_params_input, 0);
	input_array_resize(k_audio_params_input, 0);
	input_array_resize(k_subtitle_params_input, 0);

	// Clear decoder link
	decoder_.clear();

	has_source_start_time_ = false;
	source_start_time_ = Rational();
	source_start_time_source_.clear();
	clear_proxy();

	// Clear total stream count
	total_stream_count_ = 0;

	// Reset ready state
	valid_ = false;
}

void Footage::set_valid()
{
	valid_ = true;
}

std::string Footage::filename() const
{
	return get_standard_value(k_filename_input).to_string();
}

void Footage::set_filename(const std::string &s)
{
	set_standard_value(k_filename_input, s);
}

const int64_t &Footage::timestamp() const
{
	return timestamp_;
}

void Footage::set_timestamp(const int64_t &t)
{
	timestamp_ = t;
}

int Footage::get_stream_index(Track::Type type, int index) const
{
	switch (type) {
	case Track::k_video:
		if (index >= 0 && index < get_video_stream_count()) {
			return get_video_params(index).stream_index();
		}
		break;
	case Track::k_audio:
		if (index >= 0 && index < get_audio_stream_count()) {
			return get_audio_params(index).stream_index();
		}
		break;
	case Track::k_subtitle:
		if (index >= 0 && index < get_subtitle_stream_count()) {
			return get_subtitle_params(index).stream_index();
		}
		break;
	case Track::k_none:
	case Track::k_count:
		break;
	}

	return -1;
}

Track::Reference Footage::get_reference_from_real_index(int real_index) const
{
	// Check video streams
	for (int i = 0; i < get_video_stream_count(); i++) {
		if (get_video_params(i).stream_index() == real_index) {
			return Track::Reference(Track::k_video, i);
		}
	}

	for (int i = 0; i < get_audio_stream_count(); i++) {
		if (get_audio_params(i).stream_index() == real_index) {
			return Track::Reference(Track::k_audio, i);
		}
	}

	for (int i = 0; i < get_subtitle_stream_count(); i++) {
		if (get_subtitle_params(i).stream_index() == real_index) {
			return Track::Reference(Track::k_subtitle, i);
		}
	}

	return Track::Reference();
}

const std::string &Footage::decoder() const
{
	return decoder_;
}

void Footage::set_source_start_time(const Rational &time,
									const std::string &source)
{
	source_start_time_ = time;
	source_start_time_source_ = source;
	has_source_start_time_ = true;
}

void Footage::clear_source_start_time()
{
	source_start_time_ = Rational();
	source_start_time_source_.clear();
	has_source_start_time_ = false;
}

void Footage::set_proxy_enabled(bool enabled)
{
	if (proxy_enabled_ != enabled) {
		fprintf(stderr, "Footage::set_proxy_enabled: %s %d\n",
				filename().c_str(), enabled);
		proxy_enabled_ = enabled;
		if (Project *p = project()) {
			p->set_modified(true);
		}
		// (The proxy_settings_changed() signal was removed with QObject.)
	}
}

void Footage::set_proxy(const std::string &path, ProxyManager::ProxyState state,
					   int video_stream_index, int preset_version, bool enabled)
{
	fprintf(stderr, "Footage::SetProxy: %s enabled=%d state=%s path=%s\n",
			filename().c_str(), enabled,
			ProxyManager::proxy_state_to_string(state).c_str(), path.c_str());
	proxy_path_ = path;
	proxy_state_ = state;
	proxy_video_stream_index_ = video_stream_index;
	proxy_preset_version_ = preset_version;
	proxy_enabled_ = enabled;
	if (Project *p = project()) {
		p->set_modified(true);
	}
	// (The proxy_settings_changed() signal was removed with QObject.)
}

void Footage::clear_proxy()
{
	proxy_enabled_ = false;
	proxy_path_.clear();
	proxy_state_ = ProxyManager::k_proxy_missing;
	proxy_video_stream_index_ = -1;
	proxy_preset_version_ = 0;
	// (The proxy_settings_changed() signal was removed with QObject.)
}

void Footage::set_custom_proxy_params(const ProxyManager::ProxyParams &params)
{
	custom_proxy_params_ = params;
	has_custom_proxy_params_ = true;
	if (Project *p = project()) {
		p->set_modified(true);
	}
	// (The proxy_settings_changed() signal was removed with QObject.)
}

void Footage::clear_custom_proxy_params()
{
	if (has_custom_proxy_params_) {
		has_custom_proxy_params_ = false;
		custom_proxy_params_ = ProxyManager::ProxyParams();
		if (Project *p = project()) {
			p->set_modified(true);
		}
		// (The proxy_settings_changed() signal was removed with QObject.)
	}
}

ProxyManager::ProxyParams Footage::get_effective_proxy_params() const
{
	if (has_custom_proxy_params_) {
		return custom_proxy_params_;
	}

	return ProxyManager::proxy_params_from_config();
}

std::string Footage::describe_video_stream(const VideoParams &params)
{
	if (params.video_type() == VideoParams::k_video_type_still) {
		return std::to_string(params.stream_index()) + ": Image - " +
			   std::to_string(params.width()) + "x" +
			   std::to_string(params.height());
	} else {
		return std::to_string(params.stream_index()) + ": Video - " +
			   std::to_string(params.width()) + "x" +
			   std::to_string(params.height());
	}
}

std::string Footage::describe_audio_stream(const AudioParams &params)
{
	return std::to_string(params.stream_index()) + ": Audio - " +
		   std::to_string(params.channel_count()) + " Channel(s), " +
		   std::to_string(params.sample_rate()) + "Hz";
}

std::string Footage::describe_subtitle_stream(const SubtitleParams &params)
{
	return std::to_string(params.stream_index()) + ": Subtitle";
}

void Footage::value(const NodeValueRow &value, const NodeGlobals &globals,
					NodeValueTable *table) const
{
	(void) globals;

	// Pop filename from table
	std::string file = value.at(k_filename_input).to_string();

	// Proxies can be globally disabled (Tools > Use Proxy Media) without
	// losing each footage's individual proxy setting
	// ADAPT(config 波次): engine config/config.h is still Qt-based; kept as-is
	const bool proxies_allowed =
		Config::current()[QStringLiteral("UseProxyMedia")].toBool();

	// If the file exists and the reference is valid, push a footage job to the renderer
	if (std::filesystem::exists(file)) {
		// Push length
		table->push(NodeValue::k_rational, Variant::from_value(get_length()),
					this, "length");

		// Push each stream as a footage job
		for (int si = 0; si < get_total_stream_count(); si++) {
			Track::Reference ref = get_reference_from_real_index(si);
			FootageJob job(globals.time(), decoder_, filename(), ref.type(),
						   get_length(), globals.loop_mode());

			if (ref.type() == Track::k_video) {
				VideoParams vp = get_video_params(ref.index());

				if (proxies_allowed && proxy_enabled_ && !proxy_path_.empty() &&
					proxy_video_stream_index_ == vp.stream_index() &&
					ProxyManager::get_proxy_state(proxy_path_) ==
						ProxyManager::k_proxy_ready) {
					job.set_proxy(proxy_path_, "ffmpeg", 0);
				}

				// Ensure the colorspace is valid and not empty
				vp.set_colorspace(get_colorspace_to_use(vp));

				// Adjust footage job's divider
				if (globals.vparams().divider() > 1) {
					// Use a divider appropriate for this target resolution
					int calculated = VideoParams::get_divider_for_target_resolution(
						vp.width(), vp.height(),
						globals.vparams().effective_width(),
						globals.vparams().effective_height());
					vp.set_divider(
						std::min(calculated, globals.vparams().divider()));
				} else {
					// Render everything at full res
					vp.set_divider(1);
				}

				job.set_video_params(vp);

				table->push(NodeValue::k_texture, Texture::job(vp, job), this,
							ref.to_string());
			} else if (ref.type() == Track::k_audio) {
				AudioParams ap = get_audio_params(ref.index());
				job.set_audio_params(ap);
				job.set_cache_path(project()->cache_path());

				// Proxies generated with audio contain the video stream at
				// index 0 followed by all source audio streams in source order
				if (proxies_allowed && proxy_enabled_ && !proxy_path_.empty() &&
					ProxyManager::get_proxy_state(proxy_path_) ==
						ProxyManager::k_proxy_ready &&
					ProxyManager::proxy_filename_has_audio(proxy_path_)) {
					int audio_rank = 0;
					for (int sj = 0; sj < get_total_stream_count(); sj++) {
						const Track::Reference other =
							get_reference_from_real_index(sj);

						if (other.type() == Track::k_audio &&
							get_audio_params(other.index()).stream_index() <
								ap.stream_index()) {
							audio_rank++;
						}
					}
					job.set_proxy(proxy_path_, "ffmpeg", audio_rank + 1);
				}

				table->push(NodeValue::k_samples, Variant::from_value(job), this,
							ref.to_string());
			}
		}
	} else if (!file.empty()) {
		// Media is offline: push a generated warning frame for each video
		// stream so missing media is clearly visible in the timeline instead
		// of a transparent/black hole. generate_frame() draws the slat.
		for (int si = 0; si < get_total_stream_count(); si++) {
			Track::Reference ref = get_reference_from_real_index(si);
			if (ref.type() != Track::k_video) {
				continue;
			}

			VideoParams vp = get_video_params(ref.index());
			if (!vp.is_valid()) {
				vp = globals.vparams();
			}
			vp.set_format(PixelFormat::u8);
			vp.set_colorspace(
				project()->color_manager()->get_default_input_color_space());

			GenerateJob job(value);
			table->push(NodeValue::k_texture, Texture::job(vp, job), this,
						ref.to_string());
		}
	}
}

void Footage::generate_frame(FramePtr frame, const GenerateJob &job) const
{
	(void) job;

	// Dark red slat with diagonal stripes, matching the offline media
	// warnings of other NLEs. NOTE(de-Qt): this used to be rasterized with
	// QImage/QPainter including an antialiased "Media Offline" text overlay;
	// the text overlay is gone (no font engine in oaknode) and the stripes
	// are no longer antialiased.
	uint8_t *data = reinterpret_cast<uint8_t *>(frame->data());
	const int w = frame->width();
	const int h = frame->height();
	const int linesize = frame->linesize_bytes();

	const uint8_t bg_r = 60, bg_g = 0, bg_b = 0;
	const uint8_t stripe_r = 120, stripe_g = 20, stripe_b = 20;
	const int pen = std::max(2, h / 90);
	const int stripe_step = std::max(16, h / 6);

	for (int y = 0; y < h; y++) {
		uint8_t *row = data + y * linesize;
		for (int x = 0; x < w; x++) {
			// Diagonal stripes: same lines as the old QPainter loop
			// (from (sx, 0) to (sx + h, h), i.e. x - y == const)
			bool on_stripe = false;
			int d = x - y; // ranges from -(h-1) to (w-1)
			// Stripes start at x = -h and step by stripe_step
			int rel = d + h;
			if (rel >= 0) {
				int m = rel % stripe_step;
				if (m < pen) {
					on_stripe = true;
				}
			}

			uint8_t *px = row + x * 4;
			if (on_stripe) {
				px[0] = stripe_r;
				px[1] = stripe_g;
				px[2] = stripe_b;
			} else {
				px[0] = bg_r;
				px[1] = bg_g;
				px[2] = bg_b;
			}
			px[3] = 255;
		}
	}
}

std::string Footage::get_stream_type_name(Track::Type type)
{
	switch (type) {
	case Track::k_video:
		return "Video";
	case Track::k_audio:
		return "Audio";
	case Track::k_subtitle:
		return "Subtitle";
	case Track::k_none:
	case Track::k_count:
		break;
	}

	return "Unknown";
}

Node *Footage::get_connected_texture_output()
{
	if (get_video_stream_count() > 0) {
		return this;
	} else {
		return nullptr;
	}
}

Node *Footage::get_connected_sample_output()
{
	if (get_audio_stream_count() > 0) {
		return this;
	} else {
		return nullptr;
	}
}

bool time_is_out_of_bounds(const Rational &time, const Rational &length)
{
	return time < 0 || time >= length;
}

Rational Footage::adjust_time_by_loop_mode(Rational time, LoopMode loop_mode,
									   const Rational &length,
									   VideoParams::Type type,
									   const Rational &timebase)
{
	if (type == VideoParams::k_video_type_still) {
		// No looping for still images
		return 0;
	}

	if (time_is_out_of_bounds(time, length)) {
		switch (loop_mode) {
		case LoopMode::k_loop_mode_off:
			// Return no time to indicate no frame should be shown here
			time = Rational::na_n;
			break;
		case LoopMode::k_loop_mode_clamp:
			if (length < timebase) {
				// No full frame fits in the range, so there is nothing to clamp to
				time = Rational::na_n;
			} else {
				// Clamp footage time to length
				time = std::clamp(time, Rational(0), length - timebase);
			}
			break;
		case LoopMode::k_loop_mode_loop:
			if (length <= 0) {
				// Cannot loop around an empty range
				time = Rational::na_n;
			} else {
				// Loop footage time around job length
				do {
					if (time >= length) {
						time -= length;
					} else {
						time += length;
					}
				} while (time_is_out_of_bounds(time, length));
			}
			break;
		}
	}

	return time;
}

Variant Footage::data(const DataType &d) const
{
	switch (d) {
	case created_time: {
		std::error_code ec;
		if (std::filesystem::exists(filename(), ec)) {
			auto tp = QtUtils::get_creation_date(
				std::filesystem::path(filename()));
			return Variant(int64_t(
				std::chrono::system_clock::to_time_t(tp)));
		}
		break;
	}
	case modified_time: {
		std::error_code ec;
		if (std::filesystem::exists(filename(), ec)) {
			return Variant(int64_t(file_modification_time_ms(filename()) /
								   1000));
		}
		break;
	}
	case icon: {
		if (valid_ && get_total_stream_count()) {
			// Prioritize video > audio > image
			VideoParams s = get_first_enabled_video_stream();

			if (s.is_valid() &&
				s.video_type() != VideoParams::k_video_type_still) {
				return "video";
			} else if (has_enabled_audio_streams()) {
				return "audio";
			} else if (s.is_valid() &&
					   s.video_type() == VideoParams::k_video_type_still) {
				return "image";
			} else if (has_enabled_subtitle_streams()) {
				return "subtitles";
			}
		}

		return "error";
	}
	case tooltip: {
		if (valid_) {
			std::string tip = "Filename: " + filename();

			int vp_sz = get_video_stream_count();
			for (int i = 0; i < vp_sz; i++) {
				VideoParams p = get_video_params(i);

				if (p.enabled()) {
					tip.append("\n");
					tip.append(describe_video_stream(p));
				}
			}

			int ap_sz = get_audio_stream_count();
			for (int i = 0; i < ap_sz; i++) {
				AudioParams p = get_audio_params(i);

				if (p.enabled()) {
					tip.append("\n");
					tip.append(describe_audio_stream(p));
				}
			}

			int sp_sz = get_subtitle_stream_count();
			for (int i = 0; i < sp_sz; i++) {
				SubtitleParams p = get_subtitle_params(i);

				if (p.enabled()) {
					tip.append("\n");
					tip.append(describe_subtitle_stream(p));
				}
			}

			return tip;
		} else {
			return "Invalid";
		}
	}
	default:
		break;
	}

	return super::data(d);
}

bool Footage::load_custom(XmlStreamReader *reader, SerializedData *data)
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "timestamp") {
			this->set_timestamp(strtoll(reader->read_element_text().c_str(),
										nullptr, 10));
		} else if (reader->name() == "proxy") {
			bool enabled = false;
			ProxyManager::ProxyState state = ProxyManager::k_proxy_missing;
			int stream = -1;
			int preset_version = 0;
			bool has_custom_params = false;
			ProxyManager::ProxyParams custom_params;
			{
				for (const XmlStreamAttribute &attr : reader->attributes()) {
					if (attr.name == "enabled") {
						enabled =
							(attr.value == "1" || attr.value == "true");
					} else if (attr.name == "state") {
						state = ProxyManager::proxy_state_from_string(
							attr.value);
					} else if (attr.name == "stream") {
						stream = atoi(attr.value.c_str());
					} else if (attr.name == "preset") {
						preset_version = atoi(attr.value.c_str());
					} else if (attr.name == "custom") {
						has_custom_params =
							(attr.value == "1" || attr.value == "true");
					} else if (attr.name == "pwidth") {
						custom_params.width = atoi(attr.value.c_str());
					} else if (attr.name == "pheight") {
						custom_params.height = atoi(attr.value.c_str());
					} else if (attr.name == "pdivider") {
						custom_params.divider = atoi(attr.value.c_str());
					} else if (attr.name == "pcrf") {
						custom_params.crf = atoi(attr.value.c_str());
					} else if (attr.name == "ppreset") {
						custom_params.preset = attr.value;
					} else if (attr.name == "pext") {
						custom_params.extension = attr.value;
					} else if (attr.name == "paudio") {
						custom_params.include_audio =
							(attr.value == "1" || attr.value == "true");
					}
				}
			}

			if (has_custom_params) {
				set_custom_proxy_params(custom_params);
			}

			const std::string path = reader->read_element_text();
			if (!path.empty()) {
				set_proxy(path, state, stream, preset_version, enabled);
			} else if (enabled) {
				set_proxy_enabled(true);
			}
		} else if (reader->name() == "sourcestarttime") {
			std::string source;
			{
				for (const XmlStreamAttribute &attr : reader->attributes()) {
					if (attr.name == "source") {
						source = attr.value;
					}
				}
			}

			const std::vector<std::string> split =
				core::StringUtils::split(reader->read_element_text(), '/');
			if (split.size() == 2) {
				// Same ok-semantics as QString::toInt(&ok)
				char *end = nullptr;
				const int numerator =
					strtol(split.at(0).c_str(), &end, 10);
				bool numerator_ok =
					end != split.at(0).c_str() && *end == '\0';
				const int denominator =
					strtol(split.at(1).c_str(), &end, 10);
				bool denominator_ok =
					end != split.at(1).c_str() && *end == '\0';
				if (numerator_ok && denominator_ok && denominator) {
					set_source_start_time(Rational(numerator, denominator),
									  source);
				}
			}
		} else if (reader->name() == "viewer") {
			if (!ViewerOutput::load_custom(reader, data)) {
				return false;
			}
		} else {
			reader->skip_current_element();
		}
	}

	// The cached lengths are not serialized. Recompute them from the stream
	// parameters that were just loaded so that worker processes and any code
	// that reads GetLength() before InvalidateCache() runs sees valid values.
	verify_length();

	return true;
}

void Footage::save_custom(XmlStreamWriter *writer) const
{
	writer->write_text_element("timestamp", std::to_string(this->timestamp()));

	if (!proxy_path_.empty() || proxy_enabled_) {
		writer->write_start_element("proxy");
		writer->write_attribute("enabled", proxy_enabled_ ? "1" : "0");
		writer->write_attribute(
			"state", ProxyManager::proxy_state_to_string(proxy_state_));
		writer->write_attribute("stream",
							   std::to_string(proxy_video_stream_index_));
		writer->write_attribute("preset",
							   std::to_string(proxy_preset_version_));
		if (has_custom_proxy_params_) {
			writer->write_attribute("custom", "1");
			writer->write_attribute("pwidth",
								   std::to_string(custom_proxy_params_.width));
			writer->write_attribute(
				"pheight", std::to_string(custom_proxy_params_.height));
			writer->write_attribute(
				"pdivider", std::to_string(custom_proxy_params_.divider));
			writer->write_attribute("pcrf",
								   std::to_string(custom_proxy_params_.crf));
			writer->write_attribute("ppreset", custom_proxy_params_.preset);
			writer->write_attribute("pext", custom_proxy_params_.extension);
			writer->write_attribute(
				"paudio", custom_proxy_params_.include_audio ? "1" : "0");
		}
		writer->write_characters(proxy_path_);
		writer->write_end_element();
	}

	if (has_source_start_time_) {
		writer->write_start_element("sourcestarttime");
		writer->write_attribute("source", source_start_time_source_);
		writer->write_characters(
			std::to_string(source_start_time_.numerator()) + "/" +
			std::to_string(source_start_time_.denominator()));
		writer->write_end_element();
	}

	writer->write_start_element("viewer");

	ViewerOutput::save_custom(writer);

	writer->write_end_element(); // viewer
}

void Footage::reprobe()
{
	// Determine if file still exists
	std::string filename = this->filename();

	// In case of failure to load file, set timestamp to a value that will always be invalid so we
	// continuously reprobe
	set_timestamp(0);

	if (!filename.empty()) {
		std::error_code ec;
		if (std::filesystem::exists(filename, ec)) {
			// Grab timestamp
			set_timestamp(file_modification_time_ms(filename));

			// Determine if we've already cached the metadata of this file
			std::string cache_dir = get_footage_meta_cache_dir();
			std::filesystem::create_directories(cache_dir, ec);
			std::string meta_cache_file =
				(std::filesystem::path(cache_dir) /
				 FileFunctions::get_unique_file_identifier(filename))
					.string();

			FootageDescription footage_info;

			// Try to load footage info from cache
			if (!std::filesystem::exists(meta_cache_file, ec) ||
				!footage_info.load(meta_cache_file)) {
				// Probe and create cache
				std::vector<DecoderPtr> decoder_list =
					Decoder::receive_list_of_all_decoders();

				for (DecoderPtr decoder : decoder_list) {
					footage_info = decoder->probe(filename, cancelled_);

					if (footage_info.is_valid()) {
						break;
					}
				}

				if (!cancelled_ || !cancelled_->heard_cancel()) {
					// Only cache successful probes; caching a failed probe
					// would make every future load re-use the invalid metadata
					if (footage_info.is_valid() &&
						!footage_info.save(meta_cache_file)) {
						fprintf(stderr,
								"Failed to save stream cache, footage will "
								"have to be re-probed\n");
					}
				}
			}

			if (footage_info.is_valid()) {
				decoder_ = footage_info.decoder();

				input_array_resize(k_video_params_input,
								 int(footage_info.get_video_streams().size()));
				for (int i = 0; i < int(footage_info.get_video_streams().size());
					 i++) {
					VideoParams video_stream =
						footage_info.get_video_streams().at(i);

					if (i < input_array_size(k_video_params_input)) {
						VideoParams existing = this->get_video_params(i);
						if (existing.is_valid()) {
							video_stream =
								merge_video_stream(video_stream, existing);
						}
					}

					set_stream(Track::k_video, Variant::from_value(video_stream),
							  i);
				}

				input_array_resize(k_audio_params_input,
								 int(footage_info.get_audio_streams().size()));
				for (int i = 0; i < int(footage_info.get_audio_streams().size());
					 i++) {
					set_stream(Track::k_audio,
							  Variant::from_value(
								  footage_info.get_audio_streams().at(i)),
							  i);
				}

				input_array_resize(
					k_subtitle_params_input,
					int(footage_info.get_subtitle_streams().size()));
				for (int i = 0;
					 i < int(footage_info.get_subtitle_streams().size()); i++) {
					set_stream(Track::k_subtitle,
							  Variant::from_value(
								  footage_info.get_subtitle_streams().at(i)),
							  i);
				}

				total_stream_count_ = footage_info.get_stream_count();
				if (footage_info.has_source_start_time()) {
					set_source_start_time(footage_info.source_start_time(),
									   footage_info.source_start_time_source());
				}

				set_valid();
			}
		}
	}
}

VideoParams Footage::merge_video_stream(const VideoParams &base,
									  const VideoParams &over)
{
	VideoParams merged = base;

	merged.set_pixel_aspect_ratio(over.pixel_aspect_ratio());
	merged.set_interlacing(over.interlacing());
	merged.set_colorspace(over.colorspace());
	merged.set_premultiplied_alpha(over.premultiplied_alpha());
	merged.set_video_type(over.video_type());
	merged.set_color_range(over.color_range());
	if (merged.video_type() == VideoParams::k_video_type_image_sequence) {
		merged.set_start_time(over.start_time());
		merged.set_duration(over.duration());
		merged.set_frame_rate(over.frame_rate());
		merged.set_time_base(over.time_base());
	}

	return merged;
}

void Footage::check_footage()
{
	// NOTE(de-Qt): the qApp->activeWindow() gate moved to the app layer; the
	// caller (facade) decides when it is appropriate to check.
	std::string fn = filename();

	if (!fn.empty()) {
		int64_t current_file_timestamp = file_modification_time_ms(fn);

		if (current_file_timestamp != timestamp()) {
			// File has changed!
			clear();
			reprobe();
			invalidate_all(k_filename_input);
		}
	}
}

void Footage::default_color_space_changed()
{
	bool inv = false;
	int sz = get_video_stream_count();
	for (int i = 0; i < sz; i++) {
		// Check if any of our streams are using the default colorspace
		if (get_video_params(i).colorspace().empty()) {
			inv = true;
			break;
		}
	}

	if (inv) {
		invalidate_all(k_video_params_input);
	}
}

void Footage::proxy_ready(const std::string &source_filename, int stream_index,
						 const std::string &proxy_filename)
{
	proxy_finished(source_filename, stream_index, proxy_filename,
				  ProxyManager::k_proxy_ready);
}

void Footage::proxy_finished(const std::string &source_filename,
							int stream_index,
							const std::string &proxy_filename,
							ProxyManager::ProxyState state)
{
	if (filename() != source_filename ||
		proxy_video_stream_index_ != stream_index ||
		proxy_path_ != proxy_filename) {
		return;
	}

	proxy_state_ = state;
	invalidate_all(k_filename_input);
}

}
