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

#include "serializer220403.h"

#include <cstdio>
#include <cstdlib>

#include "configaccessor.h"
#include "factory.h"
#include "block/clip/clip.h"
#include "group/group.h"

namespace olive
{

ProjectSerializer220403::LoadData
ProjectSerializer220403::load(Project *project, XmlStreamReader *reader,
							  LoadType load_type, void *reserved) const
{
	std::map<uintptr_t, std::map<std::string, std::string>> properties;
	std::map<uintptr_t, std::map<uintptr_t, Node::Position>> positions;
	XMLNodeData xml_node_data;

	LoadData load_data;

	if ((load_type == k_project && reader->name() == "project") ||
		((load_type == k_only_nodes && reader->name() == "nodes") ||
		 (load_type == k_only_clips && reader->name() == "timeline")) ||
		(load_type == k_only_keyframes && reader->name() == "keyframes") ||
		(load_type == k_only_markers && reader->name() == "markers")) {
		while (xml_read_next_start_element(reader)) {
			if (reader->name() == "layout") {
				// Since the main window's functions have to occur in the GUI thread (and we're likely
				// loading in a secondary thread), we load all necessary data into a separate struct so we
				// can continue loading and queue it with the main window so it can handle the data
				// appropriately in its own thread.

				load_data.layout = SerializedLayoutInfo::from_xml(
					reader, xml_node_data.node_ptrs);

			} else if (reader->name() == "uuid") {
				if (project) {
					project->set_uuid(reader->read_element_text());
				} else {
					reader->skip_current_element();
				}

			} else if (reader->name() == "nodes") {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "node") {
						bool is_root = false;
						bool is_cm = false;
						bool is_settings = false;
						std::string id;

						{
							for (const XmlStreamAttribute &attr :
								 reader->attributes()) {
								if (attr.name == "id") {
									id = attr.value;
								} else if (attr.name == "root" &&
										   attr.value == "1") {
									is_root = true;
								} else if (attr.name == "cm" &&
										   attr.value == "1") {
									is_cm = true;
								} else if (attr.name == "settings" &&
										   attr.value == "1") {
									is_settings = true;
								}
							}
						}

						if (id.empty()) {
							fprintf(stderr,
									"Failed to load node with empty ID\n");
							reader->skip_current_element();
						} else {
							Node *node;
							bool handled_elsewhere = false;

							if (is_root) {
								project->initialize();
								node = project->root();
							} else if (is_cm) {
								load_color_manager(reader, project);
								handled_elsewhere = true;
							} else if (is_settings) {
								load_project_settings(reader, project);
								handled_elsewhere = true;
							} else {
								node = NodeFactory::create_from_id(id);
							}

							if (!handled_elsewhere) {
								if (!node) {
									fprintf(stderr,
											"Failed to find node with ID %s\n",
											id.c_str());
									reader->skip_current_element();
								} else {
									load_node(node, xml_node_data, reader);
									if (project) {
										project->add_node(node);
									} else {
										load_data.nodes.push_back(node);
									}
								}
							}
						}
					} else {
						reader->skip_current_element();
					}
				}

			} else if (reader->name() == "keyframes") {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "node") {
						std::string node_id;
						for (const XmlStreamAttribute &attr :
							 reader->attributes()) {
							if (attr.name == "id") {
								node_id = attr.value;
								break;
							}
						}

						Node *n = nullptr;
						if (!node_id.empty()) {
							n = NodeFactory::create_from_id(node_id);
						}

						if (!n) {
							reader->skip_current_element();
						} else {
							while (xml_read_next_start_element(reader)) {
								if (reader->name() == "input") {
									std::string input_id;
									for (const XmlStreamAttribute &attr :
										 reader->attributes()) {
										if (attr.name == "id") {
											input_id = attr.value;
											break;
										}
									}

									if (input_id.empty()) {
										reader->skip_current_element();
									} else {
										while (
											xml_read_next_start_element(reader)) {
											if (reader->name() == "element") {
												std::string element_id;
												for (const XmlStreamAttribute
														 &attr :
													 reader->attributes()) {
													if (attr.name == "id") {
														element_id = attr.value;
														break;
													}
												}

												if (element_id.empty()) {
													reader
														->skip_current_element();
												} else {
													while (
														xml_read_next_start_element(
															reader)) {
														if (reader->name() ==
															"track") {
															std::string
																track_id;
															for (const XmlStreamAttribute
																	 &attr :
																 reader->attributes()) {
																if (attr.name ==
																	"id") {
																	track_id =
																		attr.value;
																	break;
																}
															}

															if (track_id
																	.empty()) {
																reader
																	->skip_current_element();
															} else {
																while (
																	xml_read_next_start_element(
																		reader)) {
																	if (reader
																			->name() ==
																		"key") {
																		NodeKeyframe
																			*key =
																				new NodeKeyframe();
																		key->set_input(
																			input_id);
																		key->set_element(
																			atoi(element_id
																					 .c_str()));
																		key->set_track(
																			atoi(track_id
																					 .c_str()));

																		load_keyframe(
																			reader,
																			key,
																			n->get_input_data_type(
																				input_id));

																		load_data
																			.keyframes
																				[node_id]
																			.push_back(
																				key);
																	} else {
																		reader
																			->skip_current_element();
																	}
																}
															}
														} else {
															reader
																->skip_current_element();
														}
													}
												}
											} else {
												reader->skip_current_element();
											}
										}
									}
								} else {
									reader->skip_current_element();
								}
							}
						}

						delete n;
					} else {
						reader->skip_current_element();
					}
				}

			} else if (reader->name() == "markers") {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "marker") {
						TimelineMarker *marker = new TimelineMarker();
						load_marker(reader, marker);
						load_data.markers.push_back(marker);
					} else {
						reader->skip_current_element();
					}
				}

			} else if (reader->name() == "positions") {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "context") {
						uintptr_t context_ptr = 0;
						for (const XmlStreamAttribute &attr :
							 reader->attributes()) {
							if (attr.name == "ptr") {
								context_ptr =
									strtoull(attr.value.c_str(), nullptr, 10);
								break;
							}
						}

						if (context_ptr) {
							while (xml_read_next_start_element(reader)) {
								if (reader->name() == "node") {
									uintptr_t node_ptr;
									Node::Position node_pos;

									if (load_position(reader, &node_ptr,
													 &node_pos)) {
										if (node_ptr) {
											positions[context_ptr].insert(
												{ node_ptr, node_pos });
										} else {
											fprintf(stderr,
													"Failed to find pointer "
													"for node position\n");
											reader->skip_current_element();
										}
									}
								} else {
									reader->skip_current_element();
								}
							}
						} else {
							fprintf(stderr,
									"Attempted to load context with no "
									"pointer\n");
							reader->skip_current_element();
						}

					} else {
						reader->skip_current_element();
					}
				}

			} else if (reader->name() == "properties") {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "node") {
						uintptr_t ptr = 0;

						for (const XmlStreamAttribute &attr :
							 reader->attributes()) {
							if (attr.name == "ptr") {
								ptr =
									strtoull(attr.value.c_str(), nullptr, 10);

								// Only attribute we're looking for right now
								break;
							}
						}

						if (ptr) {
							std::map<std::string, std::string>
								properties_for_node;
							while (xml_read_next_start_element(reader)) {
								properties_for_node.insert(
									{ std::string(reader->name()),
									  reader->read_element_text() });
							}
							properties.insert({ ptr, properties_for_node });
						}
					} else {
						reader->skip_current_element();
					}
				}

			} else {
				// Skip this
				reader->skip_current_element();
			}
		}
	}

	// Resolve positions
	for (auto it = positions.cbegin(); it != positions.cend(); it++) {
		auto ctx_it = xml_node_data.node_ptrs.find(it->first);
		Node *ctx = ctx_it != xml_node_data.node_ptrs.end() ? ctx_it->second :
															  nullptr;
		if (ctx) {
			for (auto jt = it->second.cbegin(); jt != it->second.cend(); jt++) {
				auto n_it = xml_node_data.node_ptrs.find(jt->first);
				Node *n = n_it != xml_node_data.node_ptrs.end() ?
							  n_it->second :
							  nullptr;
				if (n) {
					ctx->set_node_position_in_context(n, jt->second);
				}
			}
		}
	}

	// Make connections
	post_connect(xml_node_data);

	load_data.node_ptrs = xml_node_data.node_ptrs;
	load_data.node_uuids = xml_node_data.node_uuids;

	// Resolve serialized properties (if any)
	for (auto it = properties.cbegin(); it != properties.cend(); it++) {
		auto n_it = xml_node_data.node_ptrs.find(it->first);
		Node *node =
			n_it != xml_node_data.node_ptrs.end() ? n_it->second : nullptr;
		if (node) {
			load_data.properties.insert({ node, it->second });
		}
	}

	// Re-enable caches and resolve tracks
	const std::vector<Node *> &nodes =
		project ? project->nodes() : load_data.nodes;
	for (Node *n : nodes) {
		n->set_caches_enabled(true);

		if (Track *t = dynamic_cast<Track *>(n)) {
			for (int i = 0; i < t->input_array_size(Track::k_block_input); i++) {
				Block *b = static_cast<Block *>(
					t->get_connected_output(Track::k_block_input, i));
				if (!b->track()) {
					t->append_block(b);
				}
			}
		}

		// Clear duplicate label (to facilitate #2147)
		if (ClipBlock *c = dynamic_cast<ClipBlock *>(n)) {
			if (c->connected_viewer() &&
				c->get_label() == c->connected_viewer()->get_label()) {
				c->set_label(std::string());
			}
		}
	}

	return load_data;
}

