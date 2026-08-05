/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Team
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

#include "typeserializer.h"

#include <cstdlib>

namespace olive
{

AudioParams TypeSerializer::load_audio_params(XmlStreamReader *reader)
{
	AudioParams a;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "samplerate") {
			a.set_sample_rate(atoi(reader->read_element_text().c_str()));
		} else if (reader->name() == "channellayout") {
			a.set_channel_layout(
				strtoull(reader->read_element_text().c_str(), nullptr, 10));
		} else if (reader->name() == "format") {
			a.set_format(
				SampleFormat::from_string(reader->read_element_text()));
		} else if (reader->name() == "enabled") {
			a.set_enabled(atoi(reader->read_element_text().c_str()));
		} else if (reader->name() == "streamindex") {
			a.set_stream_index(atoi(reader->read_element_text().c_str()));
		} else if (reader->name() == "duration") {
			a.set_duration(
				strtoll(reader->read_element_text().c_str(), nullptr, 10));
		} else if (reader->name() == "timebase") {
			a.set_time_base(
				Rational::from_string(reader->read_element_text()));
		} else {
			reader->skip_current_element();
		}
	}

	return a;
}

void TypeSerializer::save_audio_params(XmlStreamWriter *writer,
									 const AudioParams &a)
{
	writer->write_text_element("samplerate", std::to_string(a.sample_rate()));
	writer->write_text_element("channellayout",
							  std::to_string(a.channel_layout()));
	writer->write_text_element("format", a.format().to_string());
	writer->write_text_element("enabled", std::to_string(a.enabled()));
	writer->write_text_element("streamindex", std::to_string(a.stream_index()));
	writer->write_text_element("duration", std::to_string(a.duration()));
	writer->write_text_element("timebase", a.time_base().to_string());
}

}
