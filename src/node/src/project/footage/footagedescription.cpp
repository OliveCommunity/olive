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

#include "footagedescription.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "xmlutils.h"
#include "olive/core/util/stringutils.h"
#include "project/serializer/typeserializer.h"

namespace olive
{

bool FootageDescription::load(const std::string &filename)
{
	// Reset self
	*this = FootageDescription();

	std::ifstream file(filename, std::ios::binary);

	if (file.is_open()) {
		std::stringstream ss;
		ss << file.rdbuf();
		XmlStreamReader reader(ss.str());

		bool found_streamcache = false;

		while (xml_read_next_start_element(&reader)) {
			if (reader.name() == "streamcache") {
				found_streamcache = true;
				// Default to first version of metadata (which wasn't versioned at all)
				unsigned version = 1;

				{
					for (const XmlStreamAttribute &attr : reader.attributes()) {
						if (attr.name == "version") {
							version =
								strtoul(attr.value.c_str(), nullptr, 10);
						}
					}
				}

				if (version != k_footage_meta_version) {
					// If this is a different version, discard so we can probe new data
					return false;
				}

				while (xml_read_next_start_element(&reader)) {
					if (reader.name() == "decoder") {
						decoder_ = reader.read_element_text();
					} else if (reader.name() == "sourcestarttime") {
						std::string source;
						{
							for (const XmlStreamAttribute &attr :
								 reader.attributes()) {
								if (attr.name == "source") {
									source = attr.value;
								}
							}
						}

						const std::vector<std::string> split =
							core::StringUtils::split(reader.read_element_text(),
													 '/');
						if (split.size() == 2) {
							set_source_start_time(
								Rational(atoi(split.at(0).c_str()),
										 atoi(split.at(1).c_str())),
								source);
						}
					} else if (reader.name() == "streams") {
						{
							for (const XmlStreamAttribute &attr :
								 reader.attributes()) {
								if (attr.name == "count") {
									total_stream_count_ =
										atoi(attr.value.c_str());
								}
							}
						}

						while (xml_read_next_start_element(&reader)) {
							if (reader.name() == "video") {
								VideoParams vp;
								vp.load(&reader);
								add_video_stream(vp);
							} else if (reader.name() == "audio") {
								AudioParams ap =
									TypeSerializer::load_audio_params(&reader);
								add_audio_stream(ap);
							} else if (reader.name() == "subtitle") {
								SubtitleParams sp;
								sp.load(&reader);
								add_subtitle_stream(sp);
							} else {
								reader.skip_current_element();
							}
						}
					} else {
						reader.skip_current_element();
					}
				}
			} else {
				reader.skip_current_element();
			}
		}

		file.close();

		if (reader.has_error()) {
			fprintf(stderr, "Failed to load footage description for %s: %s\n",
					filename.c_str(), reader.error_string().c_str());
		} else {
			// Only accept files whose root element is the one Save() writes
			return found_streamcache;
		}
	}

	return false;
}

bool FootageDescription::save(const std::string &filename) const
{
	XmlStreamWriter writer;

	writer.write_start_element("streamcache");

	writer.write_attribute("version", std::to_string(k_footage_meta_version));

	writer.write_text_element("decoder", decoder_);

	if (has_source_start_time_) {
		writer.write_start_element("sourcestarttime");
		writer.write_attribute("source", source_start_time_source_);
		writer.write_characters(std::to_string(source_start_time_.numerator()) +
								"/" +
								std::to_string(source_start_time_.denominator()));
		writer.write_end_element();
	}

	writer.write_start_element("streams");

	writer.write_attribute("count", std::to_string(total_stream_count_));

	for (const VideoParams &vp : video_streams_) {
		writer.write_start_element("video");
		vp.save(&writer);
		writer.write_end_element(); // video
	}

	for (const AudioParams &ap : audio_streams_) {
		writer.write_start_element("audio");
		TypeSerializer::save_audio_params(&writer, ap);
		writer.write_end_element(); // audio
	}

	for (const SubtitleParams &sp : subtitle_streams_) {
		writer.write_start_element("subtitle");
		sp.save(&writer);
		writer.write_end_element(); // audio
	}

	writer.write_end_element(); // streams

	writer.write_end_element(); // streamcache

	writer.write_end_document();

	// NOTE(de-Qt): the former writeStartDocument() XML declaration is no
	// longer emitted; the file is still valid XML and readable by both the
	// old Qt reader and the new one.
	std::ofstream file(filename, std::ios::binary | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}

	file << writer.output();
	file.close();

	return true;
}

}