void ProjectSerializer220403::load_node(Node *node, XMLNodeData &xml_node_data,
									   XmlStreamReader *reader) const
{
	while (xml_read_next_start_element(reader)) {
		if (is_cancelled()) {
			return;
		}

		if (reader->name() == "input") {
			load_input(node, reader, xml_node_data);
		} else if (reader->name() == "ptr") {
			uintptr_t ptr =
				strtoull(reader->read_element_text().c_str(), nullptr, 10);
			xml_node_data.node_ptrs.insert({ ptr, node });
		} else if (reader->name() == "label") {
			node->set_label(reader->read_element_text());
		} else if (reader->name() == "uuid") {
			xml_node_data.node_uuids.insert(
				{ node, reader->read_element_text() });
		} else if (reader->name() == "color") {
			node->set_override_color(
				atoi(reader->read_element_text().c_str()));
		} else if (reader->name() == "links") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "link") {
					xml_node_data.block_links.push_back(
						{ node,
						  strtoull(reader->read_element_text().c_str(), nullptr,
								   10) });
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "custom") {
			load_node_custom(reader, node, xml_node_data);
		} else if (reader->name() == "connections") {
			// Load connections
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "connection") {
					std::string param_id;
					int ele = -1;

					for (const XmlStreamAttribute &attr :
						 reader->attributes()) {
						if (attr.name == "element") {
							ele = atoi(attr.value.c_str());
						} else if (attr.name == "input") {
							param_id = attr.value;
						}
					}

					std::string output_node_id;

					while (xml_read_next_start_element(reader)) {
						if (reader->name() == "output") {
							output_node_id = reader->read_element_text();
						} else {
							reader->skip_current_element();
						}
					}

					xml_node_data.desired_connections.push_back(
						{ NodeInput(node, param_id, ele),
						  strtoull(output_node_id.c_str(), nullptr, 10) });
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "hints") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "hint") {
					std::string input;
					int element = -1;

					for (const XmlStreamAttribute &attr :
						 reader->attributes()) {
						if (attr.name == "input") {
							input = attr.value;
						} else if (attr.name == "element") {
							element = atoi(attr.value.c_str());
						}
					}

					Node::ValueHint vh;
					load_value_hint(&vh, reader);
					node->set_value_hint_for_input(input, vh, element);
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "caches") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "audio") {
					node->audio_playback_cache()->set_uuid(
						reader->read_element_text());
				} else if (reader->name() == "video") {
					node->video_frame_cache()->set_uuid(
						reader->read_element_text());
				} else if (reader->name() == "thumb") {
					node->thumbnail_cache()->set_uuid(
						reader->read_element_text());
				} else if (reader->name() == "waveform") {
					node->waveform_cache()->set_uuid(
						reader->read_element_text());
				} else {
					reader->skip_current_element();
				}
			}
		} else {
			reader->skip_current_element();
		}
	}

	node->LoadFinishedEvent();
}

