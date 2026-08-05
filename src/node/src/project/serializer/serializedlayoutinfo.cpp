/*
 * Oak Video Editor - Non-Linear Video Editor
 * Copyright (C) 2025 Olive CE Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "serializedlayoutinfo.h"

#include <cstdlib>

namespace olive
{

void SerializedLayoutInfo::to_xml(XmlStreamWriter *writer) const
{
	writer->write_attribute("version", std::to_string(k_version));

	writer->write_start_element("folders");

	for (Folder *folder : open_folders) {
		writer->write_text_element(
			"folder", std::to_string(reinterpret_cast<uintptr_t>(folder)));
	}

	writer->write_end_element(); // folders

	writer->write_start_element("timeline");

	for (Sequence *sequence : open_sequences) {
		writer->write_text_element(
			"sequence",
			std::to_string(reinterpret_cast<uintptr_t>(sequence)));
	}

	writer->write_end_element(); // timeline

	writer->write_start_element("viewers");

	for (ViewerOutput *viewer : open_viewers) {
		writer->write_text_element(
			"viewer", std::to_string(reinterpret_cast<uintptr_t>(viewer)));
	}

	writer->write_end_element(); // viewers

	writer->write_start_element("data");

	for (auto it = panel_data.cbegin(); it != panel_data.cend(); it++) {
		writer->write_start_element("panel");

		writer->write_attribute("id", it->first);

		const PanelLayoutInfo &info = it->second;
		for (auto jt = info.cbegin(); jt != info.cend(); jt++) {
			writer->write_start_element("option");

			writer->write_attribute("name", jt->first);

			writer->write_characters(jt->second);

			writer->write_end_element(); // option
		}

		writer->write_end_element(); // panel
	}

	writer->write_end_element(); // data

	writer->write_text_element("state", byte_array_to_base64(state));
}

SerializedLayoutInfo
SerializedLayoutInfo::from_xml(XmlStreamReader *reader,
							  const std::map<uintptr_t, Node *> &node_ptrs)
{
	SerializedLayoutInfo info;

	unsigned int file_version = 0;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "version") {
			file_version = strtoul(attr.value.c_str(), nullptr, 10);
		}
	}

	// Really basic version checking, in the future we may use this to parse multiple versions
	if (file_version != k_version) {
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "folders") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "folder") {
					uintptr_t item_id = strtoull(
						reader->read_element_text().c_str(), nullptr, 10);

					auto it = node_ptrs.find(item_id);
					Folder *open_item = static_cast<Folder *>(
						it != node_ptrs.end() ? it->second : nullptr);
					info.open_folders.push_back(open_item);
				} else {
					reader->skip_current_element();
				}
			}

		} else if (reader->name() == "timeline") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "sequence") {
					uintptr_t item_id = strtoull(
						reader->read_element_text().c_str(), nullptr, 10);

					auto it = node_ptrs.find(item_id);
					Sequence *open_seq = static_cast<Sequence *>(
						it != node_ptrs.end() ? it->second : nullptr);
					info.open_sequences.push_back(open_seq);
				} else {
					reader->skip_current_element();
				}
			}

		} else if (reader->name() == "viewers") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "viewer") {
					uintptr_t item_id = strtoull(
						reader->read_element_text().c_str(), nullptr, 10);

					auto it = node_ptrs.find(item_id);
					ViewerOutput *open_viewer = static_cast<ViewerOutput *>(
						it != node_ptrs.end() ? it->second : nullptr);
					info.open_viewers.push_back(open_viewer);
				} else {
					reader->skip_current_element();
				}
			}

		} else if (reader->name() == "state") {
			info.state =
				byte_array_from_base64(reader->read_element_text());

		} else if (reader->name() == "data") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "panel") {
					std::string id;
					for (const XmlStreamAttribute &attr :
						 reader->attributes()) {
						if (attr.name == "id") {
							id = attr.value;
						}
					}

					if (!id.empty()) {
						PanelLayoutInfo i;

						while (xml_read_next_start_element(reader)) {
							if (reader->name() == "option") {
								std::string name;

								for (const XmlStreamAttribute &attr :
									 reader->attributes()) {
									if (attr.name == "name") {
										name = attr.value;
									}
								}

								if (!name.empty()) {
									i[name] = reader->read_element_text();
								}
							} else {
								reader->skip_current_element();
							}
						}

						info.panel_data[id] = i;
					}

				} else {
					reader->skip_current_element();
				}
			}

		} else {
			reader->skip_current_element();
		}
	}

	return info;
}

}
