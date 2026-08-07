/***

  Olive - Non-Linear Video Editor
  Copyright (C) 2023 Olive Studios LLC
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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "serializer230220.h"

#include "configaccessor.h"
#include "factory.h"
#include "group/group.h"
#include "serializeddata.h"
#include "olive/core/util/stringutils.h"

namespace olive
{

ProjectSerializer230220::LoadData
ProjectSerializer230220::load(Project *project, XmlStreamReader *reader,
							  LoadType load_type, void *reserved) const
{
	std::map<uintptr_t, std::map<std::string, std::string>> properties;
	std::map<uintptr_t, std::map<uintptr_t, Node::Position>> positions;
	LoadData load_data;
	SerializedData project_data;

	switch (load_type) {
	case k_project: {
		if (reader->name() == "project") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "project") {
					project_data = project->load(reader);
					load_data.node_ptrs = project_data.node_ptrs;
				} else if (reader->name() == "layout") {
					load_data.layout = SerializedLayoutInfo::from_xml(
						reader, project_data.node_ptrs);
				} else {
					reader->skip_current_element();
				}
			}

			post_connect(project->nodes(), &project_data);
		} else {
			reader->skip_current_element();
		}
		break;
	}
	case k_only_markers: {
		if (reader->name() == "markers") {
			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "marker") {
					SerializedMarker marker;
					marker.color = OAK_CONFIG("MarkerColor").toInt();
					for (const XmlStreamAttribute &attr :
						 reader->attributes()) {
						if (attr.name == "name") {
							marker.name = attr.value;
						} else if (attr.name == "in") {
							marker.in = Rational::from_string(attr.value);
						} else if (attr.name == "out") {
							marker.out = Rational::from_string(attr.value);
						} else if (attr.name == "color") {
							marker.color = atoi(attr.value.c_str());
						}
					}
					reader->skip_current_element();
					load_data.markers.push_back(marker);
				} else {
					reader->skip_current_element();
				}
			}
		} else {
			reader->skip_current_element();
		}
		break;
	}
	case k_only_keyframes: {
		if (reader->name() == "keyframes") {
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
									while (xml_read_next_start_element(reader)) {
										if (reader->name() == "element") {
											std::string element_id;
											for (const XmlStreamAttribute &attr :
												 reader->attributes()) {
												if (attr.name == "id") {
													element_id = attr.value;
													break;
												}
											}

											if (element_id.empty()) {
												reader->skip_current_element();
											} else {
												while (
													xml_read_next_start_element(
														reader)) {
													if (reader->name() ==
														"track") {
														std::string track_id;
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

														if (track_id.empty()) {
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

																	key->load(
																		reader,
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
		} else {
			reader->skip_current_element();
		}
		break;
	}
	case k_only_clips:
	case k_only_nodes: {
		if ((load_type == k_only_nodes && reader->name() == "nodes") ||
			(load_type == k_only_clips && reader->name() == "timeline")) {
			std::map<uintptr_t, Node *> skipped_items;

			while (xml_read_next_start_element(reader)) {
				if (reader->name() == "node") {
					std::string id;
					uintptr_t ptr = 0;
					std::vector<uintptr_t> items;

					for (const XmlStreamAttribute &attr :
						 reader->attributes()) {
						if (attr.name == "id") {
							id = attr.value;
						} else if (attr.name == "ptr") {
							ptr = strtoull(attr.value.c_str(), nullptr, 10);
						} else if (attr.name == "items") {
							std::vector<std::string> l =
								core::StringUtils::split(attr.value, ',');
							items.reserve(l.size());
							for (const std::string &s : l) {
								items.push_back(
									strtoull(s.c_str(), nullptr, 10));
							}
						}
					}

					if (id.empty()) {
						fprintf(stderr, "Failed to load node with empty ID\n");
						reader->skip_current_element();
					} else {
						bool dependency_of_item = false;

						if (project && !items.empty()) {
							for (uintptr_t p : items) {
								Node *item_node =
									reinterpret_cast<Node *>(p);
								if (std::find(project->nodes().begin(),
											  project->nodes().end(),
											  item_node) !=
									project->nodes().end()) {
									dependency_of_item = true;
									break;
								}
							}
						}

						if (dependency_of_item) {
							reader->skip_current_element();
						} else {
							Node *node = NodeFactory::create_from_id(id);
							if (!node) {
								fprintf(stderr,
										"Failed to find node with ID %s\n",
										id.c_str());
								reader->skip_current_element();
							} else {
								if (project && node->is_item() && ptr) {
									// If we're pasting an object into the same project, we should re-use the item
									// rather than duplicate.
									Node *existing =
										reinterpret_cast<Node *>(ptr);
									if (std::find(project->nodes().begin(),
												  project->nodes().end(),
												  existing) !=
										project->nodes().end()) {
										// Connect this
										skipped_items.insert({ ptr, existing });

										// Don't continue loading this
										delete node;
										node = nullptr;

										// Skip element
										reader->skip_current_element();
									}
								}

								if (node) {
									// Disable cache while node is being loaded (we'll re-enable it later)
									node->set_caches_enabled(false);
									node->load(reader, &project_data);
									load_data.nodes.push_back(node);
								}
							}
						}
					}

					load_data.node_ptrs = project_data.node_ptrs;
				} else if (reader->name() == "properties") {
					while (xml_read_next_start_element(reader)) {
						if (reader->name() == "node") {
							uintptr_t ptr = 0;

							for (const XmlStreamAttribute &attr :
								 reader->attributes()) {
								if (attr.name == "ptr") {
									ptr = strtoull(attr.value.c_str(), nullptr,
												   10);

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
					reader->skip_current_element();
				}
			}

			if (!skipped_items.empty()) {
				for (auto it = project_data.desired_connections.begin();
					 it != project_data.desired_connections.end();) {
					const SerializedData::SerializedConnection &sc = *it;

					auto si_it = skipped_items.find(sc.output_node);
					if (si_it != skipped_items.end()) {
						// Convert this to a promised connection
						Node::OutputConnection oc = { si_it->second, sc.input };
						load_data.promised_connections.push_back(oc);
						it = project_data.desired_connections.erase(it);
					} else {
						it++;
					}
				}
			}

			// Resolve serialized properties (if any)
			for (auto it = properties.cbegin(); it != properties.cend(); it++) {
				auto n_it = project_data.node_ptrs.find(it->first);
				Node *node = n_it != project_data.node_ptrs.end() ?
								 n_it->second :
								 nullptr;
				if (node) {
					load_data.properties.insert({ node, it->second });
				}
			}

			post_connect(load_data.nodes, &project_data);
		} else {
			reader->skip_current_element();
		}
		break;
	}
	}

	return load_data;
}

void write_node_map(XmlStreamWriter *writer, Node *node,
					const std::vector<Node *> &nodes)
{
	writer->write_start_element("node");

	writer->write_attribute("ptr",
						   std::to_string(reinterpret_cast<uintptr_t>(node)));

	for (auto oc : node->output_connections()) {
		if (std::find(nodes.begin(), nodes.end(), oc.second.node()) !=
			nodes.end()) {
			write_node_map(writer, oc.second.node(), nodes);
		}
	}

	writer->write_end_element();
}

void ProjectSerializer230220::save(XmlStreamWriter *writer,
								   const SaveData &data, void *reserved) const
{
	if (!data.get_only_serialize_markers().empty()) {
		writer->write_start_element("markers");

		writer->write_attribute("version", std::to_string(1));

		for (auto it = data.get_only_serialize_markers().cbegin();
			 it != data.get_only_serialize_markers().cend(); it++) {
			const SerializedMarker &marker = *it;
			writer->write_start_element("marker");
			writer->write_attribute("name", marker.name);
			writer->write_attribute("in", marker.in.to_string());
			writer->write_attribute("out", marker.out.to_string());
			writer->write_attribute("color", std::to_string(marker.color));
			writer->write_end_element(); // marker
		}

		writer->write_end_element(); // markers
	} else if (!data.get_only_serialize_keyframes().empty()) {
		writer->write_start_element("keyframes");

		writer->write_attribute("version", std::to_string(1));

		// Organize keyframes into node+input
		std::map<
			std::string,
			std::map<std::string,
					 std::map<int, std::map<int, std::vector<NodeKeyframe *>>>>>
			organized;

		for (auto it = data.get_only_serialize_keyframes().cbegin();
			 it != data.get_only_serialize_keyframes().cend(); it++) {
			NodeKeyframe *key = *it;
			organized[key->parent()->id()][key->input()][key->element()]
					 [key->track()]
						 .push_back(key);
		}

		for (auto it = organized.cbegin(); it != organized.cend(); it++) {
			writer->write_start_element("node");

			writer->write_attribute("id", it->first);

			for (auto jt = it->second.cbegin(); jt != it->second.cend(); jt++) {
				writer->write_start_element("input");

				writer->write_attribute("id", jt->first);

				for (auto kt = jt->second.cbegin(); kt != jt->second.cend();
					 kt++) {
					writer->write_start_element("element");

					writer->write_attribute("id", std::to_string(kt->first));

					for (auto lt = kt->second.cbegin(); lt != kt->second.cend();
						 lt++) {
						const std::vector<NodeKeyframe *> &keys = lt->second;

						writer->write_start_element("track");

						writer->write_attribute("id", std::to_string(lt->first));

						for (NodeKeyframe *key : keys) {
							writer->write_start_element("key");
							key->save(writer, key->parent()->get_input_data_type(
												  key->input()));
							writer->write_end_element(); // key
						}

						writer->write_end_element(); // track
					}

					writer->write_end_element(); // element
				}

				writer->write_end_element(); // input
			}

			writer->write_end_element(); // node;
		}

		writer->write_end_element(); // keyframes
	} else if (!data.get_only_serialize_nodes().empty()) {
		if (data.type() == k_only_clips) {
			writer->write_start_element("timeline");
		} else {
			writer->write_start_element("nodes");
		}

		writer->write_attribute("version", std::to_string(1));

		for (Node *n : data.get_only_serialize_nodes()) {
			writer->write_start_element("node");

			std::vector<std::string> item_list;
			for (Node *i : data.get_only_serialize_nodes()) {
				if (i->is_item() && i->inputs_from(n, true)) {
					item_list.push_back(
						std::to_string(reinterpret_cast<uintptr_t>(i)));
				}
			}
			if (!item_list.empty()) {
				std::string joined;
				for (size_t i = 0; i < item_list.size(); i++) {
					if (i) {
						joined += ',';
					}
					joined += item_list[i];
				}
				writer->write_attribute("items", joined);
			}

			n->save(writer);
			writer->write_end_element(); // node
		}

		if (!data.get_properties().empty()) {
			writer->write_start_element("properties");
			for (auto it = data.get_properties().cbegin();
				 it != data.get_properties().cend(); it++) {
				writer->write_start_element("node");

				writer->write_attribute(
					"ptr", std::to_string(reinterpret_cast<uintptr_t>(it->first)));

				for (auto jt = it->second.cbegin(); jt != it->second.cend();
					 jt++) {
					writer->write_text_element(jt->first, jt->second);
				}

				writer->write_end_element(); // node
			}
			writer->write_end_element(); // properties
		}

		writer->write_end_element(); // nodes
	} else if (Project *project = data.get_project()) {
		writer->write_start_element("project");

		writer->write_start_element("project");
		project->save(writer);
		writer->write_end_element(); // project

		writer->write_start_element("layout");
		data.get_layout().to_xml(writer);
		writer->write_end_element(); // layout

		writer->write_end_element(); // project
	} else {
		fprintf(stderr, "ProjectSerializer provided nothing to save\n");
	}
}

void ProjectSerializer230220::post_connect(const std::vector<Node *> &nodes,
										  SerializedData *project_data) const
{
	for (const SerializedData::SerializedConnection &con :
		 project_data->desired_connections) {
		auto out_it = project_data->node_ptrs.find(con.output_node);
		if (out_it != project_data->node_ptrs.end()) {
			Node::connect_edge(out_it->second, con.input);
		}
	}

	for (const SerializedData::BlockLink &l : project_data->block_links) {
		Node *a = l.block;
		auto b_it = project_data->node_ptrs.find(l.link);
		Node *b = b_it != project_data->node_ptrs.end() ? b_it->second : nullptr;

		Node::link(a, b);
	}

	for (auto it = nodes.cbegin(); it != nodes.cend(); it++) {
		Node *n = *it;

		n->PostLoadEvent(project_data);

		n->set_caches_enabled(true);
	}
}

}