void ProjectSerializer220403::load_color_manager(XmlStreamReader *reader,
											   Project *project) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "input") {
			std::string id;
			for (const XmlStreamAttribute &attr : reader->attributes()) {
				if (attr.name == "id") {
					id = attr.value;
				}
			}

			if (id == "config" || id == "default_input" ||
				id == "reference_space") {
				std::string value;

				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "primary") {
						while (xml_read_next_start_element(reader)) {
							if (reader->name() == "standard") {
								while (xml_read_next_start_element(reader)) {
									if (reader->name() == "track") {
										value = reader->read_element_text();
									} else {
										reader->skip_current_element();
									}
								}
							} else {
								reader->skip_current_element();
							}
						}
					} else {
						reader->skip_current_element();
					}
				}

				if (id == "default_input") {
					// Default color space
					// NOTE: Stupidly, we saved these as integers which means we can't add anything to the OCIO
					//       config. So we must convert back to string here.
					static const std::vector<std::string> list = {
						"Linear",
						"Filmic Log Encoding",
						"sRGB OETF",
						"Apple DCI-P3 D65",
						"AppleP3 sRGB OETF",
						"BT.1886 EOTF",
						"AppleP3 Filmic Log Encoding",
						"BT.1886 Filmic Log Encoding",
						"Fuji F-Log OETF",
						"Fuji F-Log F-Gamut",
						"Panasonic V-Log V-Gamut",
						"Arri Wide Gamut / LogC EI 800",
						"Arri Wide Gamut / LogC EI 400",
						"Blackmagic Film Wide Gamut (Gen 5)",
						"Rec.709 OETF",
						"Non-Colour Data"
					};
					int num_value = atoi(value.c_str());
					value = list.at(num_value);
					project->set_default_input_color_space(value);
				} else if (id == "reference_space") {
					// Reference space
					if (value == "1") {
						value = ocio::ROLE_COMPOSITING_LOG;
					} else {
						value = ocio::ROLE_SCENE_LINEAR;
					}
					project->set_color_reference_space(value);
				} else {
					// Config filename
					project->set_color_config_filename(value);
				}
			} else {
				reader->skip_current_element();
			}
		} else {
			reader->skip_current_element();
		}
	}
}

void ProjectSerializer220403::load_project_settings(XmlStreamReader *reader,
												  Project *project) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "input") {
			std::string id;
			for (const XmlStreamAttribute &attr : reader->attributes()) {
				if (attr.name == "id") {
					id = attr.value;
				}
			}

			if (id == "cache_setting" || id == "cache_path") {
				std::string value;

				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "primary") {
						while (xml_read_next_start_element(reader)) {
							if (reader->name() == "standard") {
								while (xml_read_next_start_element(reader)) {
									if (reader->name() == "track") {
										value = reader->read_element_text();
									} else {
										reader->skip_current_element();
									}
								}
							} else {
								reader->skip_current_element();
							}
						}
					} else {
						reader->skip_current_element();
					}
				}

				if (id == "cache_setting") {
					project->set_cache_location_setting(
						static_cast<Project::CacheSetting>(
							atoi(value.c_str())));
				} else {
					project->set_custom_cache_path(value);
				}
			} else {
				reader->skip_current_element();
			}
		} else {
			reader->skip_current_element();
		}
	}
}

void ProjectSerializer220403::load_input(Node *node, XmlStreamReader *reader,
										XMLNodeData &xml_node_data) const
{
	if (dynamic_cast<NodeGroup *>(node)) {
		// Ignore input of group
		reader->skip_current_element();
		return;
	}

	std::string param_id;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "id") {
			param_id = attr.value;

			break;
		}
	}

	if (param_id.empty()) {
		fprintf(stderr, "Failed to load parameter with missing ID\n");
		reader->skip_current_element();
		return;
	}

	if (!node->has_input_with_id(param_id)) {
		fprintf(stderr, "Failed to load parameter that didn't exist: %s\n",
				param_id.c_str());
		reader->skip_current_element();
		return;
	}

	while (xml_read_next_start_element(reader)) {
		if (is_cancelled()) {
			return;
		}

		if (reader->name() == "primary") {
			// Load primary immediate
			load_immediate(reader, node, param_id, -1, xml_node_data);
		} else if (reader->name() == "subelements") {
			// Load subelements
			for (const XmlStreamAttribute &attr : reader->attributes()) {
				if (attr.name == "count") {
					node->input_array_resize(param_id,
											 atoi(attr.value.c_str()));
				}
			}

			int element_counter = 0;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "element") {
					load_immediate(reader, node, param_id, element_counter,
								  xml_node_data);

					element_counter++;
				} else {
					reader->skip_current_element();
				}
			}
		} else {
			reader->skip_current_element();
		}
	}
}

void ProjectSerializer220403::load_immediate(XmlStreamReader *reader,
											Node *node,
											const std::string &input,
											int element,
											XMLNodeData &xml_node_data) const
{
	(void) xml_node_data;

	NodeValue::Type data_type = node->get_input_data_type(input);

	// HACK: SubtitleParams contain the actual subtitle data, so loading/replacing it will overwrite
	//       the valid subtitles. We hack around it by simply skipping loading subtitles, we'll see
	//       if this ends up being an issue in the future.
	if (data_type == NodeValue::k_subtitle_params) {
		reader->skip_current_element();
		return;
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "standard") {
			// Load standard value
			int val_index = 0;

			while (xml_read_next_start_element(reader)) {
				if (is_cancelled()) {
					return;
				}

				if (reader->name() == "track") {
					Variant value_on_track;

					if (data_type == NodeValue::k_video_params) {
						VideoParams vp;
						vp.load(reader);
						value_on_track = Variant::from_value(vp);
					} else if (data_type == NodeValue::k_audio_params) {
						AudioParams ap =
							TypeSerializer::load_audio_params(reader);
						value_on_track = Variant::from_value(ap);
					} else {
						std::string value_text = reader->read_element_text();

						if (!value_text.empty()) {
							value_on_track = NodeValue::string_to_value(
								data_type, value_text, true);
						}
					}

					node->set_split_standard_value_on_track(input, val_index,
													   value_on_track, element);

					val_index++;
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "keyframing" &&
				   node->is_input_keyframable(input)) {
			node->set_input_is_keyframing(
				input, atoi(reader->read_element_text().c_str()), element);
		} else if (reader->name() == "keyframes") {
			int track = 0;

			while (xml_read_next_start_element(reader)) {
				if (is_cancelled()) {
					return;
				}

				if (reader->name() == "track") {
					while (xml_read_next_start_element(reader)) {
						if (is_cancelled()) {
							return;
						}

						if (reader->name() == "key") {
							NodeKeyframe *key = new NodeKeyframe();
							key->set_input(input);
							key->set_element(element);
							key->set_track(track);

							load_keyframe(reader, key, data_type);

							// Replaces the former QObject setParent() (which
							// registered the keyframe through childEvent)
							node->add_keyframe(key);
						} else {
							reader->skip_current_element();
						}
					}

					track++;
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "csinput") {
			node->set_input_property(input, "col_input",
								   reader->read_element_text());
		} else if (reader->name() == "csdisplay") {
			node->set_input_property(input, "col_display",
								   reader->read_element_text());
		} else if (reader->name() == "csview") {
			node->set_input_property(input, "col_view",
								   reader->read_element_text());
		} else if (reader->name() == "cslook") {
			node->set_input_property(input, "col_look",
								   reader->read_element_text());
		} else {
			reader->skip_current_element();
		}
	}
}

void ProjectSerializer220403::load_keyframe(XmlStreamReader *reader,
										   NodeKeyframe *key,
										   NodeValue::Type data_type) const
{
	std::string key_input;
	PointF key_in_handle;
	PointF key_out_handle;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (is_cancelled()) {
			return;
		}

		if (attr.name == "input") {
			key_input = attr.value;
		} else if (attr.name == "time") {
			key->set_time(Rational::from_string(attr.value));
		} else if (attr.name == "type") {
			key->set_type_no_bezier_adj(static_cast<NodeKeyframe::Type>(
				atoi(attr.value.c_str())));
		} else if (attr.name == "inhandlex") {
			key_in_handle.set_x(strtod(attr.value.c_str(), nullptr));
		} else if (attr.name == "inhandley") {
			key_in_handle.set_y(strtod(attr.value.c_str(), nullptr));
		} else if (attr.name == "outhandlex") {
			key_out_handle.set_x(strtod(attr.value.c_str(), nullptr));
		} else if (attr.name == "outhandley") {
			key_out_handle.set_y(strtod(attr.value.c_str(), nullptr));
		}
	}

	key->set_value(
		NodeValue::string_to_value(data_type, reader->read_element_text(), true));

	key->set_bezier_control_in(key_in_handle);
	key->set_bezier_control_out(key_out_handle);
}

bool ProjectSerializer220403::load_position(XmlStreamReader *reader,
										   uintptr_t *node_ptr,
										   Node::Position *pos) const
{
	bool got_node_ptr = false;
	bool got_pos_x = false;
	bool got_pos_y = false;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "ptr") {
			*node_ptr = strtoull(attr.value.c_str(), nullptr, 10);
			got_node_ptr = true;
			break;
		}
	}

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "x") {
			pos->position.set_x(strtod(reader->read_element_text().c_str(),
									   nullptr));
			got_pos_x = true;
		} else if (reader->name() == "y") {
			pos->position.set_y(strtod(reader->read_element_text().c_str(),
									   nullptr));
			got_pos_y = true;
		} else if (reader->name() == "expanded") {
			pos->expanded = atoi(reader->read_element_text().c_str());
		} else {
			reader->skip_current_element();
		}
	}

	return got_node_ptr && got_pos_x && got_pos_y;
}

void ProjectSerializer220403::post_connect(const XMLNodeData &xml_node_data) const
{
	for (const XMLNodeData::SerializedConnection &con :
		 xml_node_data.desired_connections) {
		auto out_it = xml_node_data.node_ptrs.find(con.output_node);
		if (out_it != xml_node_data.node_ptrs.end()) {
			Node::connect_edge(out_it->second, con.input);
		}
	}

	for (const XMLNodeData::BlockLink &l : xml_node_data.block_links) {
		Node *a = l.block;
		auto b_it = xml_node_data.node_ptrs.find(l.link);
		Node *b =
			b_it != xml_node_data.node_ptrs.end() ? b_it->second : nullptr;

		Node::link(a, b);
	}

	for (const XMLNodeData::GroupLink &l : xml_node_data.group_input_links) {
		auto in_it = xml_node_data.node_ptrs.find(l.input_node);
		if (in_it != xml_node_data.node_ptrs.end()) {
			Node *input_node = in_it->second;
			NodeInput resolved(input_node, l.input_id, l.input_element);

			l.group->add_input_passthrough(resolved, l.passthrough_id);

			l.group->set_input_flag(l.passthrough_id,
								  InputFlag(l.custom_flags.value()));

			if (!l.custom_name.empty()) {
				l.group->set_input_name(l.passthrough_id, l.custom_name);
			}

			l.group->set_input_data_type(l.passthrough_id, l.data_type);

			l.group->set_default_value(l.passthrough_id, l.default_val);

			for (auto it = l.custom_properties.cbegin();
				 it != l.custom_properties.cend(); it++) {
				l.group->set_input_property(l.passthrough_id, it->first,
										  it->second);
			}
		}
	}

	for (auto it = xml_node_data.group_output_links.cbegin();
		 it != xml_node_data.group_output_links.cend(); it++) {
		auto out_it = xml_node_data.node_ptrs.find(it->second);
		if (out_it != xml_node_data.node_ptrs.end()) {
			it->first->set_output_passthrough(out_it->second);
		}
	}
}

void ProjectSerializer220403::load_node_custom(XmlStreamReader *reader,
											 Node *node,
											 XMLNodeData &xml_node_data) const
{
	// Viewer-based nodes
	if (ViewerOutput *viewer = dynamic_cast<ViewerOutput *>(node)) {
		Footage *footage = dynamic_cast<Footage *>(node);

		while (xml_read_next_start_element(reader)) {
			if (reader->name() == "points") {
				load_timeline_points(reader, viewer);
			} else if (reader->name() == "timestamp" && footage) {
				footage->set_timestamp(strtoll(
					reader->read_element_text().c_str(), nullptr, 10));
			} else {
				reader->skip_current_element();
			}
		}

	} else if (Track *track = dynamic_cast<Track *>(node)) {
		while (xml_read_next_start_element(reader)) {
			if (reader->name() == "height") {
				track->set_track_height(
					strtod(reader->read_element_text().c_str(), nullptr));
			} else {
				reader->skip_current_element();
			}
		}

	} else if (NodeGroup *group = dynamic_cast<NodeGroup *>(node)) {
		while (xml_read_next_start_element(reader)) {
			if (reader->name() == "inputpassthroughs") {
				while (xml_read_next_start_element(reader)) {
					if (reader->name() == "inputpassthrough") {
						XMLNodeData::GroupLink link;

						link.group = group;

						while (xml_read_next_start_element(reader)) {
							if (reader->name() == "node") {
								link.input_node = strtoull(
									reader->read_element_text().c_str(),
									nullptr, 10);
							} else if (reader->name() == "input") {
								link.input_id = reader->read_element_text();
							} else if (reader->name() == "element") {
								link.input_element = atoi(
									reader->read_element_text().c_str());
							} else if (reader->name() == "id") {
								link.passthrough_id =
									reader->read_element_text();
							} else if (reader->name() == "name") {
								link.custom_name = reader->read_element_text();
							} else if (reader->name() == "flags") {
								link.custom_flags = InputFlags(strtoull(
									reader->read_element_text().c_str(),
									nullptr, 10));
							} else if (reader->name() == "type") {
								link.data_type =
									NodeValue::get_data_type_from_name(
										reader->read_element_text());
							} else if (reader->name() == "default") {
								link.default_val = NodeValue::string_to_value(
									link.data_type, reader->read_element_text(),
									false);
							} else if (reader->name() == "properties") {
								while (xml_read_next_start_element(reader)) {
									if (reader->name() == "property") {
										std::string key;
										std::string value;

										while (
											xml_read_next_start_element(reader)) {
											if (reader->name() == "key") {
												key =
													reader->read_element_text();
											} else if (reader->name() ==
													   "value") {
												value =
													reader->read_element_text();
											} else {
												reader
													->skip_current_element();
											}
										}

										if (!key.empty()) {
											link.custom_properties.insert(
												{ key, value });
										}
									} else {
										reader->skip_current_element();
									}
								}
							} else {
								reader->skip_current_element();
							}
						}

						xml_node_data.group_input_links.push_back(link);
					} else {
						reader->skip_current_element();
					}
				}
			} else if (reader->name() == "outputpassthrough") {
				xml_node_data.group_output_links.insert(
					{ group,
					  strtoull(reader->read_element_text().c_str(), nullptr,
							   10) });
			} else {
				reader->skip_current_element();
			}
		}

	} else {
		reader->skip_current_element();
	}
}

void ProjectSerializer220403::load_timeline_points(XmlStreamReader *reader,
												 ViewerOutput *viewer) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "markers") {
			load_marker_list(reader, viewer->get_markers());
		} else if (reader->name() == "workarea") {
			load_work_area(reader, viewer->get_work_area());
		} else {
			reader->skip_current_element();
		}
	}
}

void ProjectSerializer220403::load_marker(XmlStreamReader *reader,
										 TimelineMarker *marker) const
{
	Rational in, out;

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "name") {
			marker->set_name(attr.value);
		} else if (attr.name == "in") {
			in = Rational::from_string(attr.value);
		} else if (attr.name == "out") {
			out = Rational::from_string(attr.value);
		} else if (attr.name == "color") {
			marker->set_color(atoi(attr.value.c_str()));
		}
	}

	marker->set_time(TimeRange(in, out));

	// This element has no inner text, so just skip it
	reader->skip_current_element();
}

void ProjectSerializer220403::load_work_area(XmlStreamReader *reader,
										   TimelineWorkArea *workarea) const
{
	Rational range_in = workarea->in();
	Rational range_out = workarea->out();

	for (const XmlStreamAttribute &attr : reader->attributes()) {
		if (attr.name == "enabled") {
			workarea->set_enabled(attr.value != "0");
		} else if (attr.name == "in") {
			range_in = Rational::from_string(attr.value);
		} else if (attr.name == "out") {
			range_out = Rational::from_string(attr.value);
		}
	}

	TimeRange loaded_workarea(range_in, range_out);

	if (loaded_workarea != workarea->range()) {
		workarea->set_range(loaded_workarea);
	}

	reader->skip_current_element();
}

void ProjectSerializer220403::load_marker_list(XmlStreamReader *reader,
											 TimelineMarkerList *markers) const
{
	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "marker") {
			TimelineMarker *marker = new TimelineMarker(markers);
			load_marker(reader, marker);
		} else {
			reader->skip_current_element();
		}
	}
}

void ProjectSerializer220403::load_value_hint(Node::ValueHint *hint,
											XmlStreamReader *reader) const
{
	std::vector<NodeValue::Type> types;

	while (xml_read_next_start_element(reader)) {
		if (reader->name() == "types") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "type") {
					types.push_back(static_cast<NodeValue::Type>(
						atoi(reader->read_element_text().c_str())));
				} else {
					reader->skip_current_element();
				}
			}
		} else if (reader->name() == "index") {
			hint->set_index(atoi(reader->read_element_text().c_str()));
		} else if (reader->name() == "tag") {
			hint->set_tag(reader->read_element_text());
		} else {
			reader->skip_current_element();
		}
	}

	hint->set_type(types);
}

}
